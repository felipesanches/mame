// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ Shugart SA4000 hard-disk controller

    Ported from PERQemu by Josh Dersch (GPL-3.0+); see perqdsk.h.

***************************************************************************/

#include "emu.h"
#include "perq.h"

perq_shugart::perq_shugart(perq_cpu_device &cpu)
	: m_cpu(cpu)
	, m_status(0)
{
}

void perq_shugart::reset()
{
	m_status = 0x80;   // unit ready
	m_cpu.clear_interrupt(perq_cpu_device::IRQ_HARDDISK);
}

u16 perq_shugart::io_read(u8 port)
{
	// 0x40 = Shugart status register
	return (port == 0x40) ? m_status : 0;
}

void perq_shugart::io_write(u8 port, u16 data)
{
	// register loads, seeks and DMA transfers are modelled in a later phase
}
