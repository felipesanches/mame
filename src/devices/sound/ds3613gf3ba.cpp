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

***************************************************************************/

#include "emu.h"
#include "ds3613gf3ba.h"

#define LOG_DSP (1U << 1)   // Register writes

#define VERBOSE (LOG_DSP)
#include "logmacro.h"

DEFINE_DEVICE_TYPE(DS3613GF3BA, ds3613gf3ba_device, "ds3613gf3ba", "DS3613GF-3BA Effect DSP")

ds3613gf3ba_device::ds3613gf3ba_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, DS3613GF3BA, tag, owner, clock)
	, m_addr(0)
{
}

void ds3613gf3ba_device::device_start()
{
	save_item(NAME(m_addr));
	save_item(NAME(m_regs));
}

void ds3613gf3ba_device::device_reset()
{
	m_addr = 0;
	std::fill(std::begin(m_regs), std::end(m_regs), 0);
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
