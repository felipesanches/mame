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
	LOGMASKED(LOG_PARALLEL, "parallel cmd 0x%02X\n", data);
}

void mn19413_device::parallel_data_w(uint8_t data)
{
	m_par_data.push_back(data);
	LOGMASKED(LOG_PARALLEL, "parallel data 0x%02X (byte#%zu for cmd 0x%02X)\n",
		data, m_par_data.size(), m_par_cmd);
}

void mn19413_device::process_command()
{
	if (m_par_data.empty())
		return;

	LOGMASKED(LOG_PARALLEL, "cmd 0x%02X complete: %zu data bytes [",
		m_par_cmd, m_par_data.size());
	for (size_t i = 0; i < m_par_data.size() && i < 8; i++)
		LOGMASKED(LOG_PARALLEL, "%s0x%02X", i ? " " : "", m_par_data[i]);
	if (m_par_data.size() > 8)
		LOGMASKED(LOG_PARALLEL, " ...");
	LOGMASKED(LOG_PARALLEL, "]\n");
}
