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
#define LOG_VOICE  (1U << 3)   // Voice state transitions

#define VERBOSE (LOG_REG | LOG_KEYBED | LOG_VOICE)
#include "logmacro.h"

DEFINE_DEVICE_TYPE(TC183C230002, tc183c230002_device, "tc183c230002", "TC183C230002 Tone Generator")

// MIDI note number to name (octave-3 convention: MIDI 60 = C4)
static const char *midi_note_name(uint8_t note)
{
	static const char *const NAMES[] = {
		"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
	};
	static char buf[8];
	if (note > 127) return "?";
	snprintf(buf, sizeof(buf), "%s%d", NAMES[note % 12], (note / 12) - 1);
	return buf;
}

tc183c230002_device::tc183c230002_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, TC183C230002, tag, owner, clock)
	, m_config_addr(0)
{
}

void tc183c230002_device::device_start()
{
	save_item(NAME(m_config_addr));
	save_item(NAME(m_regs));
	save_item(NAME(m_active_count));
	for (int i = 0; i < 64; i++)
	{
		save_item(NAME(m_voices[i].control), i);
		save_item(NAME(m_voices[i].volume), i);
		save_item(NAME(m_voices[i].active), i);
	}
}

void tc183c230002_device::device_reset()
{
	m_config_addr = 0;
	std::fill(std::begin(m_regs), std::end(m_regs), 0);
	m_active_count = 0;
	for (auto &v : m_voices)
	{
		v.control = 0;
		v.volume = 0;
		v.active = false;
	}
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

	// Register group names for semantic logging
	char const *group_name;
	switch (group)
	{
	case 0x00: group_name = "ctrl";   break;
	case 0x01: group_name = "pitch";  break;
	case 0x03: group_name = "env";    break;
	case 0x04: group_name = "filter"; break;
	case 0x08: group_name = "vol";    break;
	case 0x09: group_name = "pan";    break;
	default:   group_name = nullptr;  break;
	}

	// Track voice state for group 0x00 (control) writes
	if (group == 0x00 && channel < 64)
	{
		bool was_active = m_voices[channel].active;
		m_voices[channel].control = data;
		m_voices[channel].active = (data == 0x8100);

		if (!was_active && m_voices[channel].active)
		{
			m_active_count++;
			LOGMASKED(LOG_VOICE, "voice %d: key-on vol=0x%04X (active: %d/64)\n",
				channel, m_voices[channel].volume, m_active_count);
		}
		else if (was_active && !m_voices[channel].active)
		{
			if (m_active_count > 0)
				m_active_count--;
			LOGMASKED(LOG_VOICE, "voice %d: %s (active: %d/64)\n", channel,
				(data == 0x7e00) ? "key-off" : (data == 0x1200) ? "release" : "off",
				m_active_count);
		}
	}

	// Track volume for group 0x08
	if (group == 0x08 && channel < 64)
		m_voices[channel].volume = data;

	// Log register writes
	if (group == 0x02 || group == 0x0c || group == 0x0e)
		LOGMASKED(LOG_REG, "reg global[0x%04X] = 0x%04X\n",
			m_config_addr, data);
	else if (group_name)
		LOGMASKED(LOG_REG, "reg[%s bank=%d ch=%d] = 0x%04X\n",
			group_name, bank, channel, data);
	else
		LOGMASKED(LOG_REG, "reg[grp=0x%02X bank=%d ch=%d] = 0x%04X\n",
			group, bank, channel, data);
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

	uint8_t midi_note = raw_note + 0x24;
	if (note_on)
		LOGMASKED(LOG_KEYBED, "keybed read: note-on %s (MIDI %d) vel=%d\n",
			midi_note_name(midi_note), midi_note, velocity);
	else
		LOGMASKED(LOG_KEYBED, "keybed read: note-off %s (MIDI %d)\n",
			midi_note_name(midi_note), midi_note);

	return data;
}

void tc183c230002_device::inject_key_event(uint16_t data)
{
	m_keybed_queue.push(data);
}
