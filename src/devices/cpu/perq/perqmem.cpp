// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ main memory and its bookmark-ROM-driven state machine

    Ported from PERQemu Memory/Memory.cs and Memory/MemoryController.cs by
    Josh Dersch (GPL-3.0+); see perqmem.h.

***************************************************************************/

#include "emu.h"
#include "perqmem.h"

#include <algorithm>

perq_memory::perq_memory()
	: m_ram(MEM_WORDS, 0)
{
	std::fill(std::begin(m_bkm), std::end(m_bkm), bookmark{ false, false, false, 0, 0 });
	reset();
}

void perq_memory::load_bookmark_rom(const u8 *src, u32 len)
{
	for (int i = 0; i < 256; i++)
	{
		const u8 b = (u32(i) < len) ? src[i] : 0;
		// bit 6 ("recognize") only fed a debug assertion in PERQemu; not decoded
		m_bkm[i].abort      = (b & 0x80) != 0;
		m_bkm[i].complete   = (b & 0x20) != 0;
		m_bkm[i].valid      = (b & 0x10) != 0;
		m_bkm[i].index      = (b & 0x0c) >> 2;
		m_bkm[i].next_state = b & 0x03;
	}
}

void perq_memory::ctl_reset(controller &c)
{
	c.cur_start = c.pen_start = -1;
	c.cur_cycle = c.pen_cycle = MC_NONE;
	c.cur_book = c.pen_book = 0;
	c.cur_active = c.pen_active = false;
	c.state = c.next_state = ST_IDLE;
	c.bookmark = c.next_bookmark = 0;
	c.address = -1;
	c.index = 0;
	c.wait = false;
	c.valid = false;
}

void perq_memory::reset()
{
	ctl_reset(m_mdi_q);
	ctl_reset(m_mdo_q);
	m_tstate = 0;
	m_mdi = 0;
	m_wait = false;
	m_load_op_file = false;
	for (auto &b : m_op_file)
		b = 0xff;
	// main memory keeps its contents across reset (do not reallocate)
}


//**************************************************************************
//  Board-level API (PERQemu Memory.cs)
//**************************************************************************

void perq_memory::tick(u8 cycle_type)
{
	m_tstate = (m_tstate + 1) & 0x3;

	if (is_fetch(cycle_type))
	{
		ctl_clock(m_mdi_q, cycle_type);
		ctl_clock(m_mdo_q, MC_NONE);
	}
	else
	{
		ctl_clock(m_mdi_q, MC_NONE);
		ctl_clock(m_mdo_q, cycle_type);
	}

	execute_fetch();

	if (mdo_needed())
		m_wait = false;   // a pending store never stalls the CPU
	else
		m_wait = m_mdi_q.wait || m_mdo_q.wait;
}

void perq_memory::tock(u16 data)
{
	if (m_mdo_q.valid)
		write(m_mdo_q.address, data);   // ExecuteStore: commit the store to RAM
}

void perq_memory::request_cycle(int address, u8 cycle_type)
{
	controller &c = is_fetch(cycle_type) ? m_mdi_q : m_mdo_q;
	c.pen_start  = address;
	c.pen_cycle  = cycle_type;
	c.pen_active = true;
	c.pen_book   = c.next_bookmark;
}

void perq_memory::load_op_file()
{
	if (m_mdi_q.cur_cycle == MC_FETCH4 && m_tstate == 1)
		m_load_op_file = true;
}

void perq_memory::execute_fetch()
{
	if (!m_mdi_q.valid)
		return;

	m_mdi = read(m_mdi_q.address);

	if (m_load_op_file)
	{
		const int op_addr = m_mdi_q.index * 2;
		m_op_file[op_addr]     = m_mdi & 0xff;
		m_op_file[op_addr + 1] = (m_mdi & 0xff00) >> 8;
		if (m_mdi_q.index == 3)
			m_load_op_file = false;
	}
}


//**************************************************************************
//  Per-queue controller (PERQemu MemoryController.cs)
//**************************************************************************

