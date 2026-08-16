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

    THE CONNECTOR ON THE BOARD, AND WHAT HANGS FROM IT

    This is the one interface of the machine that the 1977 manual describes as
    having an external cable of its own: the two addresses exist "para
    possibilitar a ligacao entre o Patinho Feio e outros computadores".  So
    the board carries a slot, and the synthesiser is one of the things that
    can be plugged into it -- which is what the executor above is doing.

        ./mame patinho -io6:duplex:port synth      instrument cabled on
        ./mame patinho                             nothing on the cable

    The slot is EMPTY BY DEFAULT, for two reasons.  The board is general
    purpose and spent most of its life with nothing on the other end; and
    channels /6 and /7 hold the same board type, so a default would put a
    second instrument on the slave board, where nothing would ever reach it.

    WHAT IS NOT DECIDED BY ANY DOCUMENT, and is therefore not asserted here:

    - Whether the pair travels on one cable or on two, one serial line per
      board.  The only pin list that survives is the one of the HP 21MX
      interface (chapter 18 of the synthesiser manual: "BITS 0-7" on pin 6,
      "BITS 8-15" on pin 4, "SAIDA CLOCK" on pin 2), and that is another
      computer.  Here the whole 16 bit pair leaves through the connector of
      the board that "MANDA DADOS" was addressed to; the slave board's own
      connector stays empty.
    - Which byte is COMMAND and which is DATA.  Chapter 5 of the synthesiser
      manual has "uma palavra de comando de 8 bits ... e um dado de 8 bits"
      and never says which travels where.  High byte = this board, low byte =
      the neighbour, verified only by the notes coming out right:
      scripts/sintetizador/desacoplamento.sh in the PatinhoFeio repository.

    - The time base lines do NOT come from this board.  They belong to the
      time base generator of channel /4 and reach the same instrument, so they
      cross the backplane to get here; see patinho_io_bus_device::tick_w().

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
