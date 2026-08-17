// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    8 bit duplex interface -- channels /6 and /7

    Chapter 12 of the July 1977 assembler manual: "Os 2 enderecos 8-Bit duplex
    sao para ligacao entre o PF e outros computadores".  General purpose
    boards, older than the synthesiser; Guido Stolfi's executor pairs them so
    that one transfer carries a command byte and a data byte at once.

    "FNC /7A" ("MODO ACOPLADO") chains the two: afterwards the data
    instructions all name channel /6 and a command bit picks the register
    (executor tape FITA#011.txt):

        SAI /62   "SAI NO CANAL 6 ( 8 BITS )"
        SAI /63   "SAI NO CANAL 7 ( 8 BITS )"   -- names /6, lands in /7
        SAI /64   "MANDA DADOS."
        SAL /61   "WAIT - FOR - FLAG."
        FNC /68   "MODO SERIE PARA CANAL 6"
        FNC /78   "MODO SERIE PARA CANAL 7"
        FNC /7A   "MODO ACOPLADO"

    Crossing to the neighbour's register is what patinho_io_bus_device::card()
    is for.  The slot on this board is empty by default: /6 and /7 hold the
    same board type, so a default would also fit an instrument to the slave.

    Inferred, not documented: the whole 16 bit pair leaves through the
    connector of the board addressed by "MANDA DADOS" (no pin list survives
    for this interface); and high byte = this board, low byte = the neighbour,
    chapter 5 of the synthesiser manual having "uma palavra de comando de 8
    bits ... e um dado de 8 bits" without saying which travels where.

    The time base lines come from the generator of channel /4, not from this
    board; see patinho_io_bus_device::tick_w().

***************************************************************************/
#ifndef MAME_BUS_PATINHO_DUPLEX_H
#define MAME_BUS_PATINHO_DUPLEX_H

#pragma once

#include "iobus.h"

#include "bus/epusp/epusp.h"


// ======================> patinho_duplex_device

class patinho_duplex_device : public device_t,
		public device_patinho_io_card_interface
{
public:
	patinho_duplex_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// How long "MANDA DADOS" keeps the interface busy.  NOT a documented
	// figure: see the comment over data_w() in duplex.cpp.
	void set_tx_time(attotime t) { m_tx_time = t; }

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;

	// device_patinho_io_card_interface implementation
	virtual void func_w(uint8_t cmd) override;
	virtual void data_w(uint8_t cmd, uint8_t data) override;
	virtual void card_reset() override;

	// The tick of whichever board is generating one, passed down the cable so
	// that what is plugged in can record it.  It arrives over the backplane
	// because it starts on another board; see iobus.h.
	virtual void tick_w(int state) override;

private:
	TIMER_CALLBACK_MEMBER(tx_done);
	patinho_duplex_device *partner() const;
	void ext_sync_in(int state);

	required_device<epusp_synth_port_device> m_port;
	emu_timer *m_tx_timer = nullptr;
	attotime m_tx_time;

	bool m_serial_mode = false;   // "MODO SERIE",   FNC /n8
	bool m_coupled = false;       // "MODO ACOPLADO", FNC /nA
};

DECLARE_DEVICE_TYPE(PATINHO_DUPLEX, patinho_duplex_device)

#endif // MAME_BUS_PATINHO_DUPLEX_H
