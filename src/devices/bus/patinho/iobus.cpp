// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    I/O bus of the Patinho Feio (Escola Politecnica da USP, 1972)

    See iobus.h for the register and flip-flops of an interface board.

***************************************************************************/

#include "emu.h"
#include "iobus.h"

#define LOG_VACANT  (1U << 1)   // I/O aimed at a channel with no card in it
#define LOG_FUNC    (1U << 2)   // every FNC reaching a card

#define VERBOSE (0)
#include "logmacro.h"

DEFINE_DEVICE_TYPE(PATINHO_IO_BUS,  patinho_io_bus_device,  "patinho_io_bus",  "Patinho Feio I/O bus")
DEFINE_DEVICE_TYPE(PATINHO_IO_SLOT, patinho_io_slot_device, "patinho_io_slot", "Patinho Feio I/O slot")

patinho_io_bus_device::patinho_io_bus_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, PATINHO_IO_BUS, tag, owner, clock)
	, m_int_handler(*this)
	, m_tx_handler(*this)
	, m_tick_handler(*this)
	, m_int_state(false)
{
	std::fill(std::begin(m_card), std::end(m_card), nullptr);
}

patinho_io_slot_device::patinho_io_slot_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, PATINHO_IO_SLOT, tag, owner, clock)
	, device_single_card_slot_interface<device_patinho_io_card_interface>(mconfig, *this)
	, m_bus(*this, finder_base::DUMMY_TAG)
{
}

void patinho_io_slot_device::device_start()
{
}

device_patinho_io_card_interface::device_patinho_io_card_interface(const machine_config &mconfig, device_t &device)
	: device_interface(device, "patinhoio")
	, m_data(0)
	, m_control(false)
	, m_status(false)
	, m_irq_request(false)
	, m_irq_enable(false)
	, m_irq_latch(false)
{
}

device_patinho_io_card_interface::~device_patinho_io_card_interface()
{
}

void patinho_io_slot_device::device_resolve_objects()
{
	assert(m_channel < patinho_io_bus_device::CHANNELS);
	if (device_patinho_io_card_interface *const card = get_card_device())
		card->set_bus(*m_bus, m_channel);
}

void device_patinho_io_card_interface::interface_pre_start()
{
	if (!m_bus)
		throw device_missing_dependencies();       // as bus/ss50 does
	if (!m_bus->started())
		throw device_missing_dependencies();       // as bus/a2bus does
	m_bus->add_card(m_channel, *this);
}

void device_patinho_io_card_interface::interface_post_start()
{
	// Registered in the base class so that no card can forget the five state
	// elements chapter 12 gives to every interface board.
	device().save_item(NAME(m_data));
	device().save_item(NAME(m_control));
	device().save_item(NAME(m_status));
	device().save_item(NAME(m_irq_request));
	device().save_item(NAME(m_irq_enable));
	device().save_item(NAME(m_irq_latch));
}

void patinho_io_bus_device::device_start()
{
	std::fill(std::begin(m_card), std::end(m_card), nullptr);
	m_int_state = false;
	save_item(NAME(m_int_state));
}

/* Page A.11 and page 12.17: PREPARACAO clears CONTROLE, ESTADO, PEDIDO and
   PERMITE/IMPEDE on every interface.  device_reset() of this bus device runs
   on a machine reset; reset_bus() is what the panel button calls. */
void patinho_io_bus_device::device_reset()
{
	m_int_state = false;
	m_int_handler(CLEAR_LINE);   // never leave the line and the flag disagreeing
}

void patinho_io_bus_device::reset_bus()
{
	for (device_patinho_io_card_interface *const c : m_card)
		if (c)
			c->card_reset();
	update_int();
}

void device_patinho_io_card_interface::card_reset()
{
	m_data = 0;
	m_control = false;
	m_status = false;        // "desligado" = busy, per page 12.17
	m_irq_request = false;
	m_irq_enable = false;
	m_irq_latch = false;
	control_w(0);
	status_w(0);
}

