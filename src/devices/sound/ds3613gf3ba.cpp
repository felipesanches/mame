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
	save_item(NAME(m_par_cmd));
}

void ds3613gf3ba_device::device_reset()
{
	m_addr = 0;
	std::fill(std::begin(m_regs), std::end(m_regs), 0);
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

	char const *desc;
	if (reg >= 0x10 && reg <= 0x17)
		desc = "voice";
	else if (reg == 0x1f)
		desc = "config";
	else
		desc = "unk";

	LOGMASKED(LOG_DSP, "ch%d %s[0x%02X] = 0x%02X (addr=0x%02X)\n",
		channel, desc, reg, val, m_addr);
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
			LOGMASKED(LOG_PARALLEL, "REG WRITE addr=0x%02X value=0x%04X\n", addr, value);
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
				// Effect parameter: [0x01, 0x60, offset, sub, p1, p2, p3]
				LOGMASKED(LOG_PARALLEL, "EFFECT PARAM mode=0x%02X base=0x%02X",
					mode, param);
				for (size_t i = 2; i < m_par_data.size() && i < 7; i++)
					LOGMASKED(LOG_PARALLEL, " 0x%02X", m_par_data[i]);
				LOGMASKED(LOG_PARALLEL, "\n");
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
		LOGMASKED(LOG_PARALLEL, "INIT CONFIG [");
		for (size_t i = 0; i < m_par_data.size(); i++)
			LOGMASKED(LOG_PARALLEL, "%s0x%02X", i ? " " : "", m_par_data[i]);
		LOGMASKED(LOG_PARALLEL, "]\n");
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
		LOGMASKED(LOG_PARALLEL, "CONTROL [");
		for (size_t i = 0; i < m_par_data.size(); i++)
			LOGMASKED(LOG_PARALLEL, "%s0x%02X", i ? " " : "", m_par_data[i]);
		LOGMASKED(LOG_PARALLEL, "]\n");
		break;

	default:
		// Unknown command - hex dump
		LOGMASKED(LOG_PARALLEL, "CMD 0x%02X (%zu bytes) [",
			m_par_cmd, m_par_data.size());
		for (size_t i = 0; i < m_par_data.size() && i < 16; i++)
			LOGMASKED(LOG_PARALLEL, "%s0x%02X", i ? " " : "", m_par_data[i]);
		if (m_par_data.size() > 16)
			LOGMASKED(LOG_PARALLEL, " ...");
		LOGMASKED(LOG_PARALLEL, "]\n");
		break;
	}
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
//  Effect parameter name table (from MainCPU ROM at 0xE324D0, 84 entries)
//--------------------------------------------------------------------------

char const *const DS3613GF3BA_EFFECT_PARAM_NAMES[] = {
	"VOLUME",             // 0
	"VOLUME",             // 1
	"REV SEND",           // 2
	"DRIVE",              // 3
	"ADJUST",             // 4
	"EMPHASIS GAIN",      // 5
	"DEPTH",              // 6
	"LFO SPEED",          // 7
	"SLOW LFO SPEED",     // 8
	"FAST LFO BALANCE",   // 9
	"RESONANCE",          // 10
	"MANUAL",             // 11
	"SLOW/FAST",          // 12
	"TREBLE FAST",        // 13
	"SLOW",               // 14
	"WIND UP",            // 15
	"WIND DOWN",          // 16
	"BASS FAST",          // 17
	"BASS SLOW",          // 18
	"VOLUME ADJUST",      // 19
	"OSC SPEED",          // 20
	"DELAY L",            // 21
	"DELAY R",            // 22
	"FEEDBACK L",         // 23
	"FEEDBACK R",         // 24
	"DELAY DRY/WET",      // 25
	"CHORUS DRY/WET",     // 26
	"FLANGER DRY/WET",    // 27
	"PHASER DRY/WET",     // 28
	"LOW EMPHASIS FC",    // 29
	"LOW EMPHASIS G",     // 30
	"HIGH EMPHASIS FC",   // 31
	"HIGH EMPHASIS G",    // 32
	"REVERB TIME",        // 33
	"PRE DELAY",          // 34
	"HIGH DAMP GAIN",     // 35
	"ER.LEVEL",           // 36
	"PITCH L",            // 37
	"PITCH R",            // 38
	"THRESHOLD",          // 39
	"RATIO",              // 40
	"ATTACK SENS.",       // 41
	"RELEASE SENS.",      // 42
	"ATTACK RATE",        // 43
	"RELEASE RATE",       // 44
	"GATE TIME",          // 45
	"MASK TIME",          // 46
	"HARS TIME",          // 47
	"LFO WAVEFORM",       // 48
	"OSC WAVEFORM",       // 49
	"BAND EMPHASIS FC",   // 50
	"BAND EMPHASIS Q",    // 51
	"BAND EMPHASIS G",    // 52
	"LOW MIX",            // 53
	"HIGH MIX",           // 54
	"PHASE",              // 55
	"FEEDBACK",           // 56
	"SWEEP RANGE",        // 57
	"WAH CENTER FC",      // 58
	"HARS TIME L",        // 59
	"HARS TIME R",        // 60
	"BALANCE L",          // 61
	"BALANCE R",          // 62
	"FAST LFO SPEED L",   // 63
	"FAST LFO SPEED R",   // 64
	"MODULATION DEPTH",   // 65
	"DELAY1 DRY/WET",     // 66
	"DELAY2 DRY/WET",     // 67
	"VIBRATO DRY/WET",    // 68
	"WAH DRY/WET",        // 69
	"FAST LFO SPEED",     // 70
	"TREBLE DEPTH",       // 71
	"FAST",               // 72
	"BASS DEPTH",         // 73
	"DELAY 1",            // 74
	"DELAY 2",            // 75
	"DELAY 3",            // 76
	"DELAY 4",            // 77
	"PAN 1",              // 78
	"PAN 2",              // 79
	"PAN 3",              // 80
	"PAN 4",              // 81
	"INTENSITY",          // 82
	"EXCITE",             // 83
};

const int DS3613GF3BA_EFFECT_PARAM_COUNT = std::size(DS3613GF3BA_EFFECT_PARAM_NAMES);
