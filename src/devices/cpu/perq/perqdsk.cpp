// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ Shugart SA4000 hard-disk controller

    Ported from PERQemu IO/HardDisk/ShugartController.cs by Josh Dersch
    (GPL-3.0+); see perqdsk.h.

***************************************************************************/

#include "emu.h"
#include "perq.h"
#include "imagedev/perq_hdc.h"

#include <algorithm>

perq_shugart::perq_shugart(perq_cpu_device &cpu)
	: m_cpu(cpu)
{
	// state is initialised by reset(), called from the CPU device's device_reset
}

void perq_shugart::reset_flags()
{
	m_controller_status = ST_DONE;
	m_drive_fault   = 0;
	m_seek_complete = 0;
	m_unit_ready    = 1;

	m_serial_high = m_serial_low = 0;
	m_block_number = 0;
	m_header_addr_low = m_header_addr_high = 0;
	m_data_buffer_low = m_data_buffer_high = 0;
}

void perq_shugart::reset()
{
	reset_flags();

	m_cylinder = 0;
	m_phys_cylinder = 0;
	m_track_zero = 1;
	m_head = 0;
	m_sector = 0;
	m_index = 0;
	m_seek_state = SK_WAIT_STEP_SET;
	m_seek_data = 0;

	m_cpu.clear_interrupt(perq_cpu_device::IRQ_HARDDISK);
}

int perq_shugart::disk_cylinders() const
{
	return (m_image && m_image->loaded()) ? int(m_image->cylinders()) : 202;   // Shugart24 fallback
}


//**************************************************************************
//  I/O bus
//**************************************************************************

u16 perq_shugart::io_read(u8 port)
{
	// 0x40 = status register (reading it does NOT clear a pending interrupt)
	return (port == 0x40) ? disk_status() : 0;
}

void perq_shugart::io_write(u8 port, u16 data)
{
	switch (port)
	{
	case 0xc1: load_command_register(data);                  break;
	case 0xc2: m_head = data & 0xffff;                       break;   // head register
	case 0xc8: load_cyl_sec_register(data);                  break;
	case 0xc9: m_serial_low  = data & 0xffff;                break;
	case 0xca: m_serial_high = data & 0xffff;                break;
	case 0xcb: m_block_number = data & 0xffff;               break;
	case 0xd0: m_data_buffer_high = (~data) & 0xffff;        break;   // data buffer addr high
	case 0xd1: m_header_addr_high = (~data) & 0xffff;        break;   // header addr high
	case 0xd8: m_data_buffer_low = unfrob(data) & 0xffff;    break;   // data buffer addr low
	case 0xd9: m_header_addr_low = unfrob(data) & 0xffff;    break;   // header addr low
	default:                                                 break;
	}
}

u16 perq_shugart::disk_status() const
{
	return u16(m_controller_status & 0x7)
		| (m_index         << 3)
		| (m_track_zero    << 4)
		| (m_drive_fault   << 5)
		| (m_seek_complete << 6)
		| (m_unit_ready    << 7);
}

void perq_shugart::load_cyl_sec_register(u16 v)
{
	m_sector   = v & 0x1f;
	m_head     = (v & 0xe0) >> 5;
	m_cylinder = (v & 0xff80) >> 8;
}

void perq_shugart::load_command_register(u16 data)
{
	// command bits: 0:2 command, 3 seek direction, 4 step pulse, 5 single-seek (not decoded)
	switch (command(data & 0x7))
	{
	case command::IDLE:        m_cpu.clear_interrupt(perq_cpu_device::IRQ_HARDDISK); break;
	case command::RESET:       reset_flags(); set_busy_state();                      break;
	case command::READ_CHK:
	case command::READ_DIAG:   read_block();                                         break;
	case command::WRITE_FIRST: write_block(true);                                    break;
	case command::WRITE_CHK:   write_block(false);                                   break;
	case command::FORMAT:      format_block();                                       break;
	default:                                                                         break;
	}

	m_seek_data = data;
	clock_seek();
}


//**************************************************************************
//  Seek state machine (stepped one cylinder per command-register write)
//**************************************************************************

void perq_shugart::clock_seek()
{
	switch (m_seek_state)
	{
	case SK_WAIT_STEP_SET:
		if (m_seek_data & 0x10)
		{
			m_seek_state = SK_WAIT_STEP_RELEASE;
			m_seek_complete = 0;
		}
		break;

	case SK_WAIT_STEP_RELEASE:
		if (!(m_seek_data & 0x10))
			m_seek_state = SK_SEEK_COMPLETE;
		break;

	case SK_SEEK_COMPLETE:
		do_single_seek();
		m_seek_complete = 1;
		m_seek_state = SK_WAIT_STEP_SET;
		m_cpu.raise_interrupt(perq_cpu_device::IRQ_HARDDISK);
		break;
	}
}

void perq_shugart::do_single_seek()
{
	seek_to((m_seek_data & 0x8) ? m_phys_cylinder + 1 : m_phys_cylinder - 1);
}

void perq_shugart::do_multiple_seek(int count)
{
	seek_to((m_seek_data & 0x8) ? m_phys_cylinder + count : m_phys_cylinder - count);
}

void perq_shugart::seek_to(int cyl)
{
	m_phys_cylinder = std::clamp(cyl, 0, disk_cylinders() - 1);
	m_track_zero = (m_phys_cylinder == 0) ? 1 : 0;
}


//**************************************************************************
//  Block transfers (DMA into / out of main memory)
//**************************************************************************

void perq_shugart::read_block()
{
	const u8 *hdr, *data;
	if (m_image && m_image->read_sector(m_cylinder, m_head, m_sector, hdr, data))
	{
		const u32 data_addr   = m_data_buffer_low | (m_data_buffer_high << 16);
		const u32 header_addr = m_header_addr_low | (m_header_addr_high << 16);

		for (int i = 0; i < 512; i += 2)   // 256 data words
			m_cpu.mem_write(data_addr + (i >> 1), data[i] | (data[i + 1] << 8));
		for (int i = 0; i < 16; i += 2)    // 8 header words
			m_cpu.mem_write(header_addr + (i >> 1), hdr[i] | (hdr[i + 1] << 8));
	}

	set_busy_state();   // always, so the busy->done handshake + interrupt still fire
}

void perq_shugart::write_block(bool write_header)
{
	// the image is read-only for now; persistence is added in a later phase.
	// the busy/done handshake still runs so the microcode does not deadlock.
	(void)write_header;
	set_busy_state();
}

void perq_shugart::format_block()
{
	set_busy_state();
}


//**************************************************************************
//  Status timing (busy pulse + index pulse), driven by CPU device timers
//**************************************************************************

void perq_shugart::set_busy_state()
{
	if (m_controller_status == ST_BUSY)
		return;

	m_controller_status = ST_BUSY;
	m_cpu.disk_arm_busy_timer(attotime::from_usec(1000));   // ~1 ms, then done + interrupt
}

void perq_shugart::on_busy_done()
{
	m_controller_status = ST_DONE;
	m_cpu.raise_interrupt(perq_cpu_device::IRQ_HARDDISK);
}

void perq_shugart::on_index_edge()
{
	// SA4000 spins at 3000 rpm: a ~1.1 us index pulse once every ~20 ms
	m_index ^= 1;
	m_cpu.disk_arm_index_timer(m_index ? attotime::from_nsec(1100) : attotime::from_usec(20000));
}
