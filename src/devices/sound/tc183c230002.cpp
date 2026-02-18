// license:GPL2+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Technics TC183C230002 Tone Generator (IC303)

    64-voice wavetable synthesizer used in the Technics SX-KN5000.

    See header for register layout documentation.

    Audio output: since the internal wavetable ROM is undumped, we generate
    sine waves at the correct MIDI pitch as a functional placeholder.
    Only voices triggered by actual keybed events produce sound — the
    firmware's init sequence (which key-ons all 64 voices) is silent.

    Register groups (from config address bits 11:8):
      0x00 — Voice control (bank 0: gate, bank 1: enable, bank 2: sustain, bank 3: FX routing)
      0x01 — Pitch (bank 0: note, bank 1: bend range, bank 2: fine tune)
      0x02 — Global config
      0x03 — Envelope
      0x04 — Filter (banks 0-3: cutoff/resonance)
      0x05 — FX send A
      0x06 — FX send B
      0x08 — Volume
      0x09 — Pan
      0x0A — FX send C
      0x0C — Global config 2
      0x0E — Global config 3

    Voice control bank 0 known values:
      0x8100 — Key on (start envelope)
      0x7E00 — Key off (release)
      0x1200 — Release trigger
      0x0000 — Voice off

***************************************************************************/

#include "emu.h"
#include "tc183c230002.h"

#include <cmath>

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

// Describe a voice control bank 0 value
static const char *ctrl_bank0_desc(uint16_t data)
{
	switch (data)
	{
	case 0x8100: return "KEY ON";
	case 0x7e00: return "KEY OFF";
	case 0x1200: return "RELEASE";
	case 0x0000: return "OFF";
	default:     return nullptr;
	}
}

//--------------------------------------------------------------------------
//  MIDI note to frequency conversion
//--------------------------------------------------------------------------

double tc183c230002_device::midi_note_to_freq(uint8_t note)
{
	// A4 (MIDI 69) = 440 Hz, equal temperament
	return 440.0 * pow(2.0, (note - 69.0) / 12.0);
}

//--------------------------------------------------------------------------
//  Pending note queue (circular buffer)
//--------------------------------------------------------------------------

void tc183c230002_device::push_pending_note(uint8_t midi_note, uint8_t velocity)
{
	if (m_pending_count >= MAX_PENDING_NOTES)
	{
		// Drop oldest entry
		m_pending_tail = (m_pending_tail + 1) % MAX_PENDING_NOTES;
		m_pending_count--;
	}
	m_pending_notes[m_pending_head].midi_note = midi_note;
	m_pending_notes[m_pending_head].velocity = velocity;
	m_pending_head = (m_pending_head + 1) % MAX_PENDING_NOTES;
	m_pending_count++;
}

bool tc183c230002_device::pop_pending_note(pending_note &out)
{
	if (m_pending_count <= 0)
		return false;
	out = m_pending_notes[m_pending_tail];
	m_pending_tail = (m_pending_tail + 1) % MAX_PENDING_NOTES;
	m_pending_count--;
	return true;
}

//--------------------------------------------------------------------------
//  Construction / lifecycle
//--------------------------------------------------------------------------

tc183c230002_device::tc183c230002_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, TC183C230002, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, m_config_addr(0)
	, m_pending_head(0)
	, m_pending_tail(0)
	, m_pending_count(0)
	, m_stream(nullptr)
{
}

void tc183c230002_device::device_start()
{
	m_stream = stream_alloc(0, 2, 48000);  // stereo, 48 kHz

	save_item(NAME(m_config_addr));
	save_item(NAME(m_regs));
	save_item(NAME(m_active_count));
	save_item(NAME(m_keybed_poll_count));
	save_item(NAME(m_pending_head));
	save_item(NAME(m_pending_tail));
	save_item(NAME(m_pending_count));
	for (int i = 0; i < MAX_PENDING_NOTES; i++)
	{
		save_item(NAME(m_pending_notes[i].midi_note), i);
		save_item(NAME(m_pending_notes[i].velocity), i);
	}
	for (int i = 0; i < 64; i++)
	{
		save_item(NAME(m_voices[i].control), i);
		save_item(NAME(m_voices[i].pitch), i);
		save_item(NAME(m_voices[i].volume), i);
		save_item(NAME(m_voices[i].pan), i);
		save_item(NAME(m_voices[i].active), i);
		save_item(NAME(m_voices[i].midi_note), i);
		save_item(NAME(m_voices[i].velocity), i);
		save_item(NAME(m_voices[i].phase), i);
		save_item(NAME(m_voices[i].frequency), i);
		save_item(NAME(m_voices[i].env_level), i);
		save_item(NAME(m_voices[i].releasing), i);
	}
}

