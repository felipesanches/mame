// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    Three Rivers PERQ microengine (CPU device)

    The PERQ is a microcoded bit-slice machine: an AM2910 microsequencer
    drives a 48-bit horizontal microword out of a 16K (PERQ 1A) writable
    control store; 74S181 ALUs implement a 20-bit ALU over a 256-entry
    register file.  A bootstrap ROM overlays the low control store at
    reset.  Memory, the RasterOp pipeline, the video controller and the
    Shugart hard-disk controller are all clocked off the microengine's
    T-states, so (following MAME's Xerox Alto precedent) they live inside
    this CPU device.  The Z80 I/O board is wired up separately in the driver.

    This is a port of PERQemu by Josh Dersch (GPL-3.0+):
        https://github.com/skeezicsb/PERQemu
        https://github.com/jdersch/PERQemu

***************************************************************************/

#ifndef MAME_CPU_PERQ_PERQ_H
#define MAME_CPU_PERQ_PERQ_H

#pragma once

#include "perqalu.h"
#include "perqshift.h"
#include "perqcstack.h"
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

	// front-panel DDS diagnostic display: fired (with the current 0..999 value)
	// each time the boot/OS microcode resets the expression stack
	auto dds_update_cb() { return m_dds_cb.bind(); }

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

	// Shugart hard-disk hookup: attach the loaded image and let the controller
	// arm its status (busy) and index timers
	void set_hard_disk(perq_harddisk_image_device *hd) { m_disk.set_image(hd); }
	void disk_arm_busy_timer(const attotime &delay)    { m_disk_busy_timer->adjust(delay); }
	void disk_arm_index_timer(const attotime &delay)   { m_disk_index_timer->adjust(delay); }

protected:
	// device_t
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual const tiny_rom_entry *device_rom_region() const override ATTR_COLD;

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
	// a decoded 48-bit microinstruction (decoded per fetch; next_address is
	// mutable because the MA:=Shift special function rewrites it in place)
	struct microinstruction
	{
		u64 ucode;
		u16 pc;

		u8  x, y, a, b, w, h, alu, f, sf, z, cnd, jmp;

		u16 not_z;
		u16 long_constant;
		bool is_io_input;
		u8  io_port;
		u16 vector_dispatch_address;
		bool is_special_function;
		u16 bmux_input;
		bool want_mdi;
		u8  memory_request;     // MemoryCycle
		u16 next_address;
	};

	// address map for the control store (AS_PROGRAM)
	void ucode_map(address_map &map) ATTR_COLD;

	// microengine
	u64  fetch_microword(u16 addr);
	microinstruction decode(u16 addr);
	u32  get_amux(microinstruction &uop);
	void do_writeback(const microinstruction &uop);
	void do_muldiv_alu_op(u32 amux, u32 bmux, u16 mq, u8 op);
	void dispatch_function(microinstruction &uop);
	void dispatch_jump(const microinstruction &uop);
	bool condition_satisfied(u8 cnd);
	int  interrupt_priority();
	u16  microstate_register();

	// expression stack + DDS
	void stack_reset();
	void stack_push(u32 value);
	void stack_pop();
	void increment_dds();

	// writable control store
	void write_control_store(int which, u16 data);
	static u64 unscramble_control_store_word(u64 current, int which, u16 data);

	// boot ROM
	void load_boot_rom();

	// Shugart controller timers
	TIMER_CALLBACK_MEMBER(disk_busy_done);
	TIMER_CALLBACK_MEMBER(disk_index_edge);

	// helpers
	u8   bpc() const           { return m_bpc & 0xf; }
	bool op_file_empty() const  { return (bpc() & 0x8) != 0; }

	// main-CPU I/O bus dispatch (ports decoded to the on-board peripherals,
	// or forwarded to the driver via the callbacks)
	u16  iobus_read(u8 port);
	void iobus_write(u8 port, u16 data);

	// AS_PROGRAM = control store (48-bit microwords)
	address_space_config m_ucode_config;
	address_space *m_ucode;

	devcb_read16  m_iobus_in;
	devcb_write16 m_iobus_out;
	devcb_write16 m_dds_cb;

	// on-board subsystems (clocked off the microengine)
	perq_memory  m_mem_state;
	perq_video   m_video;
	perq_shugart m_disk;
	emu_timer   *m_disk_busy_timer = nullptr;
	emu_timer   *m_disk_index_timer = nullptr;

	// datapath
	perq_alu       m_alu;
	perq_alu::regs m_old_alu;     // previous cycle's ALU flags (conditions read these)
	perq_shifter   m_shifter;
	perq_shifter   m_mq_shifter;  // 16K hardware multiply/divide shifter

	// sequencer
	perq_extended_register m_pc;  // 14-bit microcode PC
	perq_extended_register m_s;   // 14-bit S register
	perq_callstack         m_cstack;

	// register file and expression stack
	u32 m_r[256];
	u32 m_estack[16];
	int m_stack_pointer;

	// misc CPU state
	u8   m_bpc;            // byte program counter (low 4 bits; bit3 = opfile empty)
	int  m_dds;            // diagnostic display counter (front-panel)
	u16  m_iod;            // last word read from the I/O bus
	u16  m_victim;         // victim latch (0xffff == unset)
	u8   m_register_base;  // 16K register-base for X/Y < 0x40
	u16  m_mq;             // multiplier/quotient register
	bool m_mq_enabled;
	int  m_last_bmux;
	bool m_increment_bpc;
	bool m_wcs_hold;       // one-cycle stall after a WCS write
	bool m_rom_enabled;    // boot ROM overlays 0x000-0x1ff while true
	u8   m_muldiv_inst = 0; // current hardware multiply/divide command (WidRasterOp <7:6>)

	u64  m_rom[512];       // boot microcode (overlaid over the low control store)

	u8   m_interrupt;      // hardware interrupt latch (see IRQ_* above)
	int  m_icount;

	// debugger-visible staging copies of the 14-bit PC/S (updated each cycle)
	u16  m_genpc = 0;
	u16  m_gens  = 0;
};

DECLARE_DEVICE_TYPE(PERQ, perq_cpu_device)

#endif // MAME_CPU_PERQ_PERQ_H
