// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ Shugart SA4000 hard-disk controller

    The controller sits on the main-CPU I/O bus (status at 0x40; command,
    cyl/head/sector and DMA buffer addresses at 0xC1-0xD9), DMAs sectors
    in and out of main memory and raises the HardDisk interrupt on
    completion.  Phase 0 only models the status/idle handshake; the seek
    state machine and DMA transfers (PERQemu IO/HardDisk/ShugartController.cs)
    are added in a later phase.

    Ported from PERQemu by Josh Dersch (GPL-3.0+); see perq.h.

***************************************************************************/

#ifndef MAME_CPU_PERQ_PERQDSK_H
#define MAME_CPU_PERQ_PERQDSK_H

#pragma once

class perq_cpu_device;

class perq_shugart
{
public:
	perq_shugart(perq_cpu_device &cpu);

	void reset();

	// CPU I/O bus: read 0x40 (status), write 0xC2/0xC8-0xCB/0xD0-0xD1/0xD8-0xD9
	u16  io_read(u8 port);
	void io_write(u8 port, u16 data);

private:
	perq_cpu_device &m_cpu;

	u8 m_status;   // bit7 unit ready, bit6 seek complete, bit4 track 0, ...
};

#endif // MAME_CPU_PERQ_PERQDSK_H
