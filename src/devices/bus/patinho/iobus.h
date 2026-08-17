// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    I/O bus of the Patinho Feio (Escola Politecnica da USP, 1972)

    Chapter 12 of the July 1977 assembler manual: "Ao Patinho Feio podem ser
    ligados 16 equipamentos de E/S (no maximo), com enderecos de 0 a F
    (hexadecimal)".  Every one of them, the front panel aside, sits behind an
    interface board carrying an 8 bit register and four flip-flops: CONTROLE,
    ESTADO (busy/ready), PEDIDO (interrupt request) and PERMITE/IMPEDE
    (interrupt enable).  The manual is explicit that all five live in the
    interface and not in the processor, so that is where this code puts them:
    in the card, not in the CPU.

    Channel /0 is the front panel key register, which is wired in and input
    only, so the sockets are /1 to /F.

***************************************************************************/
#ifndef MAME_BUS_PATINHO_IOBUS_H
#define MAME_BUS_PATINHO_IOBUS_H

#pragma once

class patinho_io_bus_device;
class patinho_io_slot_device;
class device_patinho_io_card_interface;


// ======================> patinho_io_bus_device

class patinho_io_bus_device : public device_t
{
	friend class patinho_io_slot_device;
	friend class device_patinho_io_card_interface;

public:
	static constexpr unsigned CHANNELS = 16;

	patinho_io_bus_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// The machine has a single interrupt level (assembler manual, chapter 11)
	// and no priority logic at all: priority is the order in which the
	// interrupt discovery routine tests "SAL /n4".
	auto int_handler() { return m_int_handler.bind(); }

	/* The cable that leaves a peripheral for something that is not on the bus.
	   The synthesiser is the case that exists: it is not on a channel of its
	   own -- it hangs off the pair of duplex boards, and every transfer is a
	   (command, data) pair from chapter 5 of its manual.

	   It lives on the bus rather than on the card because the card sits in a
	   user-configurable slot, and a machine_config cannot bind to a device
	   that may or may not be plugged in. High byte = the register of the
	   channel that sent it, low byte = the register of its neighbour. */
	auto tx_handler() { return m_tx_handler.bind(); }
	void tx_w(uint16_t pair) { m_tx_handler(pair); }

	/* The external time base line of the backplane.  The tape recorder's
	   recovered 1 kHz arrives through the synthesiser and is offered to every
	   card; only the one that asked for an external clock does anything with
	   it.  Broadcast rather than point-to-point because the bus has no
	   business knowing which channel holds a time base generator. */
	void ext_sync_w(int state);

	/* The other direction: a card's own time base, offered to whatever is
	   recording it.  The time base generator pulses this on every internal
	   tick so the 1 kHz reaches the tape while voice 1 is being laid down. */
	auto tick_handler() { return m_tick_handler.bind(); }
	void tick_w(int state) { m_tick_handler(state); }

	// ---- CPU side --------------------------------------------------------
	// In all four entry points the offset is (channel << 4) | command, where
	// the command is the low nibble of the second word of the I/O
	// instruction.  Both the type field and the command field are documented
	// in Fregni (1972), figure 4.4.
	void func_w(offs_t offset, uint8_t data);   // FNC  /nc
	uint8_t data_r(offs_t offset);              // ENTR /nc
	void data_w(offs_t offset, uint8_t data);   // SAI  /nc
	uint8_t skip_r(offs_t offset);              // SAL  /nc, non-zero = skip

	// The PREPARACAO push-button clears every flip-flop in the machine, and
	// after this refactor most of them are in the cards.  Modelled after
	// a2bus_device::reset_bus().
	void reset_bus();

	// ---- card side -------------------------------------------------------
	// Used by cards that are one half of a two board peripheral, the way the
	// second 8 bit duplex interface is chained to the first one by "FNC /7A",
	// and by the duplex card when it looks for the synthesiser time base.
	device_patinho_io_card_interface *card(unsigned channel) const
	{ return (channel < CHANNELS) ? m_card[channel] : nullptr; }

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	void add_card(unsigned channel, device_patinho_io_card_interface &card);
	void update_int();

	devcb_write_line m_int_handler;
	devcb_write16 m_tx_handler;
	devcb_write_line m_tick_handler;
	device_patinho_io_card_interface *m_card[CHANNELS];
	bool m_int_state;
};


// ======================> patinho_io_slot_device

class patinho_io_slot_device : public device_t,
		public device_single_card_slot_interface<device_patinho_io_card_interface>
{
public:
	template <typename T, typename U>
	patinho_io_slot_device(const machine_config &mconfig, const char *tag, device_t *owner,
			unsigned channel, T &&bus_tag, U &&opts, const char *dflt)
		: patinho_io_slot_device(mconfig, tag, owner, 0U)
	{
		m_channel = channel;
		m_bus.set_tag(std::forward<T>(bus_tag));
		set_options(std::forward<U>(opts), dflt, false);
	}
	patinho_io_slot_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// The channel number is passed in rather than scraped off the last
	// character of the tag, the way bus/nubus and bus/a2bus do it: the tag is
	// a user interface string and must stay free to change.
	unsigned channel() const { return m_channel; }

protected:
	virtual void device_resolve_objects() override ATTR_COLD;
	virtual void device_start() override ATTR_COLD;

private:
	required_device<patinho_io_bus_device> m_bus;
	unsigned m_channel = ~0U;   // the template constructor always overwrites it
};


