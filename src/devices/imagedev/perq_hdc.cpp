// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ hard-disk image device (.phd)

    See perq_hdc.h.  Ported from PERQemu by Josh Dersch (GPL-3.0+).

***************************************************************************/

#include "emu.h"
#include "perq_hdc.h"

DEFINE_DEVICE_TYPE(PERQ_HARDDISK, perq_harddisk_image_device, "perq_hdc", "PERQ Hard Disk")

perq_harddisk_image_device::perq_harddisk_image_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, PERQ_HARDDISK, tag, owner, clock)
	, device_image_interface(mconfig, *this)
{
}

void perq_harddisk_image_device::device_start()
{
	save_item(NAME(m_cylinders));
	save_item(NAME(m_heads));
	save_item(NAME(m_sectors));
}

std::pair<std::error_condition, std::string> perq_harddisk_image_device::call_load()
{
	const u64 len = length();
	if (len < FILE_HEADER)
		return std::make_pair(image_error::INVALIDLENGTH, std::string());

	// slurp the whole image (the position is at the start of the file on load)
	m_data.resize(len);
	if (fread(m_data.data(), u32(len)) != len)
	{
		m_data.clear();
		return std::make_pair(image_error::INVALIDIMAGE, std::string());
	}

	if (m_data[0] != 'P' || m_data[1] != 'E' || m_data[2] != 'R' || m_data[3] != 'Q')
	{
		m_data.clear();
		return std::make_pair(image_error::INVALIDIMAGE, std::string("not a PERQ (.phd) hard-disk image"));
	}
	if (m_data[4] != 1)
	{
		m_data.clear();
		return std::make_pair(image_error::INVALIDIMAGE, std::string("sector-header data must be present"));
	}

	m_cylinders = (unsigned(m_data[5]) << 8) | m_data[6];   // big-endian
	m_sectors   = m_data[7];
	m_heads     = m_data[8];

	const u64 expected = u64(FILE_HEADER) + u64(m_cylinders) * m_heads * m_sectors * RECORD;
	if (len != expected)
	{
		m_data.clear();
		return std::make_pair(image_error::INVALIDLENGTH, std::string("image size does not match its geometry"));
	}

	return std::make_pair(std::error_condition(), std::string());
}

void perq_harddisk_image_device::call_unload()
{
	m_data.clear();
	m_cylinders = m_heads = m_sectors = 0;
}

bool perq_harddisk_image_device::read_sector(unsigned cyl, unsigned head, unsigned sec, const u8 *&hdr, const u8 *&data) const
{
	if (m_data.empty() || cyl >= m_cylinders || head >= m_heads || sec >= m_sectors)
		return false;

	// sectors are stored cylinder-major, then head, then sector
	const unsigned idx = (cyl * m_heads + head) * m_sectors + sec;
	const u8 *rec = m_data.data() + FILE_HEADER + std::size_t(idx) * RECORD;
	// rec[0] = bad-sector flag; rec[1..16] = logical header; rec[17..528] = data
	hdr  = rec + 1;
	data = rec + 1 + SECTOR_HEADER;
	return true;
}
