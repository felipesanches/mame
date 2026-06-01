// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ main memory, memory state machine and RasterOp

    Ported from PERQemu by Josh Dersch (GPL-3.0+); see perqmem.h.

***************************************************************************/

#include "emu.h"
#include "perqmem.h"

#include <algorithm>

perq_memory::perq_memory()
	: m_ram(RAM_WORDS, 0)
{
}

void perq_memory::reset()
{
	std::fill(m_ram.begin(), m_ram.end(), 0);
}
