// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Sound synthesiser of the EPUSP, by Guido Stolfi -- first stage

    See epusp_synth.h for the command set and where it comes from.

***************************************************************************/

#include "emu.h"
#include "epusp_synth.h"

#include <cmath>

#define LOG_CMD    (1U << 1)   // every command that arrives
#define LOG_NOTE   (1U << 2)   // only the notes, with the frequency

#define VERBOSE (0)
#include "logmacro.h"

DEFINE_DEVICE_TYPE(EPUSP_SYNTH, epusp_synth_device, "epusp_synth", "EPUSP sound synthesiser (Guido Stolfi)")

epusp_synth_device::epusp_synth_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, EPUSP_SYNTH, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
{
	std::fill(std::begin(m_timbre), std::end(m_timbre), 0);
}

/* Chapter 3 of the synthesiser manual puts the scale between "o do de
   16,35 Hz" and "o si de 15 804,3 Hz", which is ten octaves of twelve
   semitones, and the pitch byte carries the octave in the high nibble and the
   semitone in the low one.  So code /00 is that bottom C and

       f = 16.3516 * 2^(octave + semitone/12)

   The low nibble runs 0 to 11; 12 to 15 are not notes.  They do not occur in
   any surviving tape -- scripts/sintetizador/inventario_comandos.py checks
   that in 100% of the pitch codes the low nibble is <= 11 and the high one
   <= 9 -- so reaching the clamp below means something upstream is wrong. */
double epusp_synth_device::frequency(uint8_t code)
{
	unsigned const octave = code >> 4;
	unsigned const semitone = code & 0x0F;
	return 16.3516 * std::pow(2.0, double(octave) + double(std::min(semitone, 11U)) / 12.0);
}

void epusp_synth_device::device_start()
{
	m_stream = stream_alloc(0, 1, 48000);

	save_item(NAME(m_pitch));
	save_item(NAME(m_gate));
	save_item(NAME(m_intensity));
	save_item(NAME(m_timbre));
	save_item(NAME(m_phase));
}

void epusp_synth_device::device_reset()
{
	m_pitch = 0;
	m_gate = false;
	m_intensity = 0;
	std::fill(std::begin(m_timbre), std::end(m_timbre), 0);
	m_phase = 0.0;
}

void epusp_synth_device::command_w(uint16_t pair)
{
	uint8_t const cmd = pair >> 8;
	uint8_t const data = pair & 0xFF;

	m_stream->update();

	switch (cmd)
	{
	case 0:     // NOTA TOCADA
	case 11:    // nota SILENCIOSA
		/* Chapter 3: the two carry THE SAME pitch byte, and what differs is
		   only the level of the gate signal -- "0 Volts para nota silenciosa
		   e 5 Volts para" nota tocada.  That is why the tapes always send the
		   pair (/0B, nota) then (/00, nota). */
		m_pitch = data;
		m_gate = (cmd == 0);
		LOGMASKED(LOG_NOTE, "%s /%02X -> %.1f Hz\n",
				(cmd == 0) ? "NOTA TOCADA" : "nota SILENCIOSA", data, frequency(data));
		break;

	case 12:    // D/A 1 (INT.)
		/* The D/A converters are unipolar, 0 V for 00000000 and +5 V for
		   11111111 (chapter 1, "observacoes").  The scores write negative
		   numbers -- "INT,-64" -- which the compiler punches as the byte
		   /C0, so /FF is the LOUDEST value and not a quiet "-1". */
		m_intensity = data;
		break;

	case 1: case 2: case 3: case 4:
	case 5: case 6: case 7: case 8:
		/* Armazenamento de timbre.  Sixteen 4-bit samples, transposed into
		   four bit planes of sixteen bits each; test C1.2 of
		   scripts/sintetizador/verificar.py pins the transposition down with
		   "TIMBRE,0011223344556677" -> 00 00 00 FF 0F 0F 33 33.  Stored, not
		   yet sounded: the wavetable is the next stage. */
		m_timbre[cmd - 1] = data;
		break;

	default:
		// 9 GRTMB, 10 LETMB, 13-19 the other D/A, 20 portamento, 21-24
		// envelopes, 31 SINC, 255 NOP.  All decoded by the manual, none of
		// them audible until the analogue blocks exist.
		break;
	}

	LOGMASKED(LOG_CMD, "comando %3u dado /%02X\n", cmd, data);
}

void epusp_synth_device::sound_stream_update(sound_stream &stream)
{
	if (!m_gate || m_intensity == 0)
		return;

	double const f = frequency(m_pitch);
	if (f <= 0.0 || f >= stream.sample_rate() / 2.0)
		return;   // above Nyquist there is nothing honest to emit

	/* A SQUARE WAVE IS A PLACEHOLDER, AND IS MEANT TO BE ONE.  The real
	   instrument builds its waveform in the timbre generator (chapter 11) and
	   then puts it through the VCF, the envelopes and the rest.  None of that
	   is here.  What this stage is for is making the CONTROL side audible:
	   whether the right note starts at the right moment, for the right
	   length, at the right level. */
	/* A FRACTIONAL PHASE ACCUMULATOR, not an integer half-period counter.
	   The first version counted down from int(rate / (2*f)), which quantises
	   the period to whole samples: every note came out up to 1.3% sharp, and
	   scripts/sintetizador/analisar_wav.py in the PatinhoFeio repository saw
	   it as a systematic error in the same direction on every note. A machine
	   built to play music in tune should not be detuned by the emulator's
	   arithmetic. */
	double const step = f / stream.sample_rate();
	double const level = double(m_intensity) / 255.0;

	for (int i = 0; i < stream.samples(); i++)
	{
		m_phase += step;
		if (m_phase >= 1.0)
			m_phase -= 1.0;
		stream.put(0, i, ((m_phase < 0.5) ? 1.0 : -1.0) * level);
	}
}
