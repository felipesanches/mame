// license:GPL2+
// copyright-holders:Felipe Sanches
/***************************************************************************

    DS3613GF-3BA Effect DSP (IC311)

    Memory-mapped effect DSP used in the Technics SX-KN5000.
    This is an HLE stub that logs all register accesses for protocol
    reverse engineering. No audio output is generated yet.

    See header for register layout documentation.

    Effect type/parameter name tables extracted from MainCPU ROM.
    These are used for semantic logging of effect configuration.

    Parallel port command protocol (from SubCPU firmware analysis):
      0x01 — Voice/parameter bulk write (variable length)
      0x02 — Coefficient/table upload (variable length)
      0x03 — End parameter block (latch pending writes)
      0x04 — DSP init/config (5 data bytes)
      0x09 — Status/mode register (2 data bytes)
      0x0C — Control register (3 data bytes)
      0x0F — Sync/timing marker (no data)
      0x10 — Reset (no data)
      0x30 — Register write (4 data bytes: 0, addr, value_hi, value_lo)

***************************************************************************/

#include "emu.h"
#include "ds3613gf3ba.h"

#define LOG_DSP      (1U << 1)   // Memory-mapped register writes
#define LOG_PARALLEL (1U << 2)   // Parallel port command/data

#define VERBOSE (LOG_DSP | LOG_PARALLEL)
#include "logmacro.h"

DEFINE_DEVICE_TYPE(DS3613GF3BA, ds3613gf3ba_device, "ds3613gf3ba", "DS3613GF-3BA Effect DSP")

ds3613gf3ba_device::ds3613gf3ba_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, DS3613GF3BA, tag, owner, clock)
	, m_addr(0)
	, m_par_cmd(0)
{
}

void ds3613gf3ba_device::device_start()
{
	save_item(NAME(m_addr));
	save_item(NAME(m_regs));
	save_item(NAME(m_channel_algo));
	save_item(NAME(m_par_cmd));
}

void ds3613gf3ba_device::device_reset()
{
	m_addr = 0;
	std::fill(std::begin(m_regs), std::end(m_regs), 0);
	std::fill(std::begin(m_channel_algo), std::end(m_channel_algo), 0);
	m_par_cmd = 0;
	m_par_data.clear();
}

void ds3613gf3ba_device::addr_w(uint16_t data)
{
	m_addr = data & 0x7f;
}

void ds3613gf3ba_device::data_w(uint16_t data)
{
	uint8_t val = data & 0xff;
	m_regs[m_addr] = val;

	int channel = (m_addr >> 5) & 3;
	int reg = m_addr & 0x1f;

	if (reg >= 0x10 && reg <= 0x17)
	{
		// Parameter register write — resolve to named parameter
		int slot = reg - 0x10;
		char const *param_name = get_param_name(channel, slot);
		char const *effect_name = get_channel_effect_name(channel);
		if (param_name)
			LOGMASKED(LOG_DSP, "ch%d [%s] %s = %d (0x%02X)\n",
				channel, effect_name ? effect_name : "?", param_name, val, val);
		else
			LOGMASKED(LOG_DSP, "ch%d [%s] param[%d] = %d (0x%02X)\n",
				channel, effect_name ? effect_name : "?", slot, val, val);
	}
	else if (reg == 0x1f)
	{
		LOGMASKED(LOG_DSP, "ch%d config = 0x%02X\n", channel, val);
	}
	else
	{
		LOGMASKED(LOG_DSP, "ch%d reg[0x%02X] = 0x%02X\n", channel, reg, val);
	}
}

uint16_t ds3613gf3ba_device::data_r()
{
	uint8_t val = m_regs[m_addr];
	LOGMASKED(LOG_DSP, "read reg[0x%02X] = 0x%02X\n", m_addr, val);
	return val;
}

//--------------------------------------------------------------------------
//  Parallel port interface (P7/PZ protocol from SubCPU GPIO)
//--------------------------------------------------------------------------