void tc183c230002_device::device_reset()
{
	m_config_addr = 0;
	std::fill(std::begin(m_regs), std::end(m_regs), 0);
	m_active_count = 0;
	m_keybed_poll_count = 0;
	m_pending_head = 0;
	m_pending_tail = 0;
	m_pending_count = 0;
	for (auto &v : m_voices)
	{
		v.control = 0;
		v.pitch = 0;
		v.volume = 0;
		v.pan = 0;
		v.active = false;
		v.midi_note = 0;
		v.velocity = 0;
		v.phase = 0.0;
		v.frequency = 0.0;
		v.env_level = 0.0;
		v.releasing = false;
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
	case 0x05: group_name = "fxA";    break;
	case 0x06: group_name = "fxB";    break;
	case 0x08: group_name = "vol";    break;
	case 0x09: group_name = "pan";    break;
	case 0x0a: group_name = "fxC";    break;
	default:   group_name = nullptr;  break;
	}

	// Track voice state for group 0x00, bank 0 (control) writes only.
	// The firmware writes a sequence: 0x8100 (key-on) → 0xF0FF (playing) → 0x7E00 (key-off).
	// Only react to specific known gate values for audio; ignore transitional values like 0xF0FF.
	if (group == 0x00 && bank == 0 && channel < 64)
	{
		m_voices[channel].control = data;

		switch (data)
		{
		case 0x8100: // KEY ON
		{
			m_stream->update();
			bool was_active = m_voices[channel].active;
			m_voices[channel].active = true;

			if (!was_active)
				m_active_count++;

			// Try to associate with a pending keybed note
			pending_note pn;
			if (pop_pending_note(pn))
			{
				m_voices[channel].midi_note = pn.midi_note;
				m_voices[channel].velocity = pn.velocity;
				m_voices[channel].frequency = midi_note_to_freq(pn.midi_note);
				m_voices[channel].env_level = 1.0;
				m_voices[channel].phase = 0.0;
				m_voices[channel].releasing = false;

				LOGMASKED(LOG_VOICE, "voice %d: key-on note=%s (MIDI %d) vel=%d freq=%.1f Hz (active: %d/64)\n",
					channel, midi_note_name(pn.midi_note), pn.midi_note,
					pn.velocity, m_voices[channel].frequency, m_active_count);
			}
			else if (m_voices[channel].pitch != 0)
			{
				// No keybed note — derive pitch from the pitch register.
				// Heuristic: upper byte of pitch reg + 24 ≈ MIDI note number.
				// Based on observed: MIDI 60 (C4) → pitch reg 0x244E (upper byte 0x24=36, 36+24=60).
				uint8_t estimated_note = ((m_voices[channel].pitch >> 8) & 0xff) + 24;
				if (estimated_note > 127) estimated_note = 127;

				m_voices[channel].midi_note = estimated_note;
				m_voices[channel].velocity = 200;  // default velocity for programmatic notes
				m_voices[channel].frequency = midi_note_to_freq(estimated_note);
				m_voices[channel].env_level = 1.0;
				m_voices[channel].phase = 0.0;
				m_voices[channel].releasing = false;

				LOGMASKED(LOG_VOICE, "voice %d: key-on (from pitch reg 0x%04X) note=%s (MIDI %d) freq=%.1f Hz (active: %d/64)\n",
					channel, m_voices[channel].pitch, midi_note_name(estimated_note),
					estimated_note, m_voices[channel].frequency, m_active_count);
			}
			else
			{
				// No pending note and no pitch register — truly silent
				LOGMASKED(LOG_VOICE, "voice %d: key-on (no note source, silent) vol=0x%04X (active: %d/64)\n",
					channel, m_voices[channel].volume, m_active_count);
			}
			break;
		}

		case 0x7e00: // KEY OFF — release envelope
		case 0x1200: // RELEASE
			m_stream->update();
			if (m_voices[channel].active)
			{
				m_voices[channel].active = false;
				if (m_active_count > 0)
					m_active_count--;
			}
			m_voices[channel].releasing = true;
			LOGMASKED(LOG_VOICE, "voice %d: %s (active: %d/64)\n", channel,
				(data == 0x7e00) ? "key-off" : "release", m_active_count);
			break;

		case 0x0000: // OFF — immediate silence
			m_stream->update();
			if (m_voices[channel].active)
			{
				m_voices[channel].active = false;
				if (m_active_count > 0)
					m_active_count--;
			}
			m_voices[channel].env_level = 0.0;
			m_voices[channel].releasing = false;
			LOGMASKED(LOG_VOICE, "voice %d: off (active: %d/64)\n", channel, m_active_count);
			break;

		default:
			// Transitional values (e.g. 0xF0FF) — no audio state change
			LOGMASKED(LOG_VOICE, "voice %d: ctrl=0x%04X (active: %d/64)\n",
				channel, data, m_active_count);
			break;
		}
	}

	// Track pitch for group 0x01, bank 0 only (main pitch value).
	// Used as fallback for frequency when no keybed note is pending (e.g. feature demo, MIDI).
	if (group == 0x01 && bank == 0 && channel < 64)
		m_voices[channel].pitch = data;

	// Track volume for group 0x08, bank 0 only (main voice volume).
	// Banks 1-3 are sub-parameters (e.g. FX send levels) — not used for audio gain.
	if (group == 0x08 && bank == 0 && channel < 64)
	{
		m_stream->update();
		m_voices[channel].volume = data;
	}

	// Track pan for group 0x09, bank 0 only (main pan position).
	if (group == 0x09 && bank == 0 && channel < 64)
	{
		m_stream->update();
		m_voices[channel].pan = data;
	}

	// Log register writes with semantic descriptions
	if (group == 0x02 || group == 0x0c || group == 0x0e)
	{
		LOGMASKED(LOG_REG, "reg global[0x%04X] = 0x%04X\n",
			m_config_addr, data);
	}
	else if (group_name)
	{
		// Build description suffix for known value meanings
		char desc[64] = "";
		if (group == 0x00 && bank == 0)
		{
			char const *d = ctrl_bank0_desc(data);
			if (d) snprintf(desc, sizeof(desc), " (%s)", d);
		}
		else if (group == 0x00 && bank == 1)
		{
			if (data == 0x0002) snprintf(desc, sizeof(desc), " (ENABLED)");
			else if (data == 0x0000) snprintf(desc, sizeof(desc), " (DISABLED)");
		}
		else if (group == 0x00 && bank == 2)
		{
			if (data == 0x8000) snprintf(desc, sizeof(desc), " (SUSTAIN ON)");
			else if (data == 0x0000) snprintf(desc, sizeof(desc), " (SUSTAIN OFF)");
		}
		else if (group == 0x08)
		{
			// Volume: high byte is level (0xFF=max), bit 7 of low byte is enable
			uint8_t level = (data >> 8) & 0xff;
			snprintf(desc, sizeof(desc), " (%d%%)", level * 100 / 255);
		}

		LOGMASKED(LOG_REG, "reg[%s bank=%d ch=%d] = 0x%04X%s\n",
			group_name, bank, channel, data, desc);
	}
	else
	{
		LOGMASKED(LOG_REG, "reg[grp=0x%02X bank=%d ch=%d] = 0x%04X\n",
			group, bank, channel, data);
	}
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
	m_keybed_poll_count++;
	// Log every 10000th poll as heartbeat to verify SubCPU main loop is running
	if ((m_keybed_poll_count % 10000) == 0)
		LOGMASKED(LOG_KEYBED, "@%10.6f keybed poll heartbeat: %u polls, queue=%s\n",
			machine().time().as_double(), m_keybed_poll_count, m_keybed_queue.empty() ? "empty" : "has data");
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
	{
		LOGMASKED(LOG_KEYBED, "keybed read: note-on %s (MIDI %d) vel=%d\n",
			midi_note_name(midi_note), midi_note, velocity);

		// Queue this note for association with the next voice key-on
		push_pending_note(midi_note, velocity);
	}
	else
	{
		LOGMASKED(LOG_KEYBED, "keybed read: note-off %s (MIDI %d)\n",
			midi_note_name(midi_note), midi_note);
	}

	return data;
}