// ======================> device_patinho_io_card_interface

class device_patinho_io_card_interface : public device_interface
{
	friend class patinho_io_bus_device;
	friend class patinho_io_slot_device;

public:
	virtual ~device_patinho_io_card_interface();

protected:
	device_patinho_io_card_interface(const machine_config &mconfig, device_t &device);

	virtual void interface_pre_start() override;
	virtual void interface_post_start() override;

	// ---- overridables ----------------------------------------------------

	// EVERY "FNC /nc" reaches the card, including the seven the bus handles by
	// itself, and after the standard effect has been applied.  The synthesiser
	// time base reuses commands 1 and 2 with meanings of its own ("AVISA QUE A
	// INTERFACE MANDA" / "AVISA QUE O SINTETIZADOR MANDA"), so a card that
	// only saw the leftovers could not be written at all.  The base
	// implementation logs anything outside {0,1,2,4,5,6,7}.
	virtual void func_w(uint8_t cmd);

	// The CONTROLE flip-flop changed.  Turning it on is how a program tells a
	// peripheral to start working ("FNC /n6", "SAI /n0").
	virtual void control_w(int state) { }

	// The ESTADO flip-flop changed.  Symmetrical to control_w() and required
	// for the same reason: on channel /4 this flip-flop is not busy/ready at
	// all, it selects which end of the cable generates the clock.
	virtual void status_w(int state) { }

	// ENTR: the contents of the 8 bit register go to the accumulator.  Note
	// that the bus does NOT touch CONTROLE here; the interface already dropped
	// it when it delivered the byte (page 12.15).
	virtual uint8_t data_r(uint8_t cmd) { return m_data; }

	// SAI: for cmd == 0 the accumulator has already been copied into the 8 bit
	// register, ESTADO is busy and CONTROLE is on.  For any other command the
	// bus touched nothing: chapter 13 defines "SAI /n0" and nothing else, and
	// the duplex interface uses "SAI /62", "SAI /63" and "SAI /64" on one and
	// the same channel to load two registers before triggering.
	virtual void data_w(uint8_t cmd, uint8_t data) { }

	// SAL /n2, "o dispositivo n estiver O.K.".  Chapter 13 says only the
	// printer (/5), the punch (/8) and the tape reader (/E) have this line,
	// and that "nos outros dispositivos, nao salta" -- hence the default.
	virtual bool device_ok() const { return false; }

	// SAL with a command other than the three standard ones, e.g. the
	// "SALTA canal=4, func=3" of the synthesiser time base.
	virtual bool skip_cond(uint8_t cmd) const { return false; }

	// Page 12.15: on a standard interface board "ESTADO ligado liga o PEDIDO
	// automaticamente se for permitido".  Return false on a board that does
	// not work that way -- the synthesiser time base, where ESTADO selects the
	// clock source and the request comes from the tick.
	virtual bool irq_from_status() const { return true; }

	// The backplane's external time base. Ignored by cards that do not use it.
	virtual void ext_sync_w(int state) { }

	// PREPARACAO.  A card that overrides it must call the base first.
	virtual void card_reset();

	// ---- services --------------------------------------------------------
	unsigned channel() const { return m_channel; }
	patinho_io_bus_device &bus() const { return *m_bus; }

	bool control() const { return m_control; }
	bool status() const { return m_status; }
	uint8_t reg() const { return m_data; }
	void set_reg(uint8_t data) { m_data = data; }

	// The only ways to move the two flip-flops: both always notify the card.
	void set_control(bool state);
	void set_status(bool ready);

	// Drives the PEDIDO latch directly.  For cards that override
	// irq_from_status() this is the only way their request moves.
	void set_irq_request(bool state);
	bool irq_enable() const { return m_irq_enable; }

	// A byte came in from the outside world.  Page 12.15: "Quando o
	// dispositivo acaba a transferencia do dado, desliga o CONTROLE e liga o
	// ESTADO automaticamente."
	void receive_byte(uint8_t data);

private:
	void set_bus(patinho_io_bus_device &bus, unsigned channel)
	{ m_bus = &bus; m_channel = channel; }

	void do_func(uint8_t cmd);          // called by the bus
	void update_irq_request();
	bool irq_request() const { return m_irq_request; }

	patinho_io_bus_device *m_bus = nullptr;
	unsigned m_channel = ~0U;
	uint8_t m_data;      // the 8 bit register
	bool m_control;      // flip-flop de CONTROLE
	bool m_status;       // flip-flop de ESTADO, true = "ready"
	bool m_irq_request;  // flip-flop de PEDIDO de interrupcao
	bool m_irq_enable;   // flip-flop de PERMITE/IMPEDE
	bool m_irq_latch;    // the PEDIDO latch proper; the effective request
	                     // is (m_irq_latch OR the level-sensitive set input)
};

DECLARE_DEVICE_TYPE(PATINHO_IO_BUS,  patinho_io_bus_device)
DECLARE_DEVICE_TYPE(PATINHO_IO_SLOT, patinho_io_slot_device)

#endif // MAME_BUS_PATINHO_IOBUS_H
