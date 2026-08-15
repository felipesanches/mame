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
#define LOG_TIMBRE (1U << 3)   // timbre loads, and whether S1 can hear them

#define VERBOSE (0)
#include "logmacro.h"

DEFINE_DEVICE_TYPE(EPUSP_SYNTH, epusp_synth_device, "epusp_synth", "EPUSP sound synthesiser (Guido Stolfi)")

epusp_synth_device::epusp_synth_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, EPUSP_SYNTH, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, m_s1(*this, "S1")
{
	std::fill(std::begin(m_timbre), std::end(m_timbre), 0);
	for (auto &mem : m_mem)
		std::fill(std::begin(mem), std::end(mem), 0.0);
}

/* S1, the nine-position selector of chapter 11.  It is a machine
   configuration and not a DIP switch because it is a front-panel control the
   player turned between pieces, not a setting anyone opened the case for.

   The default is position 8, the computer's own memory: it is the only one a
   punched tape can fill, so it is the only default under which a tape that
   programs a timbre can be heard at all. */
static INPUT_PORTS_START(epusp_synth)
	PORT_START("S1")
	PORT_CONFNAME(0x0f, 8, "S1 -- selecao de timbre")
	PORT_CONFSETTING(0, "Externa (chaves do painel)")
	PORT_CONFSETTING(1, "Memoria 1")
	PORT_CONFSETTING(2, "Memoria 2")
	PORT_CONFSETTING(3, "Memoria 3")
	PORT_CONFSETTING(4, "Memoria 4")
	PORT_CONFSETTING(5, "Memoria 5")
	PORT_CONFSETTING(6, "Memoria 6")
	PORT_CONFSETTING(7, "Memoria 7")
	PORT_CONFSETTING(8, "Memoria 8 (computador)")
INPUT_PORTS_END

ioport_constructor epusp_synth_device::device_input_ports() const
{
	return INPUT_PORTS_NAME(epusp_synth);
}

/* Which waveform reaches the D/A right now.  nullptr means S1 is on the
   external position, where the 4 x 16 front-panel switches fed the generator
   directly -- there is no stored table to return, and nothing survives about
   what those switches were set to. */