void patinho_io_bus_device::add_card(unsigned channel, device_patinho_io_card_interface &card)
{
	// Belt and braces: the machine config gives each channel a single socket,
	// so this cannot fire today.  It is what keeps a future two-socket channel
	// from failing silently.  Molde: nubus.cpp:378-381.
	if (m_card[channel])
		fatalerror("I/O channel /%X is already taken by %s; %s cannot have it too\n",
				channel, m_card[channel]->device().tag(), card.device().tag());
	m_card[channel] = &card;
}

void device_patinho_io_card_interface::set_control(bool state)
{
	if (state == m_control)
		return;
	m_control = state;
	control_w(state ? 1 : 0);
}

void device_patinho_io_card_interface::set_status(bool ready)
{
	if (ready != m_status)
	{
		m_status = ready;
		status_w(ready ? 1 : 0);
	}
	update_irq_request();
}

/* Page 12.15 of the July 1977 assembler manual: "ESTADO ligado liga o PEDIDO
   automaticamente se for permitido.  ESTADO desligado nao altera a situacao do
   PEDIDO"; page A.11 gives FNC /n4 as its only reset.  PEDIDO is therefore a
   latch with a level-sensitive set input: while (ESTADO AND PERMITE) holds,
   "FNC /n4" cannot clear the request, which is why page 12.11 tells the
   programmer to clear ESTADO first.  An edge-triggered model would hide that;
   (latch OR set_cond) reproduces both orderings.

   A card whose PEDIDO is not derived from ESTADO overrides irq_from_status():
   on the synthesiser time base, ESTADO selects which end of the cable makes
   the clock and the request comes from the tick. */
void device_patinho_io_card_interface::update_irq_request()
{
	bool const set_cond = irq_from_status() && m_status && m_irq_enable;

	if (set_cond)
		m_irq_latch = true;

	bool const request = m_irq_latch || set_cond;
	if (request != m_irq_request)
	{
		m_irq_request = request;
		m_bus->update_int();
	}
}

void device_patinho_io_card_interface::set_irq_request(bool state)
{
	m_irq_latch = state;
	update_irq_request();
}

// The seven commands standardised by the LSD interfaces, chapter 13 of the
// July 1977 assembler manual.  The card is notified either way.
void device_patinho_io_card_interface::do_func(uint8_t cmd)
{
	switch (cmd)
	{
	case 0x0:                                       // impede interrupcao
		// Page 12.15: this flip-flop only gates the SETTING of PEDIDO, so a
		// request already latched stays latched.
		m_irq_enable = false;
		update_irq_request();
		break;
	case 0x1: set_status(false); break;             // ESTADO = busy
	case 0x2: set_status(true); break;              // ESTADO = ready
	case 0x4:                                       // limpa PEDIDO
		// Clears the latch and then re-derives, so that a request whose set
		// input is still asserted comes straight back -- which is exactly what
		// page 12.11 warns the programmer about.
		m_irq_latch = false;
		update_irq_request();
		break;
	case 0x5:                                       // permite interrupcao
		m_irq_enable = true;
		update_irq_request();
		break;
	case 0x6:                                       // CONTROLE on, ESTADO busy
		set_status(false);
		set_control(true);
		break;
	case 0x7:                                       // CONTROLE off
		set_control(false);
		break;
	default: break;
	}
	func_w(cmd);                                    // the card always finds out
}

void device_patinho_io_card_interface::func_w(uint8_t cmd)
{
	static constexpr uint16_t STANDARD = (1 << 0) | (1 << 1) | (1 << 2)
			| (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7);
	if (!BIT(STANDARD, cmd))
		device().logerror("FNC /%X%X is not decoded by this card\n", m_channel, cmd);
}

void device_patinho_io_card_interface::receive_byte(uint8_t data)
{
	m_data = data;
	set_control(false);    // page 12.15, and it is what stops the tape reel
	set_status(true);      // this is what may raise PEDIDO
}

