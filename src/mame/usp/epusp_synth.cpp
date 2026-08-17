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
	std::fill(std::begin(m_computer_half), std::end(m_computer_half), 0.0);
	for (auto &s : m_store)
		std::fill(std::begin(s), std::end(s), 0.0);
}

/* The mode/selection control of chapter 3: "Chave de 2 posicoes (AUTO --
   MANUAL) + chave de 8 posicoes (PGRF-M1-M2...M7)", which is the nine-position
   switch chapter 11 counts.  A machine configuration and not a DIP switch,
   because it is a front-panel control the player turned between pieces.

   THE DEFAULT IS "MANUAL: PGRF", AND THE REASON IS AN EXPERIMENT, NOT TASTE.

   On AUTO the computer's LETMB chooses, and running FITA#023 that way CHOPS A
   SUSTAINED NOTE INTO PIECES: the tape stores an all-zero waveform into M7 at
   t=168 while the note that began at t=144 is still gated on, then selects M7
   at t=216 and PGRF again at t=287.  analisar_wav.py sees 8 sounding stretches
   where the score has 7 notes.

   That is self-consistent -- it is exactly what "LETMB,n selects store n"
   predicts -- but it cannot be checked against the instrument, and there are
   two readings left standing:

     (a) the piece really did have that gated texture, or
     (b) M7 was switched to BLOQUEIA, whose whole purpose per chapter 3 is that
         "a ultima forma de onda armazenada permanece inalterada,
         independentemente do modo de operacao e dos comandos do computador" --
         so GRTMB,7 did nothing and the note kept its timbre.

   Nothing records which memories were available in 1977.  So the default is
   the position where the operator's switch decides and the tape cannot chop
   anything, and AUTO is one setting away for anyone who wants to hear the
   other reading.  LOG_TIMBRE narrates every GRTMB and LETMB either way. */
static INPUT_PORTS_START(epusp_synth)
	PORT_START("S1")
	PORT_CONFNAME(0x0f, 1, "Selecao de timbre (AUTO/MANUAL + PGRF/M1-M7)")
	PORT_CONFSETTING(0, "AUTO (o computador escolhe)")
	PORT_CONFSETTING(1, "MANUAL: PGRF (painel grafico)")
	PORT_CONFSETTING(2, "MANUAL: M1")
	PORT_CONFSETTING(3, "MANUAL: M2")
	PORT_CONFSETTING(4, "MANUAL: M3")
	PORT_CONFSETTING(5, "MANUAL: M4")
	PORT_CONFSETTING(6, "MANUAL: M5")
	PORT_CONFSETTING(7, "MANUAL: M6")
	PORT_CONFSETTING(8, "MANUAL: M7")
INPUT_PORTS_END

ioport_constructor epusp_synth_device::device_input_ports() const
{
	return INPUT_PORTS_NAME(epusp_synth);
}

/* Which store reaches the output right now.  On AUTO the computer's last
   LETMB decides; on any MANUAL position the switch does, and the computer's
   LETMB is then simply ignored -- which is what "Em qualquer posicao diferente
   da AUTO" describes. */
