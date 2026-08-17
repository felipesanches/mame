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
	, m_port(*this, "port")
	// 8 bit-times at the 9600 Hz shift clock of page 29's receiving interface.
	// See the long note in func_w(), case 0x4.
	, m_tx_time(attotime::from_hz(9600) * 8)
{
}

/* THE EXTERNAL CONNECTOR OF THE BOARD.

   Chapter 12 of the July 1977 manual is the authority for it existing at all:
   the two duplex addresses are there "para possibilitar a ligacao entre o
   Patinho Feio e outros computadores".  Empty by default -- see duplex.h.

   The instrument that plugs in gets three wires: the (command, data) pair
   this board sends, the time base of whichever board is generating one, and a
   way back for the time base it recovers from a tape.  Only the first of the
   three is this board's own; the other two are the /4 board's, and they are
   here because the cable to the instrument carried all of them to the same
   connector (chapter 15 of the synthesiser manual, pins 1 to 3 of the
   "INTF. REC.").  Where the harness forked on the computer side is not
   documented, so it is modelled as two backplane lines: iobus.h says why. */
void patinho_duplex_device::device_add_mconfig(machine_config &config)
{
	EPUSP_SYNTH_PORT(config, m_port, epusp_synth_devices, nullptr);
	m_port->set_display_name("External connector");
	m_port->sync_handler().set(FUNC(patinho_duplex_device::ext_sync_in));
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

		   HOW LONG?  Read off the receiving end's schematic, page 29 of the
		   synthesiser manual (PDF 38).  The chain is drawn explicitly:

		     2.457600 MHz crystal -> 7493 (/16) -> 7493 (/16) -> node "Cp"

		   and "9600 Hz" is written on the wire leaving that node.  The
		   arithmetic agrees exactly, 2457600 / 256 = 9600, so the label is
		   confirmed and not merely legible.  Cp is the shift clock (pin 10)
		   of the 8300 pairs that reassemble both bytes.

		   COMANDO and DADO arrive on two SEPARATE serial lines, each with its
		   own edge-detect front end and its own pair of 8300s (4 bits each),
		   and both pairs are clocked by Cp.  The two bytes therefore shift in
		   together, not one after the other: a (command, data) pair costs
		   8 bit-times, 8 / 9600 = 833.3 us.

		   WHAT IS INFERRED, AND WHAT THAT COSTS.  833 us is a FLOOR, not a
		   reading of the transfer time.  The sheet's show-through carries the
		   mirrored title "FORMAS DE ONDA DA INTERFACE RECEPTORA", so a
		   waveform page exists and would give the framing -- start pulse, gap
		   between pairs -- but it has not been located in the scan.  Any
		   framing only adds.  And the Patinho-side board is not this board;
		   it must agree on the bit rate for the link to work at all, which is
		   the argument for using this number here, but its own turnaround
		   could add more.

		   The upper bound is still the measured one: the executor polls
		   "SAL /61" inside the interrupt handler, so a transfer longer than
		   the gap between ticks costs ticks.  scripts/sintetizador/
		   orcamento_tx.py in the PatinhoFeio repository measures the worst
		   burst in the surviving corpus at 12 events in one t_min, which
		   leaves 30 ms / 12 = 2.50 ms per transfer before the executor starts
		   losing time it cannot recover.

		   So the true value lies in [833 us, 2.50 ms], and we take the floor,
		   which is the only end of that interval a document supports.  This
		   replaces a 640 us placeholder that was inside the bound but stood on
		   nothing.  set_tx_time() still exists so a sweep can vary it. */
		uint8_t const second = partner() ? partner()->reg() : 0x00;
		uint16_t const pair = (uint16_t(reg()) << 8) | second;

		LOGMASKED(LOG_TX, "MANDA DADOS: /%02X /%02X\n", reg(), second);
		m_port->command_w(pair);   // out of the connector, if anything is on it

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

// Down the cable: the tick another board is generating.  Harmless when the
// connector is empty, which is the usual case.
void patinho_duplex_device::tick_w(int state)
{
	m_port->tick_w(state);
}

/* And back up it: the 1 kHz the instrument recovered from a tape, on its way
   to whichever board asked for an external time base -- "AVISA QUE O
   SINTETIZADOR MANDA", in the executor's words, and that board is not this
   one.  This card does not know which channel it is, so it puts the line on
   the backplane and lets the interested board pick it up. */
void patinho_duplex_device::ext_sync_in(int state)
{
	bus().ext_sync_w(state);
}