void tc183c230002_device::inject_key_event(uint16_t data)
{
	m_keybed_queue.push(data);
}

//--------------------------------------------------------------------------
//  Audio stream generation
//--------------------------------------------------------------------------

void tc183c230002_device::sound_stream_update(sound_stream &stream)
{
	constexpr double MASTER_GAIN = 0.05;            // prevent clipping with many voices
	constexpr double RELEASE_RATE = 1.0 / (48000.0 * 0.125); // ~125ms release time at 48kHz

	int const num_samples = stream.samples();
	double const sample_rate = double(stream.sample_rate());
	double const phase_inc_scale = 2.0 * M_PI / sample_rate;

	for (int s = 0; s < num_samples; s++)
	{
		double mix_l = 0.0;
		double mix_r = 0.0;

		for (int v = 0; v < 64; v++)
		{
			voice_state &voice = m_voices[v];

			if (voice.env_level < 0.001)
				continue;

			// Generate sine wave
			double sample = sin(voice.phase);

			// Advance phase
			voice.phase += voice.frequency * phase_inc_scale;
			if (voice.phase >= 2.0 * M_PI)
				voice.phase -= 2.0 * M_PI;

			// Apply release envelope
			if (voice.releasing)
			{
				voice.env_level -= RELEASE_RATE;
				if (voice.env_level < 0.0)
					voice.env_level = 0.0;
			}

			// Apply velocity (0-255 normalized to 0.0-1.0)
			double vel_gain = voice.velocity / 255.0;

			// Apply volume register (high byte is level 0-255)
			double vol_gain = ((voice.volume >> 8) & 0xff) / 255.0;

			// Combine gains
			double gain = sample * voice.env_level * vel_gain * vol_gain * MASTER_GAIN;

			// Apply pan (register value: 0=hard left, 0x8000=center, 0xFFFF=hard right)
			// Use equal-power panning
			double pan_pos = voice.pan / 65535.0;  // 0.0 = left, 1.0 = right
			double pan_l = cos(pan_pos * M_PI * 0.5);
			double pan_r = sin(pan_pos * M_PI * 0.5);

			mix_l += gain * pan_l;
			mix_r += gain * pan_r;
		}

		stream.put_clamp(0, s, mix_l);
		stream.put_clamp(1, s, mix_r);
	}
}
