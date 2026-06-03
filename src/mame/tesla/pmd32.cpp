// license:GPL-3.0-or-later
// copyright-holders:Roman Bórik, Martin Bórik, Felipe Corrêa da Silva Sanches
/*******************************************************************************

    PMD-32 floppy disk unit (high-level emulation)

    Ported from the Pmd32 class of GPMD85Emulator (GPL-3.0-or-later) by
    Roman Bórik and Martin Bórik.  See pmd32.h for the overview.

    The unit hangs off an Intel 8255 PPI run in mode 2 by the host's disk
    EPROM (GPIO at I/O 4Ch-4Fh on the Consul 2717).  Bytes from the host arrive
    through the 8255's port-A output callback (data_w); bytes to the host are
    presented through the port-A input callback (data_r) and strobed in with
    pc4_w; the mode-2 handshake status is observed through the port-C output
    callback (pc_w).  A periodic timer paces the unit's outgoing bytes, the way
    the real unit's processor would.

    The wire protocol: the host opens with PRESENTATION (0xAA), then a command
    byte, command-specific arguments, and a CRC (XOR of all bytes); the unit
    answers ACK/NAK, a result code, and (for reads) the sector data and its CRC.

*******************************************************************************/

#include "emu.h"
#include "pmd32.h"


DEFINE_DEVICE_TYPE(PMD32, pmd32_device, "pmd32", "PMD-32 floppy disk unit")


pmd32_device::pmd32_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, PMD32, tag, owner, clock)
	, device_image_interface(mconfig, *this)
	, m_ppi(*this, finder_base::DUMMY_TAG)
	, m_timer(nullptr)
	, m_tracks(0)
	, m_sectors(0)
	, m_state(WAIT_PRESENT)
	, m_command(0)
	, m_crc(0)
	, m_drvnum(0)
	, m_track(0)
	, m_sector(0)
	, m_address(0)
	, m_length(0)
	, m_wp(1)
	, m_byte_counter(0)
	, m_to_send(PRESENTATION)
	, m_no_send(true)
	, m_ibf(false)
	, m_point(0)
{
}


void pmd32_device::device_start()
{
	m_timer = timer_alloc(FUNC(pmd32_device::service), this);

	std::fill(std::begin(m_buffer), std::end(m_buffer), 0);
	std::fill(std::begin(m_memory), std::end(m_memory), 0);

	save_item(NAME(m_tracks));
	save_item(NAME(m_sectors));
	save_item(NAME(m_state));
	save_item(NAME(m_command));
	save_item(NAME(m_crc));
	save_item(NAME(m_drvnum));
	save_item(NAME(m_track));
	save_item(NAME(m_sector));
	save_item(NAME(m_address));
	save_item(NAME(m_length));
	save_item(NAME(m_wp));
	save_item(NAME(m_byte_counter));
	save_item(NAME(m_to_send));
	save_item(NAME(m_no_send));
	save_item(NAME(m_ibf));
	save_item(NAME(m_point));
	save_item(NAME(m_buffer));
	save_item(NAME(m_memory));
}


void pmd32_device::device_reset()
{
	m_state = WAIT_PRESENT;
	m_command = 0;
	m_crc = 0;
	m_byte_counter = 0;
	m_point = 0;
	m_to_send = PRESENTATION;
	m_no_send = true;
	m_ibf = false;

	// run the unit's byte-pump at ~100 us/byte, matching the original model
	m_timer->adjust(attotime::from_usec(100), 0, attotime::from_usec(100));
}


//-------------------------------------------------
//  i8255 mode-2 glue
//-------------------------------------------------

uint8_t pmd32_device::data_r()
{
	// the byte the unit is presenting; sampled by the 8255 on a pc4_w strobe
	return m_to_send;
}


void pmd32_device::pc_w(uint8_t data)
{
	// PC5 = IBFa: set while the host still holds a byte we strobed in
	m_ibf = BIT(data, 5);
}


void pmd32_device::strobe_to_host(uint8_t data)
{
	m_to_send = data;
	m_ppi->pc4_w(0);   // strobe low: 8255 latches data_r() into input, raises IBF (+INTRa)
	m_ppi->pc4_w(1);
}


