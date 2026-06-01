// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ hard-disk image device (.phd)

    Loads a PERQemu .phd hard-disk image: a 9-byte file header ("PERQ", a
    sector-header-present flag and the CHS geometry) followed by the sectors
    in cylinder/head/sector order, each one a one-byte status flag, a 16-byte
    logical header and 512 bytes of data.  The Shugart controller inside the
    PERQ CPU device reads sectors through this device and DMAs them into main
    memory.

    Ported from PERQemu PhysicalDisk/{HardDisk,ShugartDisk,HardDiskSector}.cs
    by Josh Dersch (GPL-3.0+).

***************************************************************************/

#ifndef MAME_IMAGEDEV_PERQ_HDC_H
#define MAME_IMAGEDEV_PERQ_HDC_H

#pragma once

#include <string>
#include <system_error>
#include <utility>
#include <vector>

class perq_harddisk_image_device : public device_t, public device_image_interface
{
public:
	perq_harddisk_image_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	// device_image_interface
	virtual bool is_readable()      const noexcept override { return true; }
	virtual bool is_writeable()     const noexcept override { return false; }   // read-only for now
	virtual bool is_creatable()     const noexcept override { return false; }
	virtual bool is_reset_on_load() const noexcept override { return false; }
	virtual bool image_is_chd_type() const noexcept override { return false; }  // flat .phd, not a CHD
	virtual const char *image_type_name()       const noexcept override { return "harddisk"; }
	virtual const char *image_brief_type_name() const noexcept override { return "hard"; }
	virtual const char *file_extensions()       const noexcept override { return "phd"; }

	virtual std::pair<std::error_condition, std::string> call_load() override;
	virtual void call_unload() override;

	// controller access
	bool     loaded()    const { return !m_data.empty(); }
	unsigned cylinders() const { return m_cylinders; }
	unsigned heads()     const { return m_heads; }
	unsigned sectors()   const { return m_sectors; }

	// point hdr (16 bytes) and data (512 bytes) at the requested sector
	bool read_sector(unsigned cyl, unsigned head, unsigned sec, const u8 *&hdr, const u8 *&data) const;

protected:
	virtual void device_start() override ATTR_COLD;

private:
	static constexpr unsigned FILE_HEADER   = 9;     // "PERQ" + flag + CHS
	static constexpr unsigned SECTOR_HEADER = 16;    // logical-block header bytes
	static constexpr unsigned SECTOR_DATA   = 512;   // data bytes
	static constexpr unsigned RECORD        = 1 + SECTOR_HEADER + SECTOR_DATA;  // 529 bytes/sector

	std::vector<u8> m_data;
	unsigned m_cylinders = 0;
	unsigned m_heads = 0;
	unsigned m_sectors = 0;
};

DECLARE_DEVICE_TYPE(PERQ_HARDDISK, perq_harddisk_image_device)

#endif // MAME_IMAGEDEV_PERQ_HDC_H
