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

***************************************************************************/

#ifndef MAME_SOUND_TC183C230002_H
#define MAME_SOUND_TC183C230002_H

#pragma once

#include <queue>

class tc183c230002_device : public device_t
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

private:
	// Per-voice state tracking (64 voices, 6-bit channel index)
	struct voice_state {
		uint16_t control;    // group 0x00 register value (key-on/idle/transition)
		uint16_t volume;     // group 0x08 register value
		bool active;         // derived: control == 0x8100
	};

	// Config register state
	uint16_t m_config_addr;
	uint16_t m_regs[4096];   // Indexed by 12-bit address

	// Voice state
	voice_state m_voices[64];
	uint8_t m_active_count;  // number of currently active voices

	// Keybed event queue (HLE)
	std::queue<uint16_t> m_keybed_queue;

	// Diagnostic: track keybed poll rate
	uint32_t m_keybed_poll_count;
};

DECLARE_DEVICE_TYPE(TC183C230002, tc183c230002_device)

#endif // MAME_SOUND_TC183C230002_H
