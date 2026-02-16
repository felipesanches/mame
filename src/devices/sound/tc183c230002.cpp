// license:GPL2+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Technics TC183C230002 Tone Generator (IC303)

    64-voice wavetable synthesizer used in the Technics SX-KN5000.
    This is an HLE stub that logs all register accesses for protocol
    reverse engineering. No audio output is generated yet.

    See header for register layout documentation.

***************************************************************************/

#include "emu.h"
#include "tc183c230002.h"

#define LOG_REG    (1U << 1)   // Register read/write
#define LOG_KEYBED (1U << 2)   // Keybed data/status reads

#define VERBOSE (LOG_REG | LOG_KEYBED)
#include "logmacro.h"

DEFINE_DEVICE_TYPE(TC183C230002, tc183c230002_device, "tc183c230002", "TC183C230002 Tone Generator")

tc183c230002_device::tc183c230002_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, TC183C230002, tag, owner, clock)
	, m_config_addr(0)
{
}

void tc183c230002_device::device_start()
{
	save_item(NAME(m_config_addr));
	save_item(NAME(m_regs));
}

void tc183c230002_device::device_reset()
{
	m_config_addr = 0;
	std::fill(std::begin(m_regs), std::end(m_regs), 0);
	while (!m_keybed_queue.empty())
		m_keybed_queue.pop();
}

//--------------------------------------------------------------------------
//  Config register interface (0x100000 addr, 0x100002 data)
//--------------------------------------------------------------------------

void tc183c230002_device::config_addr_w(uint16_t data)
{
	m_config_addr = data & 0x0fff;
}

uint16_t tc183c230002_device::config_addr_r()
{
	return m_config_addr;
}

void tc183c230002_device::config_data_w(uint16_t data)
{
	m_regs[m_config_addr] = data;

	uint8_t group   = (m_config_addr >> 8) & 0x0f;
	uint8_t bank    = (m_config_addr >> 6) & 0x03;
	uint8_t channel = m_config_addr & 0x3f;

	// Decode well-known register values for semantic logging
	char const *desc = "";
	if (group == 0x00)
	{
		if (data == 0x8100)
			desc = " (voice key-on)";
		else if (data == 0x7e00)
			desc = " (voice idle)";
		else if (data == 0x1200)
			desc = " (voice transition)";
	}
	else if (group == 0x08)
	{
		if (data == 0xff00)
			desc = " (volume mute)";
	}

	if (group == 0x02 || group == 0x0c || group == 0x0e)
		LOGMASKED(LOG_REG, "reg global[0x%04X] = 0x%04X%s\n",
			m_config_addr, data, desc);
	else
		LOGMASKED(LOG_REG, "reg[grp=0x%02X bank=%d ch=%d] = 0x%04X%s\n",
			group, bank, channel, data, desc);
}

uint16_t tc183c230002_device::config_data_r()
{
	uint16_t data = m_regs[m_config_addr];
	LOGMASKED(LOG_REG, "reg read[0x%04X] = 0x%04X\n", m_config_addr, data);
	return data;
}

//--------------------------------------------------------------------------
//  Keybed interface (0x110000 data, 0x110002 status)
//--------------------------------------------------------------------------

uint16_t tc183c230002_device::keyboard_status_r()
{
	// Bit 0 = data ready (queue non-empty)
	uint16_t status = m_keybed_queue.empty() ? 0x0000 : 0x0001;
	return status;
}

uint16_t tc183c230002_device::keyboard_data_r()
{
	if (m_keybed_queue.empty())
		return 0x0000;

	uint16_t data = m_keybed_queue.front();
	m_keybed_queue.pop();

	uint8_t raw_note = data & 0x7f;
	bool note_on = BIT(data, 7);
	uint8_t velocity = (data >> 8) & 0xff;

	if (note_on)
		LOGMASKED(LOG_KEYBED, "keybed read: note-on raw=%d MIDI=%d vel=%d data=0x%04X\n",
			raw_note, raw_note + 0x24, velocity, data);
	else
		LOGMASKED(LOG_KEYBED, "keybed read: note-off raw=%d MIDI=%d data=0x%04X\n",
			raw_note, raw_note + 0x24, data);

	return data;
}

void tc183c230002_device::inject_key_event(uint16_t data)
{
	m_keybed_queue.push(data);
}
