// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    8 bit duplex interface -- channels /6 and /7

    Chapter 12 of the July 1977 assembler manual lists two of these:

        /6   8-Bit duplex          E/S
        /7   8-Bit duplex          E/S

    and says "Os 2 enderecos 8-Bit duplex sao para ligacao entre o PF e outros
    computadores".  They predate the synthesiser: they are general purpose
    boards that Guido Stolfi's executor puts to a use of its own, pairing them
    so that one transfer carries a command byte and a data byte at once.

    COUPLED MODE.  The two boards are chained by "FNC /7A", which the executor
    calls "MODO ACOPLADO".  After that, every data instruction is addressed to
    channel /6 and the second register is reached by a command bit rather than
    by its own channel number:

        SAI /62   "SAI NO CANAL 6 ( 8 BITS )"     FITA#011.txt line 150
        SAI /63   "SAI NO CANAL 7 ( 8 BITS )"     line 156   <-- note the 7
        SAI /64   "MANDA DADOS."                  line 158
        SAL /61   "WAIT - FOR - FLAG."            line 160
        FNC /68   "MODO SERIE PARA CANAL 6"       line 396
        FNC /78   "MODO SERIE PARA CANAL 7"       line 398
        FNC /7A   "MODO ACOPLADO"                 line 400

    "SAI /63" is the surprising one: the instruction names channel /6 and the
    byte lands in the register of channel /7.  That is the whole point of the
    coupling, and it is why this card needs to reach its neighbour -- which is
    what patinho_io_bus_device::card() is for.

***************************************************************************/
#ifndef MAME_BUS_PATINHO_DUPLEX_H
#define MAME_BUS_PATINHO_DUPLEX_H

#pragma once

#include "iobus.h"


// ======================> patinho_duplex_device

class patinho_duplex_device : public device_t,
		public device_patinho_io_card_interface
{
public:
	patinho_duplex_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// One transfer, as it leaves the pair of boards: the high byte is the
	// register of this channel and the low byte the register of the next one.
	// For the synthesiser those are (command, data) -- chapter 5 of the
	// synthesiser manual, "Comandos do Computador".
	auto tx_handler() { return m_tx_handler.bind(); }

	// How long "MANDA DADOS" keeps the interface busy.  NOT a documented
	// figure: see the comment over data_w() in duplex.cpp.
	void set_tx_time(attotime t) { m_tx_time = t; }

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// device_patinho_io_card_interface implementation
	virtual void func_w(uint8_t cmd) override;
	virtual void data_w(uint8_t cmd, uint8_t data) override;
	virtual void card_reset() override;

private:
	TIMER_CALLBACK_MEMBER(tx_done);
	patinho_duplex_device *partner() const;

	devcb_write16 m_tx_handler;
	emu_timer *m_tx_timer = nullptr;
	attotime m_tx_time;

	bool m_serial_mode = false;   // "MODO SERIE",   FNC /n8
	bool m_coupled = false;       // "MODO ACOPLADO", FNC /nA
};

DECLARE_DEVICE_TYPE(PATINHO_DUPLEX, patinho_duplex_device)

#endif // MAME_BUS_PATINHO_DUPLEX_H
