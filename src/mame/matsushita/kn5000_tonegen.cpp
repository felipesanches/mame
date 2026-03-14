// license:GPL2+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Technics KN5000 Tone Generator (IC303 - TC183C230002)

    64-voice PCM wavetable synthesizer. The real chip is a custom Matsushita
    LSI that reads waveform data from 4x 32Mbit ROMs (IC304-IC307, 16MB total).

    Register-indirect interface: SubCPU writes a 16-bit register address to
    0x100000, then reads/writes data at 0x100002. P6.7 GPIO acts as chip-select
    strobe (active low during address phase).

    Waveform ROM format (per IC307 analysis):
      - 198-entry index table at offset 0 (4 bytes each)
      - Parameter records (key zone definitions)
      - Signed 16-bit LE PCM data starting at ~0x1A30

    Each ROM chip is 4MB. The combined 16MB region is loaded as "waveform":
      IC304 at offset 0x000000
      IC305 at offset 0x400000
      IC306 at offset 0x800000
      IC307 at offset 0xC00000

***************************************************************************/

#include "emu.h"
#include "kn5000_tonegen.h"

#include <algorithm>

// Logging
#define LOG_REG_W    (1U << 1)
#define LOG_KEY      (1U << 2)
#define LOG_VOICE    (1U << 3)
#define LOG_GLOBAL   (1U << 4)

#define VERBOSE (0)
#include "logmacro.h"

DEFINE_DEVICE_TYPE(KN5000_TONEGEN, kn5000_tonegen_device, "kn5000_tonegen", "KN5000 Tone Generator")


kn5000_tonegen_device::kn5000_tonegen_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, KN5000_TONEGEN, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, m_addr_latch(0)
	, m_stream(nullptr)
	, m_waveform_region_tag("waveform")
	, m_waveform_data(nullptr)
	, m_waveform_size(0)
{
}


void kn5000_tonegen_device::device_start()
{
	// Create stereo output stream at 48kHz (matching DAC sample rate)
	m_stream = stream_alloc(0, 2, 48000);

	// Resolve waveform ROM region
	memory_region *wave_region = machine().root_device().memregion(m_waveform_region_tag);
	if (wave_region)
	{
		m_waveform_data = wave_region->base();
		m_waveform_size = wave_region->bytes();
	}
	else
	{
		m_waveform_data = nullptr;
		m_waveform_size = 0;
	}

	// Parse waveform index tables from each ROM chip (IC304-IC307)
	// Each chip has 198 entries at offset 0, 4 bytes each
	if (m_waveform_data && m_waveform_size >= 0x1000000)
	{
		// Use IC307 index table (offset 0xC00000) as default
		const uint8_t *idx = m_waveform_data + 0xC00000;
		for (int i = 0; i < NUM_INDEX_ENTRIES; i++)
		{
			m_wave_index[i].param_ptr   = idx[i * 4 + 0] | (idx[i * 4 + 1] << 8);
			m_wave_index[i].wave_offset = idx[i * 4 + 2] | (idx[i * 4 + 3] << 8);
		}
	}

	// Save state
	save_item(NAME(m_addr_latch));
	save_item(NAME(m_global_regs));
	for (int i = 0; i < NUM_VOICES; i++)
	{
		save_item(NAME(m_voice[i].regs), i);
		save_item(NAME(m_voice[i].active), i);
		save_item(NAME(m_voice[i].key_on), i);
		save_item(NAME(m_voice[i].wave_offset), i);
		save_item(NAME(m_voice[i].wave_start), i);
		save_item(NAME(m_voice[i].wave_length), i);
		save_item(NAME(m_voice[i].pitch_step), i);
		save_item(NAME(m_voice[i].volume_l), i);
		save_item(NAME(m_voice[i].volume_r), i);
		save_item(NAME(m_voice[i].release_counter), i);
		save_item(NAME(m_voice[i].hold_counter), i);
	}
}


void kn5000_tonegen_device::device_reset()
{
	m_addr_latch = 0;
	std::fill(std::begin(m_global_regs), std::end(m_global_regs), 0);

	for (int i = 0; i < NUM_VOICES; i++)
		m_voice[i].reset();

	// Clear keybed queue
	while (!m_keybed_queue.empty())
		m_keybed_queue.pop();
}


//-----------------------------------------------------------------------
// Register-indirect interface
//-----------------------------------------------------------------------

void kn5000_tonegen_device::addr_w(uint16_t data)
{
	m_addr_latch = data;
}


