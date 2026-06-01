// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ shifter

    Ported from PERQemu CPU/Shifter.cs by Josh Dersch (GPL-3.0+); see perqshift.h.

***************************************************************************/

#include "emu.h"
#include "perqshift.h"

void perq_shifter::build_table()
{
	// the command byte is inverted before the two nibbles are extracted
	for (int i = 0; i < 0x100; i++)
	{
		const int low  = (~i) & 0x0f;
		const int high = ((~i) & 0xf0) >> 4;

		entry &e = m_table[i];
		e.mask = 0;

		if (low == 0xf)                                                       // LeftShift
		{
			e.command = LEFT_SHIFT;
			e.amount = u8(high);
		}
		else if ((0xf - low) == high)                                         // RightShift
		{
			e.command = RIGHT_SHIFT;
			e.amount = u8(high);
		}
		else if ((low == 0xd || low == 0xe) && (high >= 0x8 && high <= 0xf))  // Rotate
		{
			e.command = ROTATE;
			e.amount = u8((high & 0x7) | (low == 0xd ? 0x0 : 0x8));
		}
		else                                                                  // Field (extract)
		{
			e.command = FIELD;
			e.amount = u8(high);
			e.mask = u16(0x1ffff >> (0x10 - low));   // contiguous low mask, width low+1
		}
	}
}

void perq_shifter::shift(int low, int high)
{
	switch (m_params.command)
	{
	case LEFT_SHIFT:
		m_output = u16(u32(low) << m_params.amount);
		break;

	case RIGHT_SHIFT:   // logical, not arithmetic
		m_output = u16((u32(low) & 0xffff) >> m_params.amount);
		break;

	case ROTATE:
	{
		const u32 d = (u32(high & 0xffff) << 16) | u32(low & 0xffff);
		m_output = u16(d >> m_params.amount);
		break;
	}

	case FIELD:
	{
		const u32 d = (u32(high & 0xffff) << 16) | u32(low & 0xffff);
		m_output = u16((d >> m_params.amount) & m_params.mask);
		break;
	}
	}
}
