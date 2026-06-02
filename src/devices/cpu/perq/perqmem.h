// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ main memory and its state machine

    The PERQ memory board manages all of its timing with a small state
    machine driven by a 256-byte "bookmark" ROM (bkm16emu.rom): two
    MemoryController FIFOs (one for fetches feeding MDI, one for stores
    draining MDO) advance every micro-cycle off the CPU's T-states, and
    the ROM tells each whether to abort (stall the CPU), when the fetched
    word is valid, when a store is needed, and which word of a quad/pair
    is on the bus.  The same scheme lets fetches and stores overlap (for
    RasterOp).

    Configured for the PERQ 1A (16K CPU); main memory is 512KW (1MB), the
    PERQemu !TWO_MEG default.  RasterOp is added in a later phase
    (rasterop_enabled() is false), so
    the RasterOp bookmark cases are present but dormant.

    Ported from PERQemu Memory/Memory.cs and Memory/MemoryController.cs by
    Josh Dersch (GPL-3.0+); see perq.h.

***************************************************************************/

#ifndef MAME_CPU_PERQ_PERQMEM_H
#define MAME_CPU_PERQ_PERQMEM_H

#pragma once

class perq_memory
{
public:
	// MemoryCycle: the microword SF value when F==1 && SF>=8
	enum
	{
		MC_NONE = 0x0,
		MC_FETCH4R = 0x8, MC_STORE4R = 0x9,
		MC_FETCH4  = 0xa, MC_STORE4  = 0xb,
		MC_FETCH2  = 0xc, MC_STORE2  = 0xd,
		MC_FETCH   = 0xe, MC_STORE   = 0xf
	};

	perq_memory();

	void reset();

	// decode the 256-byte bookmark ROM (bkm16emu.rom) into m_bkm[]
	void load_bookmark_rom(const u8 *src, u32 len);

	// immediate word access (used by the state machine and by the video /
	// disk subsystems, which read main memory directly)
	u16  read(offs_t word_addr) const      { return m_ram[word_addr & MEM_MASK]; }
	void write(offs_t word_addr, u16 data) { m_ram[word_addr & MEM_MASK] = data; }

	// per-cycle state-machine API, called from the CPU's execute loop
	void tick(u8 cycle_type);                     // top of cycle (before the abort test)
	void tock(u16 data);                          // store half-cycle
	void request_cycle(int address, u8 cycle_type); // issue a Fetch/Store
	void load_op_file();                          // start an OpFile refill (LoadOp)

	// the RasterOp unit reports when it is active so the overlapped Fetch/Store
	// bookmark cases in the memory state machine become live
	void set_rasterop_enabled(bool e) { m_rasterop_enabled = e; }

	// status read by the CPU
	u16  mdi() const          { return m_mdi; }
	int  mdi_address() const  { return m_mdi_q.address; }   // PERQemu MemoryBoard.MADR
	int  mdi_index() const    { return m_mdi_q.index; }     // PERQemu MemoryBoard.MIndex
	bool mdi_valid() const    { return m_mdi_q.valid; }
	bool mdo_needed() const   { return m_mdo_q.valid; }
	bool wait() const         { return m_wait; }
	int  tstate() const       { return m_tstate; }
	u8   op_file(int i) const { return m_op_file[i & 0xf]; }

private:
	enum mem_state : u8 { ST_IDLE = 0, ST_WAIT_T3 = 1, ST_WAIT_T2 = 2, ST_RUNNING = 3 };

	static bool is_fetch(u8 c) { return c == MC_FETCH || c == MC_FETCH2 || c == MC_FETCH4 || c == MC_FETCH4R; }

	// a decoded bookmark-ROM byte
	struct bookmark { bool abort, complete, valid; u8 index; u8 next_state; };

	// one Fetch or one Store FIFO (PERQemu MemoryController)
	struct controller
	{
		// the current and pending MemoryRequest
		int  cur_start, pen_start;
		u8   cur_cycle, pen_cycle;
		int  cur_book,  pen_book;
		bool cur_active, pen_active;

		mem_state state, next_state;
		int  bookmark, next_bookmark;
		int  address;
		u8   index;
		bool wait, valid;
	};

	void ctl_reset(controller &c);
	void ctl_clock(controller &c, u8 next_cycle);
	void ctl_recognize(controller &c);
	void ctl_run_state_machine(controller &c);
	void ctl_update_bookmarks(controller &c, u8 next_cycle);
	const bookmark &ctl_bookmark_entry(int book, mem_state st) const;

	void execute_fetch();
	bool rasterop_enabled() const { return m_rasterop_enabled; }

	static constexpr u32 MEM_WORDS = 0x80000;   // 512KW (1MB)
	static constexpr u32 MEM_MASK  = 0x7ffff;

	bookmark   m_bkm[256];
	controller m_mdi_q;   // fetches -> MDI
	controller m_mdo_q;   // stores  -> MDO

	int  m_tstate;        // shared 0..3 T-state
	u16  m_mdi;           // last fetched word
	bool m_wait;          // board-level CPU wait
	bool m_load_op_file;  // an OpFile refill is in progress
	bool m_rasterop_enabled = false;  // the RasterOp datapath is active
	u8   m_op_file[16];   // the opcode file (q-code bytes)

	std::vector<u16> m_ram;
};

#endif // MAME_CPU_PERQ_PERQMEM_H
