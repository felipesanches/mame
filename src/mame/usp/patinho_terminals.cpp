// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Printing terminals of the Patinho Feio

    See patinho_terminals.h for the documentary basis.

***************************************************************************/

#include "emu.h"
#include "patinho_terminals.h"

/* LOG_CHAR sends each printed character to the error log with the address of
   the instruction that sent it; at 10 characters per second it is not a
   flood. */
#define LOG_CHAR  (1U << 1)

#define VERBOSE (0)
#include "logmacro.h"

DEFINE_DEVICE_TYPE(PATINHO_DECWRITER, patinho_decwriter_device, "patinho_decwriter", "DECwriter printing terminal")
DEFINE_DEVICE_TYPE(PATINHO_TTY,       patinho_tty_device,       "patinho_tty",       "Teletype ASR33 printing terminal")


patinho_terminal_device::patinho_terminal_device(const machine_config &mconfig, device_type type, const char *tag,
		device_t *owner, uint32_t clock, attotime char_time)
	: device_t(mconfig, type, tag, owner, clock)
	, device_patinho_io_card_interface(mconfig, *this)
	, m_teleprinter(*this, "terminal")
	, m_char_time(char_time)
{
}

void patinho_terminal_device::device_add_mconfig(machine_config &config)
{
	TELEPRINTER(config, m_teleprinter);
	m_teleprinter->set_keyboard_callback(FUNC(patinho_terminal_device::keyboard_input));
}

void patinho_terminal_device::device_start()
{
	m_print_timer = timer_alloc(FUNC(patinho_terminal_device::print_done), this);
}

void patinho_terminal_device::card_reset()
{
	device_patinho_io_card_interface::card_reset();

	m_print_timer->reset();
}

void patinho_terminal_device::data_w(uint8_t cmd, uint8_t data)
{
	if (cmd != 0)
	{
		logerror("unknown SAI command /%X\n", cmd);
		return;
	}

	LOGMASKED(LOG_CHAR, "imprime /%02X %s\n", data,
			(data >= 0x20 && data < 0x7F) ? util::string_format("'%c'", char(data)) : std::string());

	m_teleprinter->write(data);

	// The bus already set STATUS busy when it handed the byte over. It stays
	// busy until the character has actually been printed: a program waiting on
	// "SAL /n1" is waiting for exactly this.
	m_print_timer->adjust(print_time(data));
}

TIMER_CALLBACK_MEMBER(patinho_terminal_device::print_done)
{
	set_status(true);
}

void patinho_terminal_device::keyboard_input(uint8_t data)
{
	// The data arrives inverted (two's complement). This was worked out from a
	// comment in the source listing of the HEXAM program; it is not clear
	// whether every I/O device complements the data or whether it is a habit
	// of the terminals alone.
	receive_byte(~data);
}


patinho_decwriter_device::patinho_decwriter_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: patinho_terminal_device(mconfig, PATINHO_DECWRITER, tag, owner, clock, attotime::from_hz(10))
{
}

attotime patinho_decwriter_device::print_time(uint8_t data) const
{
	if (data == 0x0D)
		return attotime::from_msec(700); // carriage return

	return attotime::from_hz(10); // 10 characters per second
}


patinho_tty_device::patinho_tty_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: patinho_terminal_device(mconfig, PATINHO_TTY, tag, owner, clock, attotime::from_hz(10))
{
	// Teletype ASR33, 10 characters per second (doc 03, chapter 1)
}