void pmd32_device::data_w(uint8_t data)
{
	// host wrote a byte to port A (mode-2 output); run the receive state machine
	switch (m_state)
	{
	case WAIT_PRESENT:
		if (data == PRESENTATION)
			m_state = WAIT_COMMAND;
		break;

	case WAIT_COMMAND:
		m_command = data;
		switch (data)
		{
		case 'B': // boot
		case '*': // fast mode
		case '@': // slow mode
			m_state = WAIT_CRC;
			break;

		case 'Q': case 'R': // read logical sector
		case 'T': case 'W': // write logical sector
		case 'S':           // write physical sector
		case 'F':           // format track
			m_state = WAIT_SECTOR;
			break;

		case 'I': // drive select + home
			m_state = WAIT_DRIVE;
			break;

		case 'U': // write to PMD-32 RAM
		case 'C': // read from PMD-32 RAM
		case 'J': // execute code in PMD-32 RAM
			m_state = WAIT_ADDR_H;
			break;

		// PMD-32-SD extensions are not emulated here
		case 'G': case 'H': case 'P': case 'K': case 'L': case 'M': case 'N':
		case PRESENTATION:
		default:
			m_state = WAIT_PRESENT;
			break;
		}
		m_crc = m_command;
		break;

	case WAIT_SECTOR:
		m_sector = data;
		m_drvnum = m_sector >> 6;
		if (m_drvnum > 0 && m_drvnum < 3)
			m_drvnum ^= 3;
		m_sector &= 0x3f;
		m_crc ^= data;
		m_state = WAIT_TRACK;
		break;

	case WAIT_TRACK:
		m_track = data;
		m_crc ^= data;
		if (m_command == 'T' || m_command == 'W' || m_command == 'S')
		{
			m_point = 0;
			m_byte_counter = (m_command == 'S') ? (PHYSICAL_SECTOR_SIZE + 1) : SECTOR_SIZE;
			m_state = WAIT_DATA;
		}
		else
			m_state = WAIT_CRC;
		break;

	case WAIT_DRIVE:
		m_drvnum = data;
		m_crc ^= data;
		m_state = WAIT_CRC;
		break;

	case WAIT_DATA:
		if (m_point < int(sizeof(m_buffer)))
			m_buffer[m_point++] = data;
		m_crc ^= data;
		if (--m_byte_counter == 0)
			m_state = WAIT_CRC;
		break;

	case WAIT_ADDR_H:
		m_address = data << 8;
		m_crc ^= data;
		m_state = WAIT_ADDR_L;
		break;

	case WAIT_ADDR_L:
		m_address |= data;
		m_crc ^= data;
		m_state = (m_command == 'J') ? WAIT_CRC : WAIT_LEN_H;
		break;

	case WAIT_LEN_H:
		m_length = data << 8;
		m_crc ^= data;
		m_state = WAIT_LEN_L;
		break;

	case WAIT_LEN_L:
		m_length |= data;
		m_crc ^= data;
		if (m_command == 'C')
			m_state = WAIT_CRC;
		else
		{
			m_byte_counter = m_length;
			m_state = WAIT_DATA_MEM;
		}
		break;

	case WAIT_DATA_MEM:
		if (m_address >= 0 && m_address < int(INTERNAL_RAM_SIZE))
			m_memory[m_address] = data;
		m_address++;
		m_crc ^= data;
		if (--m_byte_counter == 0)
			m_state = WAIT_CRC;
		break;

	case WAIT_CRC:
		m_state = (data == m_crc) ? SEND_ACK : SEND_NAK;
		break;
	}

	// acknowledge the host's byte (mode-2 /ACKa), clearing /OBFa so it can continue
	m_ppi->pc6_w(0);
	m_ppi->pc6_w(1);
}


//-------------------------------------------------
//  outgoing byte pump
//-------------------------------------------------

TIMER_CALLBACK_MEMBER(pmd32_device::service)
{
	if (m_ibf)              // host has not consumed the previous byte yet
		return;

	state_check();
	if (!m_no_send)
		strobe_to_host(m_to_send);
}


