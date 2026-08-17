// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    8 bit duplex interface -- channels /6 and /7

    See duplex.h for the documentary basis.

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

/* Chapter 12 of the July 1977 manual: the two duplex addresses exist "para
   possibilitar a ligacao entre o Patinho Feio e outros computadores".  Empty
   by default -- see duplex.h.  The cable to the instrument carries three lines
   to one connector (chapter 15 of the synthesiser manual, pins 1 to 3 of the
   "INTF. REC."): this board's (command, data) pair, plus the time base and the
   tape-recovered sync of the /4 board, which are modelled as backplane lines
   because where the harness forked is not documented. */
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

	/* Not registered: m_data and the flip-flops are saved for every board by
	   interface_post_start() in iobus.cpp; m_tx_timer is saved by the
	   scheduler and needs no re-arming, as its callback only sets m_status,
	   which is itself saved; m_tx_time and m_port are configuration. */
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

/* The 1977 manual lists the pair at /6 and /7, adjacent, and "FNC /7A" chains
   them, so the neighbour is the next channel up.  Returns nullptr when that
   slot is empty or holds another card, which is a permitted configuration. */
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
		/* "MANDA DADOS.": send the pair and go busy until it has gone.  Timing
		   from the receiving end's schematic, page 29 of the synthesiser
		   manual: a 2.457600 MHz crystal divided by two 7493s gives node "Cp",
		   labelled 9600 Hz (2457600 / 256 = 9600), the shift clock of the 8300
		   pairs reassembling both bytes.  COMANDO and DADO arrive on two
		   separate serial lines clocked together, so a (command, data) pair
		   costs 8 bit-times, 8 / 9600 = 833.3 us.

		   Inference: this is a floor, giving a range of [833 us, 2.50 ms].
		   Framing would come from the waveform sheet "FORMAS DE ONDA DA
		   INTERFACE RECEPTORA", not located in the scan, and the Patinho-side
		   board may add turnaround; above 2.50 ms the executor loses interrupt
		   ticks while polling "SAL /61".  set_tx_time() allows a sweep. */
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

/* Back up the cable: the 1 kHz time base the instrument recovered from a tape,
   for whichever board asked for an external one -- "AVISA QUE O SINTETIZADOR
   MANDA" in the executor -- which is not this one.  This card cannot tell
   which channel wants it, so it puts the line on the backplane. */
void patinho_duplex_device::ext_sync_in(int state)
{
	bus().ext_sync_w(state);
}
