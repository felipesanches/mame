// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ video controller

    Portrait 768x1024 1bpp display.  Scanlines are fetched from main
    memory (48 words per line) and the line counter raises a video
    interrupt at a programmed interval.  Phase 0 renders directly from
    the current display address; cycle-accurate timing, the cursor and
    the line-counter interrupt (PERQemu Display/VideoController.cs) are
    added in a later phase.

    Ported from PERQemu by Josh Dersch (GPL-3.0+); see perq.h.

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

	perq_video(perq_cpu_device &cpu);

	void reset();

	// CPU I/O bus: read 0x65-0x67 (CRT status/parity), write 0xE0-0xE4
	u16  io_read(u8 port);
	void io_write(u8 port, u16 data);

	u32 screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);

private:
	perq_cpu_device &m_cpu;

	u32 m_display_addr;   // word address of the bitmap in main memory
};

#endif // MAME_CPU_PERQ_PERQVID_H