void ds3613gf3ba_device::parallel_command_w(uint8_t data)
{
	// New command starts — process any accumulated previous command
	if (!m_par_data.empty())
		process_command();

	m_par_cmd = data;
	m_par_data.clear();

	// Log no-data commands immediately with their meaning
	switch (data)
	{
	case 0x03:
		LOGMASKED(LOG_PARALLEL, "END BLOCK (latch pending writes)\n");
		break;
	case 0x0f:
		LOGMASKED(LOG_PARALLEL, "SYNC\n");
		break;
	case 0x10:
		LOGMASKED(LOG_PARALLEL, "RESET\n");
		break;
	default:
		// Commands that expect data will be decoded in process_command()
		break;
	}
}

void ds3613gf3ba_device::parallel_data_w(uint8_t data)
{
	m_par_data.push_back(data);
}

void ds3613gf3ba_device::process_command()
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
			int channel = (addr >> 5) & 3;
			int reg = addr & 0x1f;

			// Decode parameter name for registers 0x10-0x17
			if (reg >= 0x10 && reg <= 0x17)
			{
				int slot = reg - 0x10;
				char const *pname = get_param_name(channel, slot);
				char const *ename = get_channel_effect_name(channel);
				if (pname)
					LOGMASKED(LOG_PARALLEL, "REG WRITE ch%d [%s] %s = 0x%04X (%d)\n",
						channel, ename ? ename : "?", pname, value, value);
				else
					LOGMASKED(LOG_PARALLEL, "REG WRITE ch%d param[%d] = 0x%04X\n",
						channel, slot, value);
			}
			else
			{
				LOGMASKED(LOG_PARALLEL, "REG WRITE addr=0x%02X value=0x%04X (ch%d reg=0x%02X)\n",
					addr, value, channel, reg);
			}
		}
		else
		{
			LOGMASKED(LOG_PARALLEL, "REG WRITE (incomplete: %zu bytes)\n", m_par_data.size());
		}
		break;

	case 0x01: // Voice/parameter bulk write
		if (m_par_data.size() >= 2)
		{
			uint8_t mode = m_par_data[0];
			uint8_t param = m_par_data[1];
			if (mode == 0x01 && m_par_data.size() >= 7)
			{
				// Effect parameter block: [0x01, channel_base, offset, sub, p1, p2, p3]
				// channel_base: 0x40=ch0, 0x60=ch1, etc. (stride 0x20)
				int channel = (param >= 0x40) ? ((param - 0x40) >> 5) : -1;
				uint8_t offset = m_par_data[2];

				// Check for algorithm selection (offset 0x08 = algo config area)
				if (offset == 0x08 && m_par_data.size() >= 5 && channel >= 0 && channel < 4)
				{
					uint8_t algo_id = m_par_data[4];
					m_channel_algo[channel] = algo_id;
					char const *name = get_channel_effect_name(channel);
					LOGMASKED(LOG_PARALLEL, "ALGO SELECT ch%d = %d (%s)\n",
						channel, algo_id, name ? name : "unknown");
				}
				else if (offset == 0x00 && m_par_data.size() >= 6 && channel >= 0 && channel < 4)
				{
					// Register write within channel: [0x01, ch_base, 0x00, 0x00, reg, value, 0x00]
					uint8_t reg = m_par_data[4];
					uint8_t value = m_par_data[5];

					if (reg >= 0x10 && reg <= 0x17)
					{
						int slot = reg - 0x10;
						char const *pname = get_param_name(channel, slot);
						char const *ename = get_channel_effect_name(channel);
						if (pname)
							LOGMASKED(LOG_PARALLEL, "EFFECT PARAM ch%d [%s] %s = %d (0x%02X)\n",
								channel, ename ? ename : "?", pname, value, value);
						else
							LOGMASKED(LOG_PARALLEL, "EFFECT PARAM ch%d [%s] param[%d] = %d (0x%02X)\n",
								channel, ename ? ename : "?", slot, value, value);
					}
					else
					{
						LOGMASKED(LOG_PARALLEL, "EFFECT PARAM ch%d reg=0x%02X value=0x%02X\n",
							channel, reg, value);
					}
				}
				else
				{
					LOGMASKED(LOG_PARALLEL, "EFFECT PARAM ch%d offset=0x%02X [0x%02X 0x%02X 0x%02X 0x%02X]\n",
						channel, offset,
						m_par_data.size() > 3 ? m_par_data[3] : 0,
						m_par_data.size() > 4 ? m_par_data[4] : 0,
						m_par_data.size() > 5 ? m_par_data[5] : 0,
						m_par_data.size() > 6 ? m_par_data[6] : 0);
				}
			}
			else if (mode == 0x00)
			{
				// Voice/tone config data
				LOGMASKED(LOG_PARALLEL, "VOICE DATA index=0x%02X (%zu bytes)\n",
					param, m_par_data.size());
			}
			else
			{
				LOGMASKED(LOG_PARALLEL, "PARAM WRITE mode=0x%02X param=0x%02X (%zu bytes)\n",
					mode, param, m_par_data.size());
			}
		}
		else
		{
			LOGMASKED(LOG_PARALLEL, "PARAM WRITE (%zu bytes)\n", m_par_data.size());
		}
		break;

	case 0x02: // Coefficient/table upload
		if (m_par_data.size() >= 2)
		{
			LOGMASKED(LOG_PARALLEL, "COEFF UPLOAD header=[0x%02X 0x%02X] (%zu bytes)\n",
				m_par_data[0], m_par_data[1], m_par_data.size());
		}
		else
		{
			LOGMASKED(LOG_PARALLEL, "COEFF UPLOAD (%zu bytes)\n", m_par_data.size());
		}
		break;

	case 0x04: // Init/config (5 bytes)
		{
			std::string cfg_str;
			for (size_t i = 0; i < m_par_data.size(); i++)
			{
				if (i) cfg_str += ' ';
				char buf[8];
				snprintf(buf, sizeof(buf), "0x%02X", m_par_data[i]);
				cfg_str += buf;
			}
			LOGMASKED(LOG_PARALLEL, "INIT CONFIG [%s]\n", cfg_str.c_str());
		}
		break;

	case 0x09: // Status/mode (2 bytes)
		if (m_par_data.size() >= 2)
		{
			uint16_t value = (uint16_t(m_par_data[0]) << 8) | m_par_data[1];
			LOGMASKED(LOG_PARALLEL, "STATUS/MODE value=0x%04X\n", value);
		}
		else
		{
			LOGMASKED(LOG_PARALLEL, "STATUS/MODE (%zu bytes)\n", m_par_data.size());
		}
		break;

	case 0x0c: // Control (3 bytes)
		if (m_par_data.size() >= 3)
			LOGMASKED(LOG_PARALLEL, "CONTROL [0x%02X 0x%02X 0x%02X]\n",
				m_par_data[0], m_par_data[1], m_par_data[2]);
		else
			LOGMASKED(LOG_PARALLEL, "CONTROL (%zu bytes)\n", m_par_data.size());
		break;

	default:
		{
			// Unknown command - hex dump
			std::string hex_str;
			for (size_t i = 0; i < m_par_data.size() && i < 16; i++)
			{
				if (i) hex_str += ' ';
				char buf[8];
				snprintf(buf, sizeof(buf), "0x%02X", m_par_data[i]);
				hex_str += buf;
			}
			if (m_par_data.size() > 16)
				hex_str += " ...";
			LOGMASKED(LOG_PARALLEL, "CMD 0x%02X (%zu bytes) [%s]\n",
				m_par_cmd, m_par_data.size(), hex_str.c_str());
		}
		break;
	}
}