const double *epusp_synth_device::selected_wave() const
{
	unsigned const pos = m_s1->read() & 0x0f;
	if (pos == S1_EXTERNAL || pos > 8)
		return nullptr;
	return m_mem[pos - 1];
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

/* THE TIMBRE IS SIXTEEN 4-BIT SAMPLES, SENT AS FOUR BIT PLANES.

   Commands 1 to 8 carry four 16-bit words, most significant plane first, each
   split into high byte then low byte.  Sample k contributes its bit b to bit
   (15-k) of plane b -- so the first sample is the MOST significant bit of each
   word, not the least.

   That is not a reading of a schematic: it is pinned down by the one case a
   surviving pair of tapes settles.  Test C1.2 of
   scripts/sintetizador/verificar.py takes "TIMBRE,0011223344556677" from the
   source score FITA#020 and finds, in the independently digitised object
   FITA#023, exactly

       /01 /02 /03 /04 /05 /06 /07 /08
        00  00  00  FF  0F  0F  33  33

   Unpacking those back gives 0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7 -- the ramp the
   score asked for.

   The samples are unsigned 0..15, and the D/A converters are unipolar
   (chapter 1: 0 V for 00000000, +5 V for 11111111).  So the waveform that
   leaves the converter has a DC component, and what removes it is the
   coupling into the audio path -- not the data.

   CENTRE ON THE WAVEFORM'S OWN MEAN, NOT ON 7.5.  The first version subtracted
   a fixed 7.5, which is right only for a waveform that uses the full range.
   The timbre the surviving score actually asks for,
   "TIMBRE,0011223344556677", uses samples 0 to 7 and nothing above, so a fixed
   centre left every sample negative: a large DC offset with a small ripple on
   top, which never crosses zero.  The emulation went silent, and
   analisar_wav.py reported "o WAV esta em silencio" because a signal that
   never crosses zero has no measurable fundamental.

   Removing the mean is both what a coupling capacitor does and what makes the
   half-range ramp audible.  A full-range square still comes out as +-1. */
void epusp_synth_device::unpack(const uint8_t planes[8], double wave[16])
{
	unsigned bruto[16];
	double soma = 0.0;
	for (int k = 0; k < 16; k++)
	{
		unsigned sample = 0;
		for (int b = 3; b >= 0; b--)
		{
			unsigned const word = (unsigned(planes[2 * (3 - b)]) << 8) | planes[2 * (3 - b) + 1];
			sample |= ((word >> (15 - k)) & 1) << b;
		}
		bruto[k] = sample;
		soma += sample;
	}

	double const media = soma / 16.0;
	for (int k = 0; k < 16; k++)
		wave[k] = (double(bruto[k]) - media) / 7.5;
}

void epusp_synth_device::device_start()
{
	m_stream = stream_alloc(0, 1, 48000);

	save_item(NAME(m_pitch));
	save_item(NAME(m_gate));
	save_item(NAME(m_intensity));
	save_item(NAME(m_timbre));
	save_item(NAME(m_mem));
	save_item(NAME(m_phase));
}

void epusp_synth_device::device_reset()
{
	m_pitch = 0;
	m_gate = false;
	m_intensity = 0;

	/* A SQUARE WAVE AS THE DEFAULT TIMBRE, and the reason matters.

	   FITA#015 -- the Bachianinha -- sends no TIMBRE command at all: test C1.4
	   of verificar.py finds it uses only /0B and /00.  On the real instrument
	   it would play with whatever was left in the timbre store from the
	   previous piece, which is not something a fresh emulation can reproduce.
	   Starting from an all-zero table would make that tape silent, and silence
	   with no error is the worst failure mode this project has.

	   So the store comes up holding a square: eight samples high, eight low.
	   It is a modelling choice, declared, not a reading. */
	static const uint8_t QUADRADA[8] = { 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00 };
	std::copy(std::begin(QUADRADA), std::end(QUADRADA), std::begin(m_timbre));
	for (auto &mem : m_mem)
		std::fill(std::begin(mem), std::end(mem), 0.0);
	unpack(m_timbre, m_mem[COMPUTER_MEMORY]);

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
		   "TIMBRE,0011223344556677" -> 00 00 00 FF 0F 0F 33 33.

		   These eight bytes ARE the serial fill of memory 8 through en1/en0:
		   16 samples x 4 bits = 64 bits = 8 bytes, and chapter 11 gives the
		   computer no other way in.  So they land in COMPUTER_MEMORY and
		   nowhere else -- the computer cannot touch memories 1 to 7, whose
		   only loading path is the manual TRANSFERE switches. */
		m_timbre[cmd - 1] = data;
		unpack(m_timbre, m_mem[COMPUTER_MEMORY]);

		/* Worth saying out loud, because it is the single most likely reason
		   for "the tape programs a timbre and I hear no change": the computer
		   can only write memory 8, and S1 decides what is heard.  With S1
		   anywhere else, this load is real and simply inaudible -- which is
		   how the instrument behaved, not a fault. */
		if (cmd == 8 && selected_wave() != m_mem[COMPUTER_MEMORY])
		{
			LOGMASKED(LOG_TIMBRE,
					"timbre carregado na memoria 8, mas S1 esta na posicao %u: nao sera ouvido\n",
					m_s1->read() & 0x0f);
		}
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

	/* WHY THIS IS STILL A SQUARE WAVE, WITH selected_wave() SITTING RIGHT
	   THERE -- and what modelling chapter 11 changed about the answer.

	   The unpacking is correct: it reproduces the one case test C1.2 pins
	   down, and m_mem[COMPUTER_MEMORY] holds the samples the tape asked for.
	   The eight memories and S1 above are chapter 11's architecture, read off
	   the manual and the chip list rather than guessed.

	   And the model, now that it is structurally right, STILL PREDICTS
	   SILENCE.  FITA#020 sends "2,24,TIMBRE,0000000000000000" at t=168 -- an
	   all-zero table, halfway through the piece.  Commands 1 to 8 are the
	   serial fill of memory 8; there is no other door for the computer.  With
	   S1 on 8, memory 8 goes to zero at t=168 and everything after it is
	   silent.  Yet the score keeps sending notes after t=168, and the
	   instrument plainly played them.

	   THAT IS THE USEFUL RESULT.  Before chapter 11 the silence could have
	   been our unpacking, our centring, or a missing indirection -- three
	   suspects.  Now the architecture is documented and the silence survives,
	   which rules the architecture out and localises the gap: either
	   GRTMB (command 9) and LETMB (command 10) do something to the path that
	   chapter 5's names do not reveal, or the timbre D/A is not the only thing
	   feeding the output.  The hunt for what S9 and S10 strobe is paused in
	   notas/timbre_indirecao.md in the PatinhoFeio repository, with the
	   eliminations recorded so nobody repeats them.

	   So the square stays.  Sounding a table that the documented model says
	   should be silent would be a guess dressed as a result, and this project
	   has already paid for diagnoses that pointed at the wrong place.  The
	   memories are kept, unpacked and save-stated so that the reading, when it
	   comes, has somewhere to land.

	   A FRACTIONAL PHASE ACCUMULATOR, not an integer half-period counter.
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
