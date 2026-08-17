// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Printing terminals of the Patinho Feio

    Chapter 12 of the July 1977 assembler manual lists two of them:

        /A   DECWRITER (Digital Equipment Corp.)
        /B   TTY (Teleprinter da TELETYPE Corp.)

    The DECwriter is "uma maquina de escrever com 72 colunas"; "o TTY inclui
    leitora e perfuradora de fita de papel" -- the Teletype's own reader, not
    the optical HP-2737-A of channel /E.  These live under src/mame/usp/
    rather than bus/patinho/ because they wrap teleprinter_device, which is
    in src/mame/shared/ and not in a device library.

***************************************************************************/
#ifndef MAME_USP_PATINHO_TERMINALS_H
#define MAME_USP_PATINHO_TERMINALS_H

#pragma once

#include "bus/patinho/iobus.h"

#include "teleprinter.h"


// ======================> patinho_terminal_device

class patinho_terminal_device : public device_t,
		public device_patinho_io_card_interface
{
protected:
	patinho_terminal_device(const machine_config &mconfig, device_type type, const char *tag,
			device_t *owner, uint32_t clock, attotime char_time);

	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;
	virtual void device_start() override ATTR_COLD;
	virtual void card_reset() override;

	// device_patinho_io_card_interface implementation
	virtual void data_w(uint8_t cmd, uint8_t data) override;

	// Time a character takes to print. The status flip-flop stays busy until
	// then, which is what a program's "SAL /n1" wait loop is waiting for.
	virtual attotime print_time(uint8_t data) const { return m_char_time; }

	void keyboard_input(uint8_t data);

	required_device<teleprinter_device> m_teleprinter;

private:
	TIMER_CALLBACK_MEMBER(print_done);

	emu_timer *m_print_timer = nullptr;
	attotime m_char_time;
};


// ======================> patinho_decwriter_device

class patinho_decwriter_device : public patinho_terminal_device
{
public:
	patinho_decwriter_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

protected:
	// A carriage return takes the head all the way back, and that is slower
	// than printing a character.
	virtual attotime print_time(uint8_t data) const override;
};


// ======================> patinho_tty_device

class patinho_tty_device : public patinho_terminal_device
{
public:
	patinho_tty_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
};

DECLARE_DEVICE_TYPE(PATINHO_DECWRITER, patinho_decwriter_device)
DECLARE_DEVICE_TYPE(PATINHO_TTY,       patinho_tty_device)

#endif // MAME_USP_PATINHO_TERMINALS_H
