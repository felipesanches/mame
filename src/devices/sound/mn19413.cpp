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

#define LOG_SERIAL (1U << 1)   // Serial byte reception

#define VERBOSE (LOG_SERIAL)
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
}

void mn19413_device::device_reset()
{
	m_rx_shift = 0;
	m_rx_bit_count = 0;
	m_rxd_state = 1;
	m_sclk_state = 0;
	m_byte_count = 0;
	m_current_command = 0;
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