void kn5000_tonegen_device::data_w(uint16_t data)
{
	m_stream->update();

	uint16_t addr = m_addr_latch;

	// Global registers: 0x0200-0x020F, 0x0C00-0x0C0F, 0x0E00
	if ((addr & 0xFF00) == 0x0200 || (addr & 0xFF00) == 0x0C00 || (addr & 0xFF00) == 0x0E00)
	{
		int idx = -1;
		if ((addr & 0xFF00) == 0x0200)
			idx = addr & 0x0F;
		else if ((addr & 0xFF00) == 0x0C00)
			idx = 6 + (addr & 0x0F);
		else if (addr == 0x0E00)
			idx = 12;

		if (idx >= 0 && idx < NUM_GLOBAL_REGS)
		{
			m_global_regs[idx] = data;
			LOGMASKED(LOG_GLOBAL, "tonegen: global reg 0x%04X = 0x%04X\n", addr, data);
		}
		return;
	}

	// Per-voice registers
	int ch   = addr & REG_CHANNEL_MASK;
	int bank = (addr >> REG_BANK_SHIFT) & REG_BANK_MASK;
	int group = addr >> REG_GROUP_SHIFT;

	if (ch >= NUM_VOICES)
		return;

	// Map group+bank to register index (32 regs = 8 groups x 4 banks)
	// Groups: 0x00, 0x01, 0x04, 0x05, 0x06, 0x08, 0x09, 0x0A
	static const int group_map[] = { 0, 1, -1, -1, 2, 3, 4, -1, 5, 6, 7, -1, -1, -1, -1, -1 };
	int gi = (group < 16) ? group_map[group] : -1;
	if (gi < 0)
	{
		LOGMASKED(LOG_REG_W, "tonegen: voice %d unknown group 0x%02X bank %d = 0x%04X (addr=0x%04X)\n",
			ch, group, bank, data, addr);
		return;
	}

	int reg_idx = gi * 4 + bank;
	if (reg_idx >= voice_t::NUM_REGS)
		return;

	m_voice[ch].regs[reg_idx] = data;
	LOGMASKED(LOG_REG_W, "tonegen: voice %d reg[%d] (g%d.b%d) = 0x%04X\n", ch, reg_idx, group, bank, data);

	// Voice control register (group 0, bank 0) — key on/off
	if (group == 0 && bank == 0)
	{
		if (data == 0x7E00)
		{
			// Idle / key off
			process_key_off(ch);
		}
		else if (data & 0x8000)
		{
			// Key on (bit 15 = active flag)
			process_key_on(ch);
		}
	}

	// Waveform pointer latch: group 0, bank 2 with bit 15 SET triggers load,
	// then bit 15 CLEAR finalizes. We resolve on the SET strobe.
	if (group == 0 && bank == 2 && (data & 0x8000))
		resolve_waveform(ch);

	// Pitch registers (group 1)
	if (group == 1)
		update_pitch(ch);

	// Volume/pan registers (group 4 for pan, group 8 for volume)
	if (group == 4 || group == 8)
		update_voice_params(ch);
}


uint16_t kn5000_tonegen_device::data_r()
{
	// Read-back: voice control state
	uint16_t addr = m_addr_latch;
	int ch = addr & REG_CHANNEL_MASK;
	int bank = (addr >> REG_BANK_SHIFT) & REG_BANK_MASK;
	int group = addr >> REG_GROUP_SHIFT;

	if (ch < NUM_VOICES && group == 0 && bank == 0)
	{
		// Return voice status: 0x8100 if key-on or still in hold phase, 0x7E00 if idle
		const voice_t &v = m_voice[ch];
		return (v.key_on || v.hold_counter > 0) ? 0x8100 : 0x7E00;
	}

	return 0;
}


//-----------------------------------------------------------------------
// Keyboard input interface
//-----------------------------------------------------------------------

uint16_t kn5000_tonegen_device::kbd_status_r()
{
	return m_keybed_queue.empty() ? 0x0000 : 0x0001;
}


uint16_t kn5000_tonegen_device::kbd_data_r()
{
	if (m_keybed_queue.empty())
		return 0x0000;

	uint16_t data = m_keybed_queue.front();
	m_keybed_queue.pop();
	return data;
}


void kn5000_tonegen_device::push_keybed_event(uint16_t data)
{
	m_keybed_queue.push(data);
}


//-----------------------------------------------------------------------
// Voice parameter management
//-----------------------------------------------------------------------

