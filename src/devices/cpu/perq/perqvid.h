// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ video controller

    Portrait 768x1024 1bpp display.  Each scanline is 48 words fetched from
    main memory at displayAddress + line*48.  A programmable line counter
    raises the LineCounter interrupt after a set number of scanlines (the
    microcode's display driver runs off it), and the CRT-signals register
    reports vertical sync and the line-counter-overflow latch.  A hardware
    cursor is composited over the picture.

    Ported from PERQemu Display/VideoController.cs by Josh Dersch (GPL-3.0+);
    see perq.h.

***************************************************************************/

#ifndef MAME_CPU_PERQ_PERQVID_H
#define MAME_CPU_PERQ_PERQVID_H

#pragma once

class perq_cpu_device;

class perq_video
{
public:
	static constexpr int WIDTH  = 768;
	static constexpr int HEIGHT = 1024;
	static constexpr int WORDS_PER_LINE = WIDTH / 16;   // 48
	static constexpr int BYTES_PER_LINE = WIDTH / 8;    // 96
	static constexpr int VTOTAL = 1067;                 // 1024 visible + 43 blanking
	static constexpr int CPU_CLOCKS_PER_LINE = 92;      // 70 visible + 22 hblank microcycles

	perq_video(perq_cpu_device &cpu);

	void reset();

	// CPU I/O bus: read 0x65-0x67 (CRT status/parity), write 0xE0-0xE4
	u16  io_read(u8 port);
	void io_write(u8 port, u16 data);

	// advance one scanline (driven by a timer on the CPU device): count the
	// line counter down and raise the LineCounter interrupt when it expires
	void line_tick();

	u32 screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);

private:
	friend class perq_cpu_device;   // for save-state of the members below

	// the line-counter interrupt is gated by the video-control "disable parity
	// interrupt" bit being clear; some cursor functions invert the whole picture
	bool parity_interrupts_enabled() const { return (m_video_status & 0x0800) == 0; }
	bool display_inverted() const
	{
		return m_cursor_func == 0 || m_cursor_func == 3 || m_cursor_func == 4 || m_cursor_func == 6;
	}

	perq_cpu_device &m_cpu;

	u32 m_display_addr = 0;            // 0xE1: word address of the bitmap
	u32 m_cursor_addr = 0;            // 0xE2: word address of the cursor bitmap
	int m_cursor_x = 0;               // 0xE4: 240 - (data & 0xff), in bytes
	int m_cursor_y = 0;               // reset on 0xE3 enable-cursor
	u8  m_cursor_func = 2;            // 0xE3 bits 15:13 (AND cursor at reset)
	u16 m_video_status = 0;           // 0xE3 bits 12:8 latched

	int  m_line_counter = 0;          // current down-count
	int  m_line_counter_init = 0;     // 128 - (data & 0x7f)
	bool m_line_count_overflow = false;   // latched until 0xE0 is reloaded
	int  m_scanline = 0;              // 0..VTOTAL-1
};

#endif // MAME_CPU_PERQ_PERQVID_H
