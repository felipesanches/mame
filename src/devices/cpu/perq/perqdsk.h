// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ Shugart SA4000 hard-disk controller

    The controller sits on the main-CPU I/O bus (status at 0x40; command,
    cyl/head/sector, file serial, block and DMA buffer/header addresses at
    0xC1-0xD9).  A command makes the controller go busy, then (after a short
    delay) done while raising the HardDisk interrupt; a read DMAs the sector
    header and data straight into main memory.  Seeks are stepped one
    cylinder at a time by the step bit in the command register.

    Ported from PERQemu IO/HardDisk/ShugartController.cs by Josh Dersch
    (GPL-3.0+); see perq.h.

***************************************************************************/

#ifndef MAME_CPU_PERQ_PERQDSK_H
#define MAME_CPU_PERQ_PERQDSK_H

#pragma once

class perq_cpu_device;
class perq_harddisk_image_device;

class perq_shugart
{
public:
	perq_shugart(perq_cpu_device &cpu);

	void reset();
	void set_image(perq_harddisk_image_device *hd) { m_image = hd; }

	// CPU I/O bus: read 0x40 (status); write 0xC1-0xD9 (command and registers)
	u16  io_read(u8 port);
	void io_write(u8 port, u16 data);

	// hard-disk single-step strobe from the Z80 I/O board (port 0xD8 there)
	void do_single_seek();
	void do_multiple_seek(int count);

	// driven by the CPU device's timers
	void on_busy_done();
	void on_index_edge();

private:
	friend class perq_cpu_device;   // for save-state of the members below

	enum class command : u8
	{
		IDLE = 0, READ_CHK = 1, READ_DIAG = 2, WRITE_CHK = 3,
		WRITE_FIRST = 4, FORMAT = 5, SEEK = 6, RESET = 7
	};
	// controller-status low 3 bits, and the seek state machine (stored as plain
	// u8 members so they save/restore without an ALLOW_SAVE_TYPE declaration)
	static constexpr u8 ST_DONE = 0, ST_BUSY = 7;
	static constexpr u8 SK_WAIT_STEP_SET = 0, SK_WAIT_STEP_RELEASE = 1, SK_SEEK_COMPLETE = 2;

	void load_command_register(u16 data);
	void load_cyl_sec_register(u16 v);
	u16  disk_status() const;
	void clock_seek();
	void seek_to(int cyl);
	void read_block();
	void write_block(bool write_header);
	void format_block();
	void set_busy_state();
	void reset_flags();
	int  disk_cylinders() const;

	// the PERQ stores DMA buffer addresses with the low word XNOR'd against 0x3ff
	static u32 unfrob(u32 v) { return (0x3ff & v) | (~0x3ff & ~v); }

	perq_cpu_device &m_cpu;
	perq_harddisk_image_device *m_image = nullptr;

	u8 m_controller_status = ST_DONE;   // ST_DONE / ST_BUSY (low 3 bits of the status word)
	u8 m_track_zero = 1, m_drive_fault = 0, m_seek_complete = 0, m_unit_ready = 1, m_index = 0;

	int m_cylinder = 0, m_phys_cylinder = 0, m_head = 0, m_sector = 0;
	u32 m_serial_low = 0, m_serial_high = 0, m_block_number = 0;
	u32 m_header_addr_low = 0, m_header_addr_high = 0, m_data_buffer_low = 0, m_data_buffer_high = 0;

	u8  m_seek_state = SK_WAIT_STEP_SET;
	u16 m_seek_data = 0;
};

#endif // MAME_CPU_PERQ_PERQDSK_H