unsigned epusp_synth_device::selected_store() const
{
	unsigned const pos = m_s1->read() & 0x0f;
	if (pos == S1_AUTO)
		return (m_auto_select < STORES) ? m_auto_select : PGRF;
	return std::min<unsigned>(pos - 1, STORES - 1);
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

   THESE SIXTEEN ARE HALF A CYCLE.  Chapter 3: the graphic panel is "16 chaves
   deslizantes de 16 posicoes cada uma no qual se desenha MEIO CICLO de uma
   forma de onda IMPAR", and the output SAU is a "FUNCAO IMPAR cujo PRIMEIRO
   SEMI-CICLO e o registrado".  The clock confirms it without needing the
   prose: frl is "32 VEZES a da forma de onda de saida", and 32 = 16 x 2.

   So this returns the first half cycle only, and sound_stream_update() plays
   it forwards and then negated.  The full period is odd by construction, so
   its mean is exactly zero.

   An earlier version treated the sixteen as a whole cycle and subtracted their
   mean to kill a DC offset.  The offset was an artefact of that mistake: a
   half-range ramp read as a full cycle really does sit off-centre.  Read as a
   half cycle it does not, and the subtraction is gone.

   Scale: samples are unsigned 0..15 out of a unipolar converter (chapter 1,
   0 V for 00000000 and +5 V for 11111111), and SAU swings -5 to +5 V, so the
   sample maps to the positive half and its negation to the other. */
void epusp_synth_device::unpack(const uint8_t planes[8], double half[16])
{
	for (int k = 0; k < 16; k++)
	{
		unsigned sample = 0;
		for (int b = 3; b >= 0; b--)
		{
			unsigned const word = (unsigned(planes[2 * (3 - b)]) << 8) | planes[2 * (3 - b) + 1];
			sample |= ((word >> (15 - k)) & 1) << b;
		}
		half[k] = double(sample) / 15.0;
	}
}

void epusp_synth_device::device_start()
{
	m_stream = stream_alloc(0, 1, 48000);

	save_item(NAME(m_pitch));
	save_item(NAME(m_gate));
	save_item(NAME(m_intensity));
	save_item(NAME(m_timbre));
	save_item(NAME(m_computer_half));
	save_item(NAME(m_store));
	save_item(NAME(m_auto_select));
	save_item(NAME(m_phase));
}

void epusp_synth_device::device_reset()
{
	m_pitch = 0;
	m_gate = false;

	/* INTENSITY COMES UP AT FULL SCALE, and this is not cosmetic.

	   sound_stream_update() returns early when intensity is zero, and TWO of
	   the four surviving music tapes never send command 12 at all.  FITA#015,
	   the Bachianinha, uses only /0B and /00 -- pitch and gate -- for all 432
	   of its notes, and FITA#024 is the same.  Both played in total silence,
	   with no error anywhere.

	   That is not what the instrument did.  INT is D/A 1, a control voltage
	   into the gain-controlled amplifier, and a tape that never writes it
	   simply played at whatever the front-panel attenuators (POTM, chapter 3)
	   were set to.  Those settings are not on the tape and did not survive.

	   Full scale is the declared choice: it makes an unset gain audible rather
	   than silent, and the listener's own volume control is the honest place
	   for a level nobody recorded.  Silence with no error is the worst failure
	   mode this project has, and it had claimed two tapes. */
	m_intensity = 0xFF;

	/* A SQUARE AS THE DEFAULT WAVEFORM, in every store, and the reason
	   matters more now than it did.

	   Position 0 is PGRF, the GRAPHIC PANEL: sixteen sliding switches the
	   composer set BY HAND.  FITA#023 selects it with "LETMB,0" for most of
	   the piece, and that drawing is not on the tape -- the one thing needed
	   to reproduce the piece exactly is the one thing no tape can carry.

	   FITA#015, the Bachianinha, sends no TIMBRE command at all (test C1.4 of
	   verificar.py), so on the real instrument it played with whatever was
	   left in the stores from the previous session.

	   Both cases would be silent from an all-zero store, and silence with no
	   error is the worst failure mode this project has.  So every store comes
	   up holding a square -- the first half cycle all at maximum, whose odd
	   extension is the square.  It is a modelling choice, declared, not a
	   reading. */
	static const uint8_t QUADRADA[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	std::copy(std::begin(QUADRADA), std::end(QUADRADA), std::begin(m_timbre));
	unpack(m_timbre, m_computer_half);
	for (auto &s : m_store)
		std::copy(std::begin(m_computer_half), std::end(m_computer_half), std::begin(s));

	m_auto_select = PGRF;
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
		/* Armazenamento de timbre: the computer's own store.  Sixteen 4-bit
		   samples transposed into four bit planes of sixteen bits, pinned down
		   by test C1.2 of scripts/sintetizador/verificar.py --
		   "TIMBRE,0011223344556677" -> 00 00 00 FF 0F 0F 33 33.

		   These eight bytes are the serial fill of chapter 11's eighth memory
		   through en1/en0: 16 samples x 4 bits = 64 bits = 8 bytes.  They do
		   NOT reach the output on their own; GRTMB has to move them. */
		m_timbre[cmd - 1] = data;
		unpack(m_timbre, m_computer_half);
		break;

	case 9:
	{
		/* GRTMB -- the "TRANSFIRA" of chapter 3.  Copies the computer's store
		   into memory n.  Position 0 is the graphic panel, which is a set of
		   physical switches and cannot be written, so a transfer there is
		   ignored rather than silently accepted. */
		unsigned const n = data & 0x0F;
		if (n == PGRF || n >= STORES)
		{
			LOGMASKED(LOG_TIMBRE, "GRTMB,%u ignorado: %s\n", n,
					(n == PGRF) ? "PGRF e painel fisico, nao memoria" : "fora de M1-M7");
		}
		else
		{
			std::copy(std::begin(m_computer_half), std::end(m_computer_half),
					std::begin(m_store[n]));
			LOGMASKED(LOG_TIMBRE, "GRTMB,%u: forma de onda do computador -> M%u\n", n, n);
		}
		break;
	}

	case 10:
		/* LETMB -- the "LEIA" of chapter 3.  Selects which store reaches the
		   output.  Only has effect with the front panel on AUTO; chapter 3 is
		   explicit that in any other position the operator's switch decides. */
		m_auto_select = data & 0x0F;
		LOGMASKED(LOG_TIMBRE, "LETMB,%u: seleciona %s\n", m_auto_select,
				(m_auto_select == PGRF) ? "PGRF" : "memoria");
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

	/* THE WAVETABLE, AT LAST -- and what changed to make it honest.

	   The store holds sixteen samples that are HALF a cycle of an odd
	   waveform, so the period is 32 steps: the sixteen forwards, then the same
	   sixteen negated.  Chapter 3 states it twice (the panel draws "meio ciclo
	   de uma forma de onda impar"; SAU is a "funcao impar cujo primeiro
	   semi-ciclo e o registrado") and the clock proves it a third time -- frl
	   is "32 vezes a da forma de onda de saida", and 32 = 16 x 2.

	   That is what unblocked this.  Earlier attempts fed the sixteen samples
	   in as a whole cycle and had to subtract their mean to stop a DC offset
	   from swamping the signal; a half-range ramp read that way sits
	   off-centre, went silent, and looked like a missing indirection.  Read as
	   a half cycle there is no offset to remove, because an odd function has
	   none.

	   A FRACTIONAL PHASE ACCUMULATOR, not an integer step counter.  The first
	   square-wave version counted down from int(rate / (2*f)), which quantises
	   the period to whole samples: every note came out up to 1.3% sharp, and
	   scripts/sintetizador/analisar_wav.py in the PatinhoFeio repository saw a
	   systematic error in the same direction on every note.  A machine built
	   to play music in tune should not be detuned by the emulator's
	   arithmetic.

	   No interpolation: the instrument steps its D/A once per frl edge and
	   holds, so zero-order hold is what it did. */
	double const *const half = m_store[selected_store()];
	double const step = f / stream.sample_rate();
	double const level = double(m_intensity) / 255.0;

	for (int i = 0; i < stream.samples(); i++)
	{
		m_phase += step;
		if (m_phase >= 1.0)
			m_phase -= 1.0;

		unsigned const k = unsigned(m_phase * 32.0) & 31;
		double const s = (k < 16) ? half[k] : -half[k - 16];
		stream.put(0, i, s * level);
	}
}
