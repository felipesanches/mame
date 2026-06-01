// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ main memory, memory state machine and RasterOp

    Phase 0 provides plain word-addressed RAM (up to 1 MW = 2 MB on a
    PERQ 1A).  The cycle-accurate memory state machine and the RasterOp
    pipeline (ported from PERQemu Memory/Memory.cs, MemoryController.cs
    and RasterOp.cs) are added in a later phase.

    Ported from PERQemu by Josh Dersch (GPL-3.0+); see perq.h.

***************************************************************************/

#ifndef MAME_CPU_PERQ_PERQMEM_H
#define MAME_CPU_PERQ_PERQMEM_H

#pragma once

class perq_memory
{
public:
	perq_memory();

	void reset();

	u16  read(offs_t word_addr) const      { return m_ram[word_addr & (RAM_WORDS - 1)]; }
	void write(offs_t word_addr, u16 data) { m_ram[word_addr & (RAM_WORDS - 1)] = data; }

private:
	static constexpr u32 RAM_WORDS = 0x100000;  // 1 MW (2 MB), PERQ 1A maximum

	std::vector<u16> m_ram;
};

#endif // MAME_CPU_PERQ_PERQMEM_H
