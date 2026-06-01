// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    Three Rivers PERQ microengine (CPU device)

    The PERQ is a microcoded bit-slice machine: an AM2910 microsequencer
    drives a 48-bit horizontal microword out of a 4K (PERQ 1) or 16K
    (PERQ 1A) writable control store; 74S181 ALUs implement a 20-bit ALU
    over a 256-entry register file.  A bootstrap ROM overlays the control
    store at reset.  Memory, the RasterOp pipeline, the video controller
    and the Shugart hard-disk controller are all clocked off the
    microengine's T-states, so (following MAME's Xerox Alto precedent)
    they live inside this CPU device.  The Z80 I/O board is wired up
    separately in the driver.

    This is a port of PERQemu by Josh Dersch (GPL-3.0+):
        https://github.com/skeezicsb/PERQemu
        https://github.com/jdersch/PERQemu

***************************************************************************/

#ifndef MAME_CPU_PERQ_PERQ_H
#define MAME_CPU_PERQ_PERQ_H

#pragma once

#include "perqmem.h"
#include "perqvid.h"
#include "perqdsk.h"

class perq_cpu_device : public cpu_device
{
public:
	// construction/destruction
	perq_cpu_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);

	// I/O bus callbacks: the microengine's "IOB function" issues IN/OUT on
	// an 8-bit port; ports not handled internally (video, disk) are routed
	// to the driver, where the Z80 I/O board and its FIFOs live.
	auto iobus_in_cb()  { return m_iobus_in.bind(); }
	auto iobus_out_cb() { return m_iobus_out.bind(); }

	// portrait 768x1024 1bpp display, served from main memory
	u32 screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);

	// hardware interrupt lines raised by the I/O board / video / disk
	// (matches PERQemu's InterruptType bit assignments)
	enum
	{
		IRQ_Z80_DATA_OUT = 0,   // 0x01 Z80 has data for the PERQ
		IRQ_Y            = 1,   // 0x02
		IRQ_HARDDISK     = 2,   // 0x04
		IRQ_NETWORK      = 3,   // 0x08
		IRQ_Z80_DATA_IN  = 4,   // 0x10 PERQ data consumed by the Z80
		IRQ_LINECOUNTER  = 5,   // 0x20 video line counter expired
		IRQ_X            = 6,   // 0x40
		IRQ_PARITY       = 7    // 0x80 memory parity error
	};

	// raise/clear an interrupt latch bit (used by the subsystems and driver)
	void raise_interrupt(int which)  { m_interrupt |= (1 << which); }
	void clear_interrupt(int which)  { m_interrupt &= ~(1 << which); }

	// main memory access helpers for the video/disk subsystems
	u16  mem_read(offs_t word_addr)            { return m_mem_state.read(word_addr); }
	void mem_write(offs_t word_addr, u16 data) { m_mem_state.write(word_addr, data); }

protected:
	// device_t
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// device_execute_interface
	virtual u32 execute_min_cycles() const noexcept override { return 1; }
	virtual u32 execute_max_cycles() const noexcept override { return 1; }
	virtual void execute_run() override;
	virtual void execute_set_input(int inputnum, int state) override;

	// device_memory_interface
	virtual space_config_vector memory_space_config() const override;

	// device_disasm_interface
	virtual std::unique_ptr<util::disasm_interface> create_disassembler() override;

private:
	// AS_PROGRAM = control store (48-bit microwords).  Main memory is a
	// plain word array owned by perq_memory (matching both the Alto CPU
	// device and PERQemu's own array-backed memory model).
	void ucode_map(address_map &map) ATTR_COLD;

	// main-CPU I/O bus dispatch (ports decoded to the on-board peripherals,
	// or forwarded to the driver via the callbacks)
	u16  iobus_read(u8 port);
	void iobus_write(u8 port, u16 data);

	address_space_config m_ucode_config;

	devcb_read16  m_iobus_in;
	devcb_write16 m_iobus_out;

	// on-board subsystems (clocked off the microengine)
	perq_memory  m_mem_state;
	perq_video   m_video;
	perq_shugart m_disk;

	// microengine state (a minimal subset for now; the full datapath is
	// ported in a later phase)
	u16 m_pc;           // microcode program counter (12/14 bits)
	u8  m_interrupt;    // hardware interrupt latch (see IRQ_* above)
	int m_icount;
};

DECLARE_DEVICE_TYPE(PERQ, perq_cpu_device)

#endif // MAME_CPU_PERQ_PERQ_H
