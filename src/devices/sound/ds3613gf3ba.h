// license:GPL2+
// copyright-holders:Felipe Sanches
/***************************************************************************

    DS3613GF-3BA Effect DSP (IC311)

    Memory-mapped DSP used in the Technics SX-KN5000 for digital effects
    (reverb, chorus, delay, etc). Connected to Sub CPU at:
      0x130000 (address register), 0x130002 (data register)

    Register layout: 4 channels x 32 registers = 128 bytes
      Channel 0: 0x00-0x1F   Channel 1: 0x20-0x3F
      Channel 2: 0x40-0x5F   Channel 3: 0x60-0x7F

    Known per-channel registers (from SubCPU firmware DSP_Write_Channel):
      0x10-0x17: Voice/effect parameters (8 slots)
      0x1F:      Channel config (written as 0x01 during init)

    Parameter naming: The firmware uses a category-based system where
    algorithm IDs map to categories, and each category defines which
    parameter name corresponds to each of the 8 register slots.
    The mapping table is at MainCPU ROM 0xE446DC (8 rows x 16 bytes).

    Categories:
      Row 0 (0x00): Distortion/Dynamics effects
      Row 2 (0x20): Rotary speaker (treble section)
      Row 3 (0x30): Rotary speaker (bass section)
      Row 4 (0x40): Delay/Chorus/Flanger/Phaser effects
      Row 6 (0x60): Reverb effects (REVERB TIME, PRE DELAY, HI DAMP, ER.LEVEL)

***************************************************************************/

#ifndef MAME_SOUND_DS3613GF3BA_H
#define MAME_SOUND_DS3613GF3BA_H

#pragma once

#include <vector>

// Per-algorithm category assignment (algo ID -> category row index)
// Used to look up which parameter names apply to registers 0x10-0x17
enum dsp_category : uint8_t
{
	DSP_CAT_NONE     = 0xff,  // No category (unused/reserved algo IDs)
	DSP_CAT_DISTDYN  = 0,     // Distortion, Overdrive, Fuzz, Compressor, etc.
	DSP_CAT_ROTARY_T = 2,     // Rotary speaker (treble controls)
	DSP_CAT_ROTARY_B = 3,     // Rotary speaker (bass controls)
	DSP_CAT_MODDELAY = 4,     // Delay, Chorus, Flanger, Phaser, Ensemble
	DSP_CAT_REVERB   = 6,     // All reverb algorithms (Room/Plate/Concert/Dark/Bright/Wave)
};

class ds3613gf3ba_device : public device_t
{
public:
	ds3613gf3ba_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// Memory-mapped register interface (SubCPU 0x130000/0x130002)
	void addr_w(uint16_t data);
	void data_w(uint16_t data);
	uint16_t data_r();

	// Parallel port interface (P7/PZ protocol from SubCPU)
	void parallel_command_w(uint8_t data);
	void parallel_data_w(uint8_t data);

	// Query current effect state (for debug/UI)
	uint8_t get_channel_algo(int ch) const { return (ch >= 0 && ch < 4) ? m_channel_algo[ch] : 0; }
	char const *get_channel_effect_name(int ch) const;
	char const *get_param_name(int ch, int slot) const;

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	void process_command();
	dsp_category algo_to_category(uint8_t algo_id) const;

	uint8_t m_addr;        // Current register address (0x00-0x7F)
	uint8_t m_regs[128];   // 4 channels x 32 registers

	// Per-channel effect tracking
	uint8_t m_channel_algo[4];    // Current DSP algorithm type per channel (2-11)
	uint8_t m_channel_program[4]; // Last DSP program module per channel (0xC8=reverb, 0x54=chorus)
	uint8_t m_pending_program;    // Most recently loaded program module (assigned to next channel)

	// Parallel port protocol state
	uint8_t m_par_cmd;                  // Current command byte
	std::vector<uint8_t> m_par_data;    // Data bytes for current command
};

// Effect type name table (from MainCPU ROM at 0xE32A7A, 100 entries)
extern char const *const DS3613GF3BA_EFFECT_TYPE_NAMES[];
extern const int DS3613GF3BA_EFFECT_TYPE_COUNT;

// Effect parameter name table (from MainCPU ROM at 0xE324C4, 85 entries; index 0 = spacer)
extern char const *const DS3613GF3BA_EFFECT_PARAM_NAMES[];
extern const int DS3613GF3BA_EFFECT_PARAM_COUNT;

DECLARE_DEVICE_TYPE(DS3613GF3BA, ds3613gf3ba_device)

#endif // MAME_SOUND_DS3613GF3BA_H