void kn5000_tonegen_device::update_voice_params(int ch)
{
	voice_t &v = m_voice[ch];

	// Volume from register group 8, bank 0-1
	// reg[20] = group 8, bank 0 (volume main)
	// reg[21] = group 8, bank 1 (volume secondary)
	uint16_t vol_main = v.regs[20]; // group 8, bank 0

	// Firmware uses 0xFF00/0xFF80 for mute, lower values for louder
	// Invert: 0xFF00 → 0, 0x0000 → max volume
	int vol = 0xFF00 - (vol_main & 0xFF00);
	vol = (vol >> 8) & 0xFF; // 0-255

	// Pan from register group 4, bank 0 (reg[8])
	uint16_t pan_val = v.regs[8]; // group 4, bank 0
	int pan = (pan_val >> 8) & 0xFF; // 0-255, 128=center

	// Apply pan law (simple linear)
	int vol_l, vol_r;
	if (pan <= 128)
	{
		vol_l = vol;
		vol_r = (pan == 0) ? 0 : vol * pan / 128;
	}
	else
	{
		vol_l = vol * (255 - pan) / 127;
		vol_r = vol;
	}

	v.volume_l = int16_t(std::min(vol_l, 255) * 128); // scale to 0-32640
	v.volume_r = int16_t(std::min(vol_r, 255) * 128);
}


void kn5000_tonegen_device::update_pitch(int ch)
{
	voice_t &v = m_voice[ch];

	// Pitch from register group 1 (regs 4-7)
	// reg[4] = group 1, bank 0: pitch coarse (firmware init: 0x017F)
	// reg[5] = group 1, bank 1: pitch fine (firmware init: 0x7F7F)
	//
	// The tone generator chip uses these to set the playback rate.
	// Interpretation: reg[4] high byte = octave/coarse, low byte = note fraction.
	// The firmware's ToneGen_Calc_Pitch adds 0x24 (36) to MIDI note before
	// computing the pitch value, suggesting the register encodes a note number
	// offset from some base.
	//
	// For now, treat reg[4] as a 16-bit pitch increment where 0x0100 = native
	// sample rate (1.0x). This gives range 0x0001 (~1/256x) to 0xFFFF (~256x).
	uint16_t pitch_reg = v.regs[4]; // group 1, bank 0
	if (pitch_reg == 0)
	{
		v.pitch_step = 0x10000; // default: native rate
		return;
	}

	// Scale: reg[4] = 0x0100 → pitch_step = 0x10000 (1.0x native)
	// This maps each unit of reg[4] to 256 units of pitch_step
	v.pitch_step = uint32_t(pitch_reg) << 8;

	LOGMASKED(LOG_VOICE, "tonegen: voice %d pitch reg=0x%04X step=0x%08X\n",
		ch, pitch_reg, v.pitch_step);
}


void kn5000_tonegen_device::resolve_waveform(int ch)
{
	voice_t &v = m_voice[ch];

	// Waveform pointer from registers:
	// reg[1] = group 0, bank 1 (0x040): waveform pointer low
	// reg[2] = group 0, bank 2 (0x080): waveform pointer high (bit 15 = latch strobe)
	//
	// Together these form a waveform address. The exact encoding depends on
	// the hardware — for now use reg[2] bits 6:0 as a waveform index (0-127)
	// within the appropriate ROM chip, and reg[1] for fine addressing.
	uint16_t wave_lo = v.regs[1]; // group 0, bank 1
	uint16_t wave_hi = v.regs[2]; // group 0, bank 2

	// Extract waveform index from low 7 bits of wave_hi (bit 15 is strobe)
	int wave_idx = wave_hi & 0x7F;
	if (wave_idx >= NUM_INDEX_ENTRIES)
		wave_idx = 0;

	// Determine ROM chip from reg[1] or wave_hi bits 8-14
	// For now, only IC307 is dumped (offset 0xC00000). Use it for all lookups
	// until other ROMs are available.
	uint32_t chip_base = 0xC00000; // IC307

	// Read index entry from chip's index table
	if (m_waveform_data && m_waveform_size > chip_base + NUM_INDEX_ENTRIES * 4)
	{
		const uint8_t *idx = m_waveform_data + chip_base;
		uint16_t wave_off_raw = idx[wave_idx * 4 + 2] | (idx[wave_idx * 4 + 3] << 8);
		uint32_t wave_byte_offset = uint32_t(wave_off_raw) * 16;

		v.wave_start = chip_base + wave_byte_offset;

		// Determine length from next index entry
		uint32_t next_off;
		if (wave_idx + 1 < NUM_INDEX_ENTRIES)
		{
			uint16_t next_raw = idx[(wave_idx + 1) * 4 + 2] | (idx[(wave_idx + 1) * 4 + 3] << 8);
			next_off = uint32_t(next_raw) * 16;
		}
		else
		{
			next_off = wave_byte_offset + 512;
		}

		if (next_off > wave_byte_offset)
			v.wave_length = (next_off - wave_byte_offset) / 2; // bytes to samples
		else
			v.wave_length = 256;

		LOGMASKED(LOG_VOICE, "tonegen: voice %d waveform idx=%d start=0x%06X len=%d (lo=0x%04X hi=0x%04X)\n",
			ch, wave_idx, v.wave_start, v.wave_length, wave_lo, wave_hi);
	}
	else
	{
		v.wave_start = 0;
		v.wave_length = 0;
	}
}


