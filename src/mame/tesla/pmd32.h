// license:GPL-3.0-or-later
// copyright-holders:Roman Bórik, Martin Bórik, Felipe Corrêa da Silva Sanches
/*******************************************************************************

    PMD-32 floppy disk unit (high-level emulation)

    The PMD-32 is the external "intelligent" floppy disk unit for the Tesla
    PMD-85 family and the Consul 2717.  It connects through an Intel 8255 PPI
    running in mode 2 (bidirectional, strobed) and speaks a byte-stream command
    protocol.  This device reproduces that protocol rather than emulating the
    unit's own 8080, whose firmware is not dumped; the host side (the Consul's
    disk EPROM) drives the GPIO 8255 at I/O ports 4Ch-4Fh.

    The protocol model is a port of the Pmd32 class from GPMD85Emulator
    (https://github.com/mborik/GPMD85Emulator) by Roman Bórik and Martin Bórik,
    which is licensed GPL-3.0-or-later; this file is therefore also
    GPL-3.0-or-later.

*******************************************************************************/

#ifndef MAME_TESLA_PMD32_H
#define MAME_TESLA_PMD32_H

#pragma once

#include "machine/i8255.h"
#include "softlist_dev.h"


class pmd32_device : public device_t, public device_image_interface
{
public:
	pmd32_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// the GPIO 8255 this unit is wired to (so it can drive the mode-2 strobe/ack)
	template <typename T> void set_ppi(T &&tag) { m_ppi.set_tag(std::forward<T>(tag)); }

	// hook these up to the 8255's port-A and port-C callbacks
	uint8_t data_r();             // -> ppi in_pa_callback  (byte the unit presents)
	void    data_w(uint8_t data); // <- ppi out_pa_callback (byte the host wrote)
	void    pc_w(uint8_t data);   // <- ppi out_pc_callback (mode-2 handshake status)

	// device_image_interface
	virtual bool is_readable()       const noexcept override { return true; }
	virtual bool is_writeable()      const noexcept override { return false; }
	virtual bool is_creatable()      const noexcept override { return false; }
	virtual bool is_reset_on_load()  const noexcept override { return false; }
	virtual const char *image_interface()       const noexcept override { return "c2717_flop"; }
	virtual const char *file_extensions()       const noexcept override { return "img,p32,dz8"; }
	virtual const char *image_type_name()       const noexcept override { return "floppydisk"; }
	virtual const char *image_brief_type_name() const noexcept override { return "flop"; }

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	virtual std::pair<std::error_condition, std::string> call_load() override;
	virtual void call_unload() override;
	virtual const software_list_loader &get_software_list_loader() const override { return image_software_list_loader::instance(); }

private:
	// protocol states (from GPMD85Emulator Pmd32.h)
	enum : uint8_t
	{
		IDLE_STATE = 0, WAIT_PRESENT, WAIT_COMMAND, WAIT_CRC, WAIT_SECTOR,
		WAIT_TRACK, WAIT_DRIVE, WAIT_DATA, SEND_DATA, SEND_CRC, SEND_ACK,
		SEND_RESULT, SEND_NAK, WAIT_ADDR_H, WAIT_ADDR_L, WAIT_LEN_H, WAIT_LEN_L,
		WAIT_DATA_MEM, SEND_DATA_MEM, WAIT_WP
	};

	enum : uint8_t { PRESENTATION = 0xaa, ACK = 0x33, NAK = 0x99 };

	enum : uint8_t
	{
		RESULT_OK = 0, RESULT_WP = 1, RESULT_FE = 2, RESULT_RE = 3,
		RESULT_WE = 4, RESULT_BD = 5, RESULT_NF = 6
	};

	static constexpr unsigned SECTOR_SIZE = 128;
	static constexpr unsigned PHYSICAL_SECTOR_SIZE = 4 * SECTOR_SIZE;
	static constexpr unsigned INTERNAL_RAM_SIZE = 8 * SECTOR_SIZE;
	static constexpr unsigned MAX_SECTORS_PER_TRACK = 64;

	TIMER_CALLBACK_MEMBER(service);
	void state_check();            // == Disk32ServiceStateCheck
	void send_result_command();    // == Disk32ServiceSendResultCommand
	bool prepare_sector();
	bool write_sector();
	void strobe_to_host(uint8_t data);

	required_device<i8255_device> m_ppi;
	emu_timer *m_timer;

	// single mounted image (drive A); geometry inferred from its size
	std::vector<uint8_t> m_disk;
	uint8_t m_tracks;       // total tracks
	uint8_t m_sectors;      // logical 128-byte sectors per track

	// protocol state
	uint8_t m_state;
	uint8_t m_command;
	uint8_t m_crc;
	uint8_t m_drvnum, m_track, m_sector;
	int     m_address, m_length;
	uint8_t m_wp;
	int     m_byte_counter;
	uint8_t m_to_send;
	bool    m_no_send;
	bool    m_ibf;          // cached host-side input-buffer-full (from pc_w)

	uint8_t m_buffer[MAX_SECTORS_PER_TRACK * SECTOR_SIZE];
	uint8_t m_memory[INTERNAL_RAM_SIZE];
	int     m_point;        // index into m_buffer
};

DECLARE_DEVICE_TYPE(PMD32, pmd32_device)

#endif // MAME_TESLA_PMD32_H
