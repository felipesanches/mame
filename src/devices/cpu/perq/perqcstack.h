// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ microsequencer call stack and extended (14-bit) PC/S registers

    The hardware PC and S registers are 14 bits, but the Am2910 sequencer
    only manages 12 internally; the top 2 bits are external glue (the
    "2-bit kluge").  The call stack likewise has the 2910's 5-deep 12-bit
    stack plus a parallel external stack for the top 2 bits.

    Configured for the 16K CPU (PERQ 1A): Value() combines lo | (hi<<12).

    Ported from PERQemu CPU/ExtendedRegister.cs and CPU/CallStack.cs by
    Josh Dersch (GPL-3.0+); see perq.h.

***************************************************************************/

#ifndef MAME_CPU_PERQ_PERQCSTACK_H
#define MAME_CPU_PERQ_PERQCSTACK_H

#pragma once

// 14-bit PC/S register.  lo() is the 2910's 12-bit register; hi() the 2-bit
// external latch.  Sequential execution increments only lo() (no carry into
// hi); only explicit Value() loads change the high bits.
class perq_extended_register
{
public:
	void reset() { m_lo = 0; m_hi = 0; }

	u16  lo() const    { return m_lo; }
	u16  hi() const    { return m_hi; }
	u16  value() const { return u16(m_lo | (m_hi << 12)); }

	void set_lo(u16 v)    { m_lo = v & 0xfff; }
	void set_hi(u16 v)    { m_hi = v & 0x3; }
	void set_value(u16 v) { m_lo = v & 0xfff; m_hi = (v >> 12) & 0x3; }

	void inc_lo() { m_lo = (m_lo + 1) & 0xfff; }   // wraps inside the 4K page
	void dec_lo() { m_lo = (m_lo - 1) & 0xfff; }

private:
	u16 m_lo = 0;
	u16 m_hi = 0;
};


// Am2910 5-deep call stack with the parallel 2-bit external stack.  Pointers
// saturate (do not wrap): on overflow the top is overwritten, on underflow
// the bottom is re-read.  Index 0 is the empty sentinel; 1..5 are usable.
class perq_callstack
{
public:
	void clear()             // zero the contents, leave the pointers
	{
		for (int i = 0; i < STACK_SIZE; i++) { m_lo[i] = 0; m_hi[i] = 0; }
	}

	void reset()             // pointers to 0, leave the contents
	{
		m_sp_lo = 0;
		m_sp_hi = 0;
	}

	void push_lo(u16 address)
	{
		if (m_sp_lo < STACK_LIMIT) m_sp_lo++;
		m_lo[m_sp_lo] = address & 0xfff;
	}

	u16 pop_lo()
	{
		const u16 address = m_lo[m_sp_lo];   // read before decrement
		if (m_sp_lo > 0) m_sp_lo--;
		return address;
	}

	u16 top_lo() const { return m_lo[m_sp_lo]; }

	void push_full(u16 address)
	{
		if (m_sp_lo < STACK_LIMIT) m_sp_lo++;
		if (m_sp_hi < STACK_LIMIT) m_sp_hi++;
		m_lo[m_sp_lo] = address & 0xfff;
		m_hi[m_sp_hi] = address & 0x3000;    // kept in place at bits 12-13
	}

	u16 pop_full()
	{
		const u16 address = u16(m_lo[m_sp_lo] | m_hi[m_sp_hi]);   // read before decrement
		if (m_sp_lo > 0) m_sp_lo--;
		if (m_sp_hi > 0) m_sp_hi--;
		return address;
	}

	u16 top_full() const { return u16(m_lo[m_sp_lo] | m_hi[m_sp_hi]); }

private:
	static constexpr int STACK_SIZE  = 6;
	static constexpr int STACK_LIMIT = 5;

	u16 m_lo[STACK_SIZE] = { 0 };
	u16 m_hi[STACK_SIZE] = { 0 };
	int m_sp_lo = 0;
	int m_sp_hi = 0;
};

#endif // MAME_CPU_PERQ_PERQCSTACK_H