const perq_memory::bookmark &perq_memory::ctl_bookmark_entry(int book, mem_state st) const
{
	const int lookup = ((book & 0x0f) << 4) | ((int(st) & 0x3) << 2) | (m_tstate & 0x3);
	return m_bkm[lookup];
}

void perq_memory::ctl_clock(controller &c, u8 next_cycle)
{
	ctl_recognize(c);
	ctl_run_state_machine(c);
	ctl_update_bookmarks(c, next_cycle);
}

void perq_memory::ctl_recognize(controller &c)
{
	if (c.pen_active && !c.cur_active)
	{
		c.cur_start  = c.pen_start;
		c.cur_cycle  = c.pen_cycle;
		c.cur_book   = c.pen_book;
		c.cur_active = c.pen_active;
		c.bookmark   = c.cur_book;

		c.pen_start  = -1;
		c.pen_cycle  = MC_NONE;
		c.pen_book   = 0;
		c.pen_active = false;
	}
}

void perq_memory::ctl_run_state_machine(controller &c)
{
	c.state = c.next_state;

	const bookmark &f = ctl_bookmark_entry(c.bookmark, c.state);
	c.wait = f.abort;
	c.next_state = mem_state(f.next_state);
	c.valid = f.valid;

	if (c.valid)
	{
		c.index = f.index;
		switch (c.cur_cycle)
		{
		case MC_FETCH4R: case MC_FETCH4: case MC_STORE4R: case MC_STORE4:
			c.address = (c.cur_start & 0xffffc) + c.index;   // quad-word aligned
			break;
		case MC_FETCH2: case MC_STORE2:
			c.address = (c.cur_start & 0xffffe) + c.index;   // double-word aligned
			break;
		default:
			c.address = c.cur_start;                          // single word
			break;
		}
	}

	if (f.complete)
	{
		c.cur_start = -1;
		c.cur_cycle = MC_NONE;
		c.cur_book = 0;
		c.cur_active = false;
		c.bookmark = 0;
	}
}

void perq_memory::ctl_update_bookmarks(controller &c, u8 next_cycle)
{
	if (next_cycle == MC_NONE)
	{
		if (!c.cur_active && !c.pen_active)
			c.bookmark = 0;
		return;
	}

	int book = next_cycle;

	// RasterOp overlapped store/fetch bookmarks (dormant until RasterOp lands)
	if (rasterop_enabled())
	{
		if (m_tstate == 0)
		{
			if (next_cycle == MC_STORE4R)     book = 0x2;   // RopStore4R
			else if (next_cycle == MC_STORE4) book = 0x4;   // RopStore4
		}
		else if (m_tstate == 3)
		{
			if (c.cur_cycle == MC_FETCH4R && next_cycle == MC_FETCH4R) { c.bookmark = book = 0x1; }   // RopFetch4R
			else if (c.cur_cycle == MC_FETCH4 && next_cycle == MC_FETCH4) { c.bookmark = book = 0x3; } // RopFetch4
		}
	}

	c.next_bookmark = book;

	// non-RasterOp overlapped fetch: an indirect fetch issued while a fetch
	// is already in flight
	if (is_fetch(next_cycle))
	{
		if (c.cur_cycle == MC_FETCH || c.cur_cycle == MC_FETCH2)
		{
			book = 0x6;             // IndFetch
			c.bookmark = book;
			c.next_bookmark = next_cycle;
		}
		else if (c.cur_cycle == MC_FETCH4 && !rasterop_enabled())
		{
			book = 0x7;             // IndFetch4
			c.bookmark = book;
			c.next_bookmark = next_cycle;
		}
	}

	const bookmark &f = ctl_bookmark_entry(book, c.next_state);

	if (f.complete)
	{
		c.cur_start = -1;
		c.cur_cycle = MC_NONE;
		c.cur_book = 0;
		c.cur_active = false;
	}

	c.wait = f.abort;
	c.next_state = mem_state(f.next_state);
}
