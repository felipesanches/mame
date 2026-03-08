// license:GPL2+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Technics TC183C230002 Tone Generator (IC303)

    64-voice wavetable synthesizer used in the Technics SX-KN5000.
    Connected to Sub CPU via memory-mapped registers:
      0x100000 (config addr), 0x100002 (config data r/w)
      0x110000 (keybed data), 0x110002 (keybed status)

    Register layout (from SubCPU firmware analysis):
      Address bits: [11:8]=group, [7:6]=bank, [5:0]=channel
      Groups: 0x00=voice ctrl, 0x02=global A, 0x08=volume,
              0x0C=global B, 0x0E=global C

    Keybed interface: hardware key scanning of 61-key keyboard.
    Currently HLE'd — inject_key_event() queues note events.

    Audio output: sine wave placeholder (wavetable ROM undumped).
    MIDI note tracking via keybed events — only voices triggered
    by actual key presses produce sound (init key-ons are silent).

***************************************************************************/

#ifndef MAME_SOUND_TC183C230002_H
#define MAME_SOUND_TC183C230002_H

#pragma once

#include <queue>

class tc183c230002_device : public device_t, public device_sound_interface
{
public:
	tc183c230002_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// Memory-mapped register interface (SubCPU 0x100000-0x100003)
	void config_addr_w(uint16_t data);
	uint16_t config_addr_r();
	void config_data_w(uint16_t data);
	uint16_t config_data_r();

	// Keybed interface (SubCPU 0x110000-0x110003)
	uint16_t keyboard_data_r();
	uint16_t keyboard_status_r();

	// HLE: inject a key event from the driver's keybed scan timer
	// Format: (velocity << 8) | (raw_note | 0x80) for note-on
	//         (0xFF << 8) | raw_note for note-off
	void inject_key_event(uint16_t data);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void sound_stream_update(sound_stream &stream) override;

private:
	// Per-voice state tracking (64 voices, 6-bit channel index)
	struct voice_state {
		uint16_t control;    // group 0x00 register value (key-on/idle/transition)
		uint16_t pitch;      // group 0x01 bank 0 register value
		uint16_t volume;     // group 0x08 register value
		uint16_t pan;        // group 0x09 register value
		bool active;         // derived: control == 0x8100

		// Audio generation state
		uint8_t midi_note;   // MIDI note number (0 = inactive)
		uint8_t velocity;    // Note velocity (0-255)
		double phase;        // Oscillator phase accumulator (radians)
		double frequency;    // Note frequency in Hz
		double env_level;    // Envelope level (0.0 to 1.0)
		bool releasing;      // True when in release phase
		uint32_t hold_samples_remaining; // Minimum time to report voice as active after key-off
	};

	// Pending note from keybed (bridges keybed read to voice key-on)
	struct pending_note {
		uint8_t midi_note;
		uint8_t velocity;
	};

	// Config register state
	uint16_t m_config_addr;
	uint16_t m_regs[4096];   // Indexed by 12-bit address

	// Voice state
	voice_state m_voices[64];
	uint8_t m_active_count;  // number of currently active voices

	// Keybed event queue (HLE)
	std::queue<uint16_t> m_keybed_queue;

	// Pending notes queue (keybed note-on -> voice key-on bridge)
	static constexpr int MAX_PENDING_NOTES = 16;
	pending_note m_pending_notes[MAX_PENDING_NOTES];
	int m_pending_head;
	int m_pending_tail;
	int m_pending_count;

	// Init sequence tracking: first 64 pitch-fallback key-ons are the init
	// sequence (voices 0-63 cycled through), suppress audio for those.
	int m_init_keyon_count;

	// Audio stream
	sound_stream *m_stream;

	// Diagnostic: track keybed poll rate
	uint32_t m_keybed_poll_count;

	// Helpers
	void push_pending_note(uint8_t midi_note, uint8_t velocity);
	bool pop_pending_note(pending_note &out);
	static double midi_note_to_freq(uint8_t note);
};

DECLARE_DEVICE_TYPE(TC183C230002, tc183c230002_device)

#endif // MAME_SOUND_TC183C230002_H
