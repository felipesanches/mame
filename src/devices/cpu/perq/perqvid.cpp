// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ video controller

    Ported from PERQemu Display/VideoController.cs by Josh Dersch (GPL-3.0+);
    see perqvid.h.

***************************************************************************/

#include "emu.h"
#include "perq.h"

perq_video::perq_video(perq_cpu_device &cpu)
	: m_cpu(cpu)
{
}

void perq_video::reset()
{
	m_display_addr = 0;
	m_cursor_addr = 0;
	m_cursor_x = 0;
	m_cursor_y = 0;
	m_cursor_func = 2;   // AND cursor
	m_video_status = 0;
	m_line_counter = 0;
	m_line_counter_init = 0;
	m_line_count_overflow = false;
	m_scanline = 0;
}

u16 perq_video::io_read(u8 port)
{
	switch (port)
	{
	case 0x65:   // CRT signals
		{
			// bit 7 LandscapeDisplay (always set = portrait), bit 4 line-counter
			// overflow (latched until 0xE0 reload), bit 1 vertical sync
			const bool in_vblank = m_scanline >= HEIGHT;
			return 0x80
				| (m_line_count_overflow ? 0x10 : 0)
				| (in_vblank             ? 0x02 : 0);
		}

	default:     // 0x66/0x67 address parity - not modelled
		return 0;
	}
}

void perq_video::io_write(u8 port, u16 data)
{
	switch (port)
	{
	case 0xe0:   // line counter: bits 6:0 hold the 2's complement of N scanlines
		m_line_counter_init = 128 - (data & 0x7f);
		m_line_counter = m_line_counter_init;
		m_line_count_overflow = false;
		m_cpu.clear_interrupt(perq_cpu_device::IRQ_LINECOUNTER);
		break;

	case 0xe1:   // display address (>>4 form, for >= 512KB boards)
		m_display_addr = u32(data) << 4;
		break;

	case 0xe2:   // cursor address (same decode)
		m_cursor_addr = u32(data) << 4;
		break;

	case 0xe3:   // video control
		m_cursor_func  = (data & 0xe000) >> 13;   // 15:13 cursor map function
		m_video_status = data & 0x1f00;           // 12:8 control bits
		if (m_video_status & 0x0100)              // enable cursor: restart cursor Y
			m_cursor_y = 0;
		if (m_video_status & 0x0200)              // enable vertical sync: restart at top
			m_scanline = 0;
		break;

	case 0xe4:   // cursor X position (8-pixel granularity)
		m_cursor_x = 240 - (data & 0xff);
		break;

	default:
		break;
	}
}

void perq_video::line_tick()
{
	if (++m_scanline >= VTOTAL)
		m_scanline = 0;

	if (m_line_counter > 0)
		m_line_counter--;

	// raise the LineCounter interrupt once when the counter expires; the overflow
	// flag latches (independent of the interrupt enable) until 0xE0 is reloaded
	if (m_line_counter == 0 && m_line_counter_init > 0 && !m_line_count_overflow)
	{
		if (parity_interrupts_enabled())
			m_cpu.raise_interrupt(perq_cpu_device::IRQ_LINECOUNTER);
		m_line_count_overflow = true;
	}
}

u32 perq_video::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	const bool invert = display_inverted();

	for (int y = cliprect.top(); y <= cliprect.bottom(); y++)
	{
		const offs_t base = m_display_addr + offs_t(y) * WORDS_PER_LINE;

		for (int wx = 0; wx < WORDS_PER_LINE; wx++)
		{
			u16 word = m_cpu.mem_read(base + wx);
			if (invert)
				word = ~word;

			for (int bit = 0; bit < 16; bit++)
				bitmap.pix(y, wx * 16 + bit) = BIT(word, 15 - bit);
		}
	}

	return 0;
}