void kn5000_tonegen_device::process_key_on(int ch)
{
	voice_t &v = m_voice[ch];

	LOGMASKED(LOG_KEY, "tonegen: KEY ON voice %d\n", ch);

	v.key_on = true;
	v.active = true;
	v.wave_offset = 0;
	v.release_counter = 0;
	v.hold_counter = 0;

	// Waveform should already be resolved from the register strobe sequence
	// (resolve_waveform called when group 0, bank 2 written with bit 15 set).
	// If not yet resolved, try now as fallback.
	if (v.wave_length == 0)
		resolve_waveform(ch);

	// Update pitch from current registers
	update_pitch(ch);

	// Update volume/pan from current registers
	update_voice_params(ch);
}


void kn5000_tonegen_device::process_key_off(int ch)
{
	voice_t &v = m_voice[ch];

	LOGMASKED(LOG_KEY, "tonegen: KEY OFF voice %d\n", ch);

	v.key_on = false;

	// Start release envelope: ~50ms fade-out at 48kHz = 2400 samples
	v.release_counter = 2400;

	// Hold voice active for firmware status readback (2 seconds at 48kHz)
	v.hold_counter = 96000;
}


int16_t kn5000_tonegen_device::read_waveform_sample(uint32_t byte_offset) const
{
	if (!m_waveform_data || byte_offset + 1 >= m_waveform_size)
		return 0;

	// Signed 16-bit little-endian PCM
	return int16_t(m_waveform_data[byte_offset] | (m_waveform_data[byte_offset + 1] << 8));
}


//-----------------------------------------------------------------------
// Sound stream update — mix all active voices into stereo output
//-----------------------------------------------------------------------

void kn5000_tonegen_device::sound_stream_update(sound_stream &stream)
{
	for (int s = 0; s < stream.samples(); s++)
	{
		int32_t mix_l = 0;
		int32_t mix_r = 0;

		for (int ch = 0; ch < NUM_VOICES; ch++)
		{
			voice_t &v = m_voice[ch];
			if (!v.active || v.wave_length == 0)
				continue;

			// Handle hold timer (keeps voice "active" for firmware status queries)
			if (!v.key_on && v.hold_counter > 0)
			{
				v.hold_counter--;
				if (v.hold_counter == 0 && v.release_counter == 0)
				{
					v.active = false;
					continue;
				}
			}

			// Read current sample with linear interpolation (16.16 fixed point)
			uint32_t sample_pos = v.wave_offset >> 16;
			uint32_t frac = v.wave_offset & 0xFFFF;

			if (sample_pos >= v.wave_length)
			{
				// Loop back to start
				v.wave_offset = 0;
				sample_pos = 0;
				frac = 0;
			}

			uint32_t byte_pos = v.wave_start + sample_pos * 2;
			int32_t s0 = read_waveform_sample(byte_pos);

			// Linear interpolation with next sample
			int32_t s1;
			if (sample_pos + 1 < v.wave_length)
				s1 = read_waveform_sample(byte_pos + 2);
			else
				s1 = read_waveform_sample(v.wave_start); // wrap to loop start

			int32_t sample = s0 + ((s1 - s0) * int32_t(frac >> 1)) / 32768;

			// Apply release envelope
			if (v.release_counter > 0)
			{
				sample = sample * int32_t(v.release_counter) / 2400;
				v.release_counter--;
				if (v.release_counter == 0 && v.hold_counter == 0)
				{
					v.active = false;
				}
			}

			// Apply volume
			mix_l += (sample * v.volume_l) >> 15;
			mix_r += (sample * v.volume_r) >> 15;

			// Advance position
			v.wave_offset += v.pitch_step;
		}

		// Clip to 16-bit range and convert to float (-1.0 to 1.0)
		mix_l = std::clamp(mix_l, -32768, 32767);
		mix_r = std::clamp(mix_r, -32768, 32767);

		stream.put(0, s, sound_stream::sample_t(mix_l) / 32768.0f);
		stream.put(1, s, sound_stream::sample_t(mix_r) / 32768.0f);
	}
}
