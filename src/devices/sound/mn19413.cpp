// license:GPL2+
// copyright-holders:Felipe Sanches
/***************************************************************************

    MN19413 Effect DSP (IC310)

    Serial-interfaced effect DSP used in the Technics SX-KN5000.
    This is an HLE stub that logs all received bytes for protocol
    reverse engineering. No audio output is generated yet.

    Connected via SubCPU serial port 0. Bytes are shifted in on the
    rising edge of SCLK, MSB first.

    See header for known commands and debug strings.

    Parallel port command protocol (bit-banged via PF pins):
      0x30 — Register/parameter write (4 data bytes: 0, addr, value_hi, value_lo)
      0x03 — End parameter block (latch pending writes)

    NOTE: Due to the bit-banged serial framing, all bytes currently
    arrive via parallel_data_w() as a single stream under cmd 0x00.
    The stream contains embedded 0x30 sub-commands every 5 bytes.

***************************************************************************/

#include "emu.h"
#include "mn19413.h"

#define LOG_SERIAL   (1U << 1)   // Serial byte reception
#define LOG_PARALLEL (1U << 2)   // Parallel port command/data

#define VERBOSE (LOG_PARALLEL)
#include "logmacro.h"

DEFINE_DEVICE_TYPE(MN19413, mn19413_device, "mn19413", "MN19413 Effect DSP")

mn19413_device::mn19413_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, MN19413, tag, owner, clock)
	, m_rx_shift(0)
	, m_rx_bit_count(0)
	, m_rxd_state(1)
	, m_sclk_state(0)
	, m_byte_count(0)
	, m_current_command(0)
	, m_par_cmd(0)
	, m_txd_cb(*this)
{
}

void mn19413_device::device_start()
{
	save_item(NAME(m_rx_shift));
	save_item(NAME(m_rx_bit_count));
	save_item(NAME(m_rxd_state));
	save_item(NAME(m_sclk_state));
	save_item(NAME(m_byte_count));
	save_item(NAME(m_current_command));
	save_item(NAME(m_par_cmd));
}

void mn19413_device::device_reset()
{
	m_rx_shift = 0;
	m_rx_bit_count = 0;
	m_rxd_state = 1;
	m_sclk_state = 0;
	m_byte_count = 0;
	m_current_command = 0;
	m_par_cmd = 0;
	m_par_data.clear();
}

void mn19413_device::rxd(int state)
{
	m_rxd_state = state ? 1 : 0;
}

void mn19413_device::sclk(int state)
{
	uint8_t new_sclk = state ? 1 : 0;

	// Sample data on rising edge of SCLK
	if (!m_sclk_state && new_sclk)
	{
		m_rx_shift = (m_rx_shift << 1) | m_rxd_state;
		m_rx_bit_count++;

		if (m_rx_bit_count >= 8)
		{
			uint8_t byte = m_rx_shift;
			m_rx_bit_count = 0;
			m_rx_shift = 0;

			LOGMASKED(LOG_SERIAL, "RX byte 0x%02X (byte#%d cmd=0x%02X)\n",
				byte, m_byte_count, m_current_command);

			// Track first byte as command for context in subsequent data logging
			if (m_byte_count == 0)
				m_current_command = byte;

			m_byte_count++;
		}
	}

	m_sclk_state = new_sclk;
}

//--------------------------------------------------------------------------
//  Parallel port interface (delivered by driver from PF bit-bang decoding)
//--------------------------------------------------------------------------

void mn19413_device::parallel_command_w(uint8_t data)
{
	// New command starts — process any accumulated previous command
	if (!m_par_data.empty())
		process_command();

	m_par_cmd = data;
	m_par_data.clear();

	// Log no-data commands immediately
	switch (data)
	{
	case 0x03:
		LOGMASKED(LOG_PARALLEL, "END BLOCK (latch pending writes)\n");
		break;
	default:
		break;
	}
}

void mn19413_device::parallel_data_w(uint8_t data)
{
	m_par_data.push_back(data);

	// Decode embedded 0x30 register write frames in the data stream.
	// The firmware sends: cmd 0x30, data 0x00, data addr, data val_hi, data val_lo
	// But due to bit-bang framing issues, all bytes arrive as "data" under cmd 0x00.
	// Detect 5-byte frames: [0x30, 0x00, addr, val_hi, val_lo]
	size_t sz = m_par_data.size();
	if (sz >= 5 && (sz % 5) == 0)
	{
		size_t base = sz - 5;
		uint8_t sub_cmd = m_par_data[base];
		if (sub_cmd == 0x30 && m_par_data[base + 1] == 0x00)
		{
			uint8_t addr = m_par_data[base + 2];
			uint16_t value = (uint16_t(m_par_data[base + 3]) << 8) | m_par_data[base + 4];
			LOGMASKED(LOG_PARALLEL, "REG WRITE addr=0x%02X value=0x%04X\n", addr, value);
			return;
		}
	}
}

void mn19413_device::process_command()
{
	if (m_par_data.empty())
		return;

	switch (m_par_cmd)
	{
	case 0x30: // Register write: data[0]=0, data[1]=addr, data[2..3]=value
		if (m_par_data.size() >= 4)
		{
			uint8_t addr = m_par_data[1];
			uint16_t value = (uint16_t(m_par_data[2]) << 8) | m_par_data[3];
			LOGMASKED(LOG_PARALLEL, "REG WRITE addr=0x%02X value=0x%04X\n", addr, value);
		}
		else
		{
			LOGMASKED(LOG_PARALLEL, "REG WRITE (incomplete: %zu bytes)\n", m_par_data.size());
		}
		break;

	default:
		// Summarize the accumulated data stream
		if (m_par_data.size() > 0)
		{
			// Count embedded 0x30 sub-commands
			size_t reg_writes = 0;
			for (size_t i = 0; i + 4 < m_par_data.size(); i += 5)
			{
				if (m_par_data[i] == 0x30 && m_par_data[i + 1] == 0x00)
					reg_writes++;
			}
			if (reg_writes > 0)
				LOGMASKED(LOG_PARALLEL, "STREAM cmd=0x%02X: %zu bytes (%zu reg writes)\n",
					m_par_cmd, m_par_data.size(), reg_writes);
			else
				LOGMASKED(LOG_PARALLEL, "CMD 0x%02X: %zu data bytes\n",
					m_par_cmd, m_par_data.size());
		}
		break;
	}
}
