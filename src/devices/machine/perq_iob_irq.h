// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ I/O board soft-interrupt arbiter (Z80 daisy-chain device)

    The PERQ Z80 I/O board has several interrupt sources that are not Z80
    peripheral chips - the PERQ->Z80 FIFO, the uPD765 floppy controller and
    the keyboard - gated onto the Z80's interrupt with fixed IM2 vectors by
    discrete logic.  This device represents that logic as a single Z80
    daisy-chain member so those sources arbitrate (priority and vector)
    alongside the real SIO and CTC instead of contending for the CPU's
    interrupt line.  Highest priority first: FIFO (0x20), floppy (0x24),
    keyboard (0x28).

    Ported from PERQemu by Josh Dersch (GPL-3.0+).

***************************************************************************/

#ifndef MAME_MACHINE_PERQ_IOB_IRQ_H
#define MAME_MACHINE_PERQ_IOB_IRQ_H

#pragma once

#include "machine/z80daisy.h"

class perq_iob_irq_device : public device_t, public device_z80daisy_interface
{
public:
	perq_iob_irq_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	// asserted to the Z80's /INT
	auto int_handler() { return m_int_cb.bind(); }

	// the soft interrupt sources (active-high level inputs), highest priority first
	void fifo_w(int state) { set_source(0, state); }   // PERQ -> Z80 FIFO (vector 0x20)
	void fdc_w(int state)  { set_source(1, state); }   // uPD765 floppy    (vector 0x24)
	void kbd_w(int state)  { set_source(2, state); }   // keyboard         (vector 0x28)

protected:
	virtual void device_start() override ATTR_COLD;

	// device_z80daisy_interface
	virtual int  z80daisy_irq_state() override;
	virtual int  z80daisy_irq_ack() override;
	virtual void z80daisy_irq_reti() override;

private:
	void set_source(int src, int state);

	devcb_write_line m_int_cb;
	u8 m_active = 0;   // bitmask of currently-asserted sources

	static constexpr u8 VECTOR[3] = { 0x20, 0x24, 0x28 };
};

DECLARE_DEVICE_TYPE(PERQ_IOB_IRQ, perq_iob_irq_device)

#endif // MAME_MACHINE_PERQ_IOB_IRQ_H
