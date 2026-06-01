// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ I/O board soft-interrupt arbiter (Z80 daisy-chain device)

    See perq_iob_irq.h.  Ported from PERQemu by Josh Dersch (GPL-3.0+).

***************************************************************************/

#include "emu.h"
#include "machine/perq_iob_irq.h"

DEFINE_DEVICE_TYPE(PERQ_IOB_IRQ, perq_iob_irq_device, "perq_iob_irq", "PERQ IOB interrupt arbiter")

perq_iob_irq_device::perq_iob_irq_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, PERQ_IOB_IRQ, tag, owner, clock)
	, device_z80daisy_interface(mconfig, *this)
	, m_int_cb(*this)
{
}

void perq_iob_irq_device::device_start()
{
	save_item(NAME(m_active));
}

void perq_iob_irq_device::set_source(int src, int state)
{
	const u8 mask = 1 << src;
	if (state)
		m_active |= mask;
	else
		m_active &= ~mask;

	// flag the Z80 whenever any soft source is asserted; the daisy chain then
	// arbitrates priority and supplies the vector during the acknowledge
	m_int_cb(m_active ? ASSERT_LINE : CLEAR_LINE);
}

int perq_iob_irq_device::z80daisy_irq_state()
{
	return m_active ? Z80_DAISY_INT : 0;
}

int perq_iob_irq_device::z80daisy_irq_ack()
{
	// the highest-priority asserted source (lowest index) wins the acknowledge
	for (int i = 0; i < 3; i++)
		if (BIT(m_active, i))
			return VECTOR[i];
	return 0xff;
}

void perq_iob_irq_device::z80daisy_irq_reti()
{
	// the sources are level-driven (each interrupt service routine clears its
	// own condition), so there is no in-service state to unwind here
}