void patinho_io_bus_device::func_w(offs_t offset, uint8_t data)
{
	unsigned const ch = (offset >> 4) & 0x0f;
	if (device_patinho_io_card_interface *const c = m_card[ch])
		c->do_func(offset & 0x0f);
	else
		LOGMASKED(LOG_VACANT, "FNC /%X%X on a vacant channel\n", ch, offset & 0x0f);
}

uint8_t patinho_io_bus_device::data_r(offs_t offset)
{
	unsigned const ch = (offset >> 4) & 0x0f;
	device_patinho_io_card_interface *const c = m_card[ch];
	if (!c)
	{
		// Nothing is known about pull-ups on the real backplane, so zero is a
		// modelling choice rather than a documented behaviour. Worth revisiting
		// against the FIN-8 board schematic (plate VI of Fregni 1972).
		LOGMASKED(LOG_VACANT, "ENTR /%X%X on a vacant channel\n", ch, offset & 0x0f);
		return 0x00;
	}
	return c->data_r(offset & 0x0f);
}

void patinho_io_bus_device::data_w(offs_t offset, uint8_t data)
{
	unsigned const ch = (offset >> 4) & 0x0f;
	device_patinho_io_card_interface *const c = m_card[ch];
	if (!c)
	{
		LOGMASKED(LOG_VACANT, "SAI /%X%X on a vacant channel\n", ch, offset & 0x0f);
		return;
	}
	uint8_t const cmd = offset & 0x0f;
	if (cmd == 0)
	{
		// "Saida do dado do acumulador para o registrador de 8 bits do
		//  dispositivo n.  A seguir, liga CONTROLE e desliga ESTADO (= busy),
		//  automaticamente causando a saida para o meio exterior" -- ch. 13.
		// Register and flip-flops are set before data_w(), so a card that can
		// finish at once may raise ESTADO from inside it.
		c->set_reg(data);
		c->set_status(false);
		c->set_control(true);
	}
	c->data_w(cmd, data);
}

uint8_t patinho_io_bus_device::skip_r(offs_t offset)
{
	unsigned const ch = (offset >> 4) & 0x0f;
	device_patinho_io_card_interface *const c = m_card[ch];
	switch (offset & 0x0f)
	{
	case 0x1:
		// A vacant channel has no ESTADO flip-flop to be on, so it never
		// skips.  Worth a log: "SAL /n1" is normally the head of a busy-wait,
		// and on a vacant channel that loop never ends.
		if (!c)
			LOGMASKED(LOG_VACANT, "SAL /%X1 on a vacant channel: this loop will not end\n", ch);
		return (c && c->status()) ? 1 : 0;
	case 0x2: return (c && c->device_ok()) ? 1 : 0;      // vacant: never skips
	case 0x4: return (!c || !c->irq_request()) ? 1 : 0;  // vacant: always skips
	default:  return (c && c->skip_cond(offset & 0x0f)) ? 1 : 0;
	}
}

/* Page 12.16: the sixteen PEDIDO flip-flops feed one OR gate.  The per-device
   PERMITE/IMPEDE flip-flop is not in this path -- page 12.15 has it "permite
   que o PEDIDO de interrupcao seja ligado", so it gates the set only.  The
   panel INTERRUPCAO button is the second OR input, handled inside the CPU. */
void patinho_io_bus_device::update_int()
{
	bool pending = false;
	for (device_patinho_io_card_interface *const c : m_card)
		pending = pending || (c && c->irq_request());
	if (pending != m_int_state)
	{
		m_int_state = pending;
		m_int_handler(pending ? ASSERT_LINE : CLEAR_LINE);
	}
}

void patinho_io_bus_device::ext_sync_w(int state)
{
	for (device_patinho_io_card_interface *card : m_card)
	{
		if (card)
			card->ext_sync_w(state);
	}
}
