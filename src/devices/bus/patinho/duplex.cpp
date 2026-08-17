// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    8 bit duplex interface -- channels /6 and /7

    See duplex.h for where every function name comes from.

***************************************************************************/

#include "emu.h"
#include "duplex.h"

#define LOG_TX  (1U << 1)

#define VERBOSE (0)
#include "logmacro.h"

DEFINE_DEVICE_TYPE(PATINHO_DUPLEX, patinho_duplex_device, "patinho_duplex", "Patinho Feio 8-bit duplex interface")

patinho_duplex_device::patinho_duplex_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, PATINHO_DUPLEX, tag, owner, clock)
	, device_patinho_io_card_interface(mconfig, *this)
	, m_tx_handler(*this)
	, m_tx_time(attotime::from_usec(640))
{
}

void patinho_duplex_device::device_start()
{
	m_tx_timer = timer_alloc(FUNC(patinho_duplex_device::tx_done), this);

	save_item(NAME(m_serial_mode));
	save_item(NAME(m_coupled));
}

void patinho_duplex_device::device_reset()
{
	m_tx_timer->reset();
	m_serial_mode = false;
	m_coupled = false;
}

void patinho_duplex_device::card_reset()
{
	device_patinho_io_card_interface::card_reset();

	m_tx_timer->reset();
	m_serial_mode = false;
	m_coupled = false;
}

/* The other half of the pair.  The 1977 manual lists the two boards at /6 and
   /7, adjacent, and "FNC /7A" is what chains them, so the neighbour is the
   next channel up.  Returns nullptr when the user has left that slot empty or
   filled it with something else, which is a configuration the machine allows:
   the boards are general purpose and were sold as a pair only by convention. */
patinho_duplex_device *patinho_duplex_device::partner() const
{
	return dynamic_cast<patinho_duplex_device *>(bus().card(channel() + 1));
}

void patinho_duplex_device::func_w(uint8_t cmd)
{
	switch (cmd)
	{
	case 0x8:
		// "MODO SERIE PARA CANAL 6" / "... CANAL 7".  Recorded, but nothing in
		// the emulation depends on it yet: the difference between serial and
		// parallel is in the cable, and both ends of that cable are modelled.
		m_serial_mode = true;
		break;

	case 0xA:
		// "MODO ACOPLADO": from here on the neighbour's register is written
		// through this channel, with "SAI /n3".
		m_coupled = true;
		break;

	default:
		device_patinho_io_card_interface::func_w(cmd);
		break;
	}
}

void patinho_duplex_device::data_w(uint8_t cmd, uint8_t data)
{
	switch (cmd)
	{
	case 0x2:
		// "SAI NO CANAL 6 ( 8 BITS )": this board's own register.
		set_reg(data);
		break;

	case 0x3:
		/* "SAI NO CANAL 7 ( 8 BITS )" -- addressed to channel /6 and landing
		   in the register of /7.  That is the coupling, and it is the only
		   reason this card has to know its neighbour. */
		if (patinho_duplex_device *const p = partner())
		{
			p->set_reg(data);
		}
		else
		{
			logerror("SAI /%X3 wants the register of channel /%X, "
					"which has no duplex board in it\n", channel(), channel() + 1);
		}
		break;

	case 0x4:
	{
		/* "MANDA DADOS.": send the pair and go busy until it has gone.

		   HOW LONG?  No document in the project gives the rate of these
		   boards -- not chapter 12 of the assembler manual, which only says
		   they are "para ligacao entre o PF e outros computadores", and not
		   chapter 8 of the synthesiser manual.  What IS known is an upper
		   bound, and it was measured rather than guessed: the executor polls
		   "SAL /61" inside the interrupt handler, so a transfer longer than
		   the gap between ticks costs ticks.  scripts/sintetizador/
		   orcamento_tx.py in the PatinhoFeio repository measures the worst
		   burst in the surviving corpus at 12 events in one t_min, which
		   leaves 30 ms / 12 = 2.50 ms per transfer before the executor starts
		   losing time it cannot recover.

		   640 us is a placeholder chosen to sit well inside that bound, not a
		   reading.  set_tx_time() exists so the eventual sweep can vary it
		   without touching this file. */
		uint8_t const second = partner() ? partner()->reg() : 0x00;
		uint16_t const pair = (uint16_t(reg()) << 8) | second;

		LOGMASKED(LOG_TX, "MANDA DADOS: /%02X /%02X\n", reg(), second);
		m_tx_handler(pair);

		set_status(false);              // busy: "SAL /61" waits on this
		m_tx_timer->adjust(m_tx_time);
		break;
	}

	default:
		logerror("unknown SAI /%X%X\n", channel(), cmd);
		break;
	}
}

TIMER_CALLBACK_MEMBER(patinho_duplex_device::tx_done)
{
	// The interface is free again, which is what "SAL /61" is waiting to see.
	set_status(true);
}