void pmd32_device::state_check()
{
	m_no_send = false;

	switch (m_state)
	{
	case WAIT_PRESENT:
		m_to_send = PRESENTATION;
		break;

	case SEND_DATA:
		m_to_send = m_buffer[m_point++];
		m_crc ^= m_to_send;
		if (--m_byte_counter == 0)
			m_state = SEND_CRC;
		break;

	case SEND_DATA_MEM:
		m_to_send = (m_address >= 0 && m_address < int(INTERNAL_RAM_SIZE)) ? m_memory[m_address] : 0;
		m_address++;
		m_crc ^= m_to_send;
		if (--m_byte_counter == 0)
			m_state = SEND_CRC;
		break;

	case SEND_CRC:
		m_to_send = m_crc;
		m_state = WAIT_COMMAND;
		break;

	case SEND_ACK:
		m_to_send = ACK;
		m_state = SEND_RESULT;
		if (m_command == 'C') // read from PMD-32 RAM: ACK is followed by the data
		{
			m_byte_counter = m_length;
			m_crc = ACK;
			m_state = SEND_DATA_MEM;
		}
		break;

	case SEND_RESULT:
		send_result_command();
		break;

	case SEND_NAK:
		m_to_send = NAK;
		m_state = WAIT_COMMAND;
		break;

	default:
		m_no_send = true;
		break;
	}
}


void pmd32_device::send_result_command()
{
	switch (m_command)
	{
	case 'B': // boot reads drive 0 / track 0 / sector 0
		m_drvnum = 0;
		m_track = 0;
		m_sector = 0;
		[[fallthrough]];
	case 'Q':
	case 'R':
		if (prepare_sector())
		{
			m_point = 0;
			m_byte_counter = SECTOR_SIZE;
			m_crc = 0;
			m_state = SEND_DATA;
			m_to_send = RESULT_OK;
		}
		else
		{
			m_state = WAIT_COMMAND;
			m_to_send = RESULT_RE;
		}
		break;

	case 'T':
	case 'W':
	case 'S':
	case 'F':
		if (m_wp)
			m_to_send = RESULT_WP;
		else if (write_sector())
			m_to_send = RESULT_OK;
		else
			m_to_send = RESULT_WE;
		m_state = WAIT_COMMAND;
		break;

	case 'I': // drive select
	case '*': // fast mode
	case '@': // slow mode
	case 'U': // write to PMD-32 RAM
	case 'J': // execute code in PMD-32 RAM (not actually executed here)
		m_to_send = RESULT_OK;
		m_state = WAIT_COMMAND;
		break;

	default:
		m_to_send = RESULT_NF;
		m_state = WAIT_COMMAND;
		break;
	}
}


//-------------------------------------------------
//  disk access (single mounted image)
//-------------------------------------------------

bool pmd32_device::prepare_sector()
{
	if (!m_disk.empty() && m_sector < m_sectors && (m_tracks == 0 || m_track < m_tracks))
	{
		uint32_t const seek = (uint32_t(m_track) * m_sectors + m_sector) * SECTOR_SIZE;
		if (seek + SECTOR_SIZE <= m_disk.size())
		{
			std::memcpy(m_buffer, &m_disk[seek], SECTOR_SIZE);
			return true;
		}
	}
	return false;
}


bool pmd32_device::write_sector()
{
	// archival images are mounted read-only for now (the unit reports write-protected)
	return false;
}


std::pair<std::error_condition, std::string> pmd32_device::call_load()
{
	uint32_t const size = length();
	m_disk.resize(size);
	if (size)
	{
		fseek(0, SEEK_SET);
		if (fread(&m_disk[0], size) != size)
			return std::make_pair(image_error::UNSPECIFIED, "Error reading PMD-32 disk image");
	}

	// infer the 8" CP/M geometry: 128-byte logical sectors, 26 sectors/track
	m_sectors = 26;
	m_tracks = (size % (26 * SECTOR_SIZE) == 0) ? uint8_t(size / (26 * SECTOR_SIZE)) : 0;

	return std::make_pair(std::error_condition(), std::string());
}


void pmd32_device::call_unload()
{
	m_disk.clear();
	m_tracks = 0;
	m_sectors = 0;
}