//--------------------------------------------------------------------------
//  Algorithm-to-category mapping and parameter name resolution
//--------------------------------------------------------------------------

// Per-category parameter name indices (from MainCPU ROM 0xE446DC, 8 rows x 8 slots)
// Index 0xFF means the parameter slot is unused for this category.
// Other values index into DS3613GF3BA_EFFECT_PARAM_NAMES[].
static const uint8_t s_category_param_indices[8][8] = {
	// Row 0: Distortion/Dynamics (algo IDs 32-39)
	{ 0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x00 },
	// Row 1: (unused)
	{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
	// Row 2: Rotary speaker treble (algo ID 53)
	{ 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0xff, 0x0f },
	// Row 3: Rotary speaker bass (algo ID 53, continued)
	{ 0x10, 0xff, 0x11, 0xff, 0x12, 0x13, 0x15, 0xff },
	// Row 4: Delay/Chorus/Flanger/Phaser (algo IDs 1-6, 9-11)
	{ 0x17, 0x18, 0x19, 0x1a, 0x1b, 0xff, 0x1c, 0x1d },
	// Row 5: (unused)
	{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
	// Row 6: Reverb (algo IDs 8, 16-27)
	{ 0x22, 0x23, 0x24, 0x25, 0xff, 0xff, 0xff, 0xff },
	// Row 7: (unused)
	{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
};

dsp_category ds3613gf3ba_device::algo_to_category(uint8_t algo_id) const
{
	switch (algo_id)
	{
	// Reverb algorithms (IDs 8, 16-27)
	case  8: // GATED REVERB
	case 16: case 17: case 18: case 19: // ROOM/PLATE
	case 20: case 21: case 22: case 23: // CONCERT/DARK
	case 24: case 25: case 26: case 27: // BRIGHT/WAVE
		return DSP_CAT_REVERB;

	// Modulation/delay (IDs 1-6, 9-11, 15)
	case  1: case  2: case  3: case  4: case  5: case  6: // Chorus/Enhancer/Flanger/Phaser/Ensemble
	case  9: case 10: case 11: // Single/MultiTap/Modulation Delay
	case 15: // ROCK ROTARY (uses delay/mod category)
		return DSP_CAT_MODDELAY;

	// Distortion/Dynamics (IDs 32-39)
	case 32: case 33: case 34: case 35: // Distortion/Overdrive/Fuzz/Exciter
	case 36: case 37: case 38: case 39: // Compressor/SlowAttacker/NoiseFlanger/PEQ
		return DSP_CAT_DISTDYN;

	// Rotary speaker (ID 53)
	case 53:
		return DSP_CAT_ROTARY_T;

	default:
		return DSP_CAT_NONE;
	}
}

char const *ds3613gf3ba_device::get_channel_effect_name(int ch) const
{
	if (ch < 0 || ch >= 4)
		return nullptr;

	uint8_t algo = m_channel_algo[ch];
	if (algo < DS3613GF3BA_EFFECT_TYPE_COUNT && DS3613GF3BA_EFFECT_TYPE_NAMES[algo])
		return DS3613GF3BA_EFFECT_TYPE_NAMES[algo];

	return nullptr;
}

char const *ds3613gf3ba_device::get_param_name(int ch, int slot) const
{
	if (ch < 0 || ch >= 4 || slot < 0 || slot >= 8)
		return nullptr;

	dsp_category cat = algo_to_category(m_channel_algo[ch]);
	if (cat == DSP_CAT_NONE || cat >= 8)
		return nullptr;

	uint8_t name_idx = s_category_param_indices[cat][slot];
	if (name_idx == 0xff)
		return nullptr;

	if (name_idx < DS3613GF3BA_EFFECT_PARAM_COUNT)
		return DS3613GF3BA_EFFECT_PARAM_NAMES[name_idx];

	return nullptr;
}

//--------------------------------------------------------------------------
//  Effect type name table (from MainCPU ROM at 0xE32A7A, 128 entries)
//--------------------------------------------------------------------------

char const *const DS3613GF3BA_EFFECT_TYPE_NAMES[] = {
	"NO OPERATION",       // 0
	"CHORUS",             // 1
	"MODULATED CHORUS",   // 2
	"ENHANCER",           // 3
	"FLANGER",            // 4
	"PHASER",             // 5
	"ENSEMBLE",           // 6
	nullptr,              // 7
	"GATED REVERB",       // 8
	"SINGLE DELAY",       // 9
	"MULTI TAP DELAY",    // 10
	"MODULATION DELAY",   // 11
	nullptr,              // 12
	nullptr,              // 13
	nullptr,              // 14
	"ROCK ROTARY",        // 15
	"ROOM REVERB 1",      // 16
	"ROOM REVERB 2",      // 17
	"PLATE REVERB 1",     // 18
	"PLATE REVERB 2",     // 19
	"CONCERT REVERB 1",   // 20
	"CONCERT REVERB 2",   // 21
	"DARK REVERB 1",      // 22
	"DARK REVERB 2",      // 23
	"BRIGHT REVERB 1",    // 24
	"BRIGHT REVERB 2",    // 25
	"WAVE REVERB 1",      // 26
	"WAVE REVERB 2",      // 27
	nullptr,              // 28
	nullptr,              // 29
	nullptr,              // 30
	nullptr,              // 31
	"DISTORTION",         // 32
	"OVERDRIVE",          // 33
	"FUZZ",               // 34
	"EXCITER",            // 35
	"COMPRESSOR",         // 36
	"SLOW ATTACKER",      // 37
	"NOISE FLANGER",      // 38
	"PARAMETRIC EQ",      // 39
	nullptr,              // 40
	nullptr,              // 41
	nullptr,              // 42
	nullptr,              // 43
	"CEL",                // 44
	"CELM",               // 45
	nullptr,              // 46
	nullptr,              // 47
	"AUTO PAN",           // 48
	"PITCH SHIFTER",      // 49
	"VIBRATO",            // 50
	"PEDAL WAH",          // 51
	"AUTO WAH",           // 52
	"ROTARY SPEAKER",     // 53
	"RING MODULATOR",     // 54
	"HARS EFFECT",        // 55
	"MIX UP",             // 56
	"STANDARD",           // 57
	"PERCUSSIVE",         // 58
	"SYMPHONIC",          // 59
	"DEEP SPACE",         // 60
	nullptr,              // 61
	nullptr,              // 62
	"STRING",             // 63
	"S.DELAY+CHORUS",     // 64
	"S.DELAY+S.DELAY",    // 65
	"S.DELAY+FLANGER",    // 66
	"S.DELAY+VIBRATO",    // 67
	"S.DELAY+PHASER",     // 68
	"PEDAL WAH+DELAY",    // 69
	"AUTO WAH+S.DELAY",   // 70
	"PEQ+CHORUS",         // 71
	"PEQ+S.DELAY",        // 72
	"PEQ+FLANGER",        // 73
	"PEQ+VIBRATO",        // 74
	"PEQ+COMPRESSOR",     // 75
	nullptr,              // 76
	nullptr,              // 77
	nullptr,              // 78
	"GEQ",                // 79
	"DS_D",               // 80
	"OVER_D",             // 81
	nullptr,              // 82
	nullptr,              // 83
	nullptr,              // 84
	nullptr,              // 85
	nullptr,              // 86
	nullptr,              // 87
	"ROOM",               // 88
	"KARAOKE",            // 89
	"BATH ROOM",          // 90
	"STAGE",              // 91
	nullptr,              // 92
	nullptr,              // 93
	nullptr,              // 94
	nullptr,              // 95
	"PEQ+COMPR+DIST",     // 96
	"PEQ+COMPR+OVERDR",   // 97
	"PEQ+DIST+DELAY",     // 98
	"PEQ+OVERDR+DELAY",   // 99
};

const int DS3613GF3BA_EFFECT_TYPE_COUNT = std::size(DS3613GF3BA_EFFECT_TYPE_NAMES);

//--------------------------------------------------------------------------
//  Effect parameter name table (from MainCPU ROM at 0xE324C4, 85 entries)
//  Index 0 is a blank spacer in the ROM; indices match ROM addressing directly.
//--------------------------------------------------------------------------

char const *const DS3613GF3BA_EFFECT_PARAM_NAMES[] = {
	"",                   // 0 (blank spacer — ROM index 0 means "no parameter name")
	"VOLUME",             // 1
	"VOLUME",             // 2
	"REV SEND",           // 3
	"DRIVE",              // 4
	"ADJUST",             // 5
	"EMPHASIS GAIN",      // 6
	"DEPTH",              // 7
	"LFO SPEED",          // 8
	"SLOW LFO SPEED",     // 9
	"FAST LFO BALANCE",   // 10
	"RESONANCE",          // 11
	"MANUAL",             // 12
	"SLOW/FAST",          // 13
	"TREBLE FAST",        // 14
	"SLOW",               // 15
	"WIND UP",            // 16
	"WIND DOWN",          // 17
	"BASS FAST",          // 18
	"BASS SLOW",          // 19
	"VOLUME ADJUST",      // 20
	"OSC SPEED",          // 21
	"DELAY L",            // 22
	"DELAY R",            // 23
	"FEEDBACK L",         // 24
	"FEEDBACK R",         // 25
	"DELAY DRY/WET",      // 26
	"CHORUS DRY/WET",     // 27
	"FLANGER DRY/WET",    // 28
	"PHASER DRY/WET",     // 29
	"LOW EMPHASIS FC",    // 30
	"LOW EMPHASIS G",     // 31
	"HIGH EMPHASIS FC",   // 32
	"HIGH EMPHASIS G",    // 33
	"REVERB TIME",        // 34 (0x22)
	"PRE DELAY",          // 35 (0x23)
	"HIGH DAMP GAIN",     // 36 (0x24)
	"ER.LEVEL",           // 37 (0x25)
	"PITCH L",            // 38
	"PITCH R",            // 39
	"THRESHOLD",          // 40
	"RATIO",              // 41
	"ATTACK SENS.",       // 42
	"RELEASE SENS.",      // 43
	"ATTACK RATE",        // 44
	"RELEASE RATE",       // 45
	"GATE TIME",          // 46
	"MASK TIME",          // 47
	"HARS TIME",          // 48
	"LFO WAVEFORM",       // 49
	"OSC WAVEFORM",       // 50
	"BAND EMPHASIS FC",   // 51
	"BAND EMPHASIS Q",    // 52
	"BAND EMPHASIS G",    // 53
	"LOW MIX",            // 54
	"HIGH MIX",           // 55
	"PHASE",              // 56
	"FEEDBACK",           // 57
	"SWEEP RANGE",        // 58
	"WAH CENTER FC",      // 59
	"HARS TIME L",        // 60
	"HARS TIME R",        // 61
	"BALANCE L",          // 62
	"BALANCE R",          // 63
	"FAST LFO SPEED L",   // 64
	"FAST LFO SPEED R",   // 65
	"MODULATION DEPTH",   // 66
	"DELAY1 DRY/WET",     // 67
	"DELAY2 DRY/WET",     // 68
	"VIBRATO DRY/WET",    // 69
	"WAH DRY/WET",        // 70
	"FAST LFO SPEED",     // 71
	"TREBLE DEPTH",       // 72
	"FAST",               // 73
	"BASS DEPTH",         // 74
	"DELAY 1",            // 75
	"DELAY 2",            // 76
	"DELAY 3",            // 77
	"DELAY 4",            // 78
	"PAN 1",              // 79
	"PAN 2",              // 80
	"PAN 3",              // 81
	"PAN 4",              // 82
	"INTENSITY",          // 83
	"EXCITE",             // 84
};

const int DS3613GF3BA_EFFECT_PARAM_COUNT = std::size(DS3613GF3BA_EFFECT_PARAM_NAMES);
