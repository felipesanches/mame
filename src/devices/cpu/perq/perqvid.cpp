// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ video controller

    Ported from PERQemu by Josh Dersch (GPL-3.0+); see perqvid.h.

***************************************************************************/

#include "emu.h"
#include "perq.h"

perq_video::perq_video(perq_cpu_device &cpu)
	: m_cpu(cpu)
	, m_display_addr(0)
{
}

void perq_video::reset()
{
	m_display_addr = 0;
}

u16 perq_video::io_read(u8 port)
{
	// 0x65 CRT signals, 0x66/0x67 parity - stubbed until video timing is modelled
	return 0;
}

void perq_video::io_write(u8 port, u16 data)
{
	switch (port)
	{
	case 0xe1:  // load display address (>>4 form, for >= 512KB boards)
		m_display_addr = u32(data) << 4;
		break;

	default:    // line counter, cursor address, video control, cursor X
		break;
	}
}

u32 perq_video::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	for (int y = cliprect.top(); y <= cliprect.bottom(); y++)
	{
		const offs_t base = m_display_addr + offs_t(y) * WORDS_PER_LINE;

		for (int wx = 0; wx < WORDS_PER_LINE; wx++)
		{
			const u16 word = m_cpu.mem_read(base + wx);

			for (int bit = 0; bit < 16; bit++)
				bitmap.pix(y, wx * 16 + bit) = BIT(word, 15 - bit);
		}
	}

	return 0;
}
