// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Sound synthesiser of the EPUSP, by Guido Stolfi -- first stage

    See synth.h for the command set and where it comes from.

***************************************************************************/

#include "emu.h"
#include "synth.h"

#include "speaker.h"

#include "epusp_synth.lh"

#include <bit>
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
	, device_epusp_synth_port_interface(mconfig, *this)
	, m_s1(*this, "S1")
	, m_pgrf_amostra(*this, "AM%u", 0U)
	, m_tecl_modo(*this, "TECL")
	, m_tecl_transp(*this, "TRANSP")
	, m_tape(*this, "tape")
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
	/* PORT_TOGGLE ON A PORT_CONFNAME is what makes the knob on the front panel
	   turn when it is clicked.  A layout item bound to a field calls
	   ioport_field::set_value(1) on the press, and frame_update() answers that
	   with select_next_setting() -- but ONLY for a field flagged as a toggle
	   (src/emu/ioport.cpp:1270).  Without the flag the knob would draw the
	   position correctly and refuse to be turned.

	   It costs nothing anywhere else: a toggle field folds its value into the
	   port's default value and reports no digital bit of its own
	   (ioport.cpp:1288), so m_s1->read() returns exactly the same thing it did
	   before, and the Machine Configuration menu still sets it directly.  The
	   precedent is atari/a2600.cpp, whose TV Type and difficulty switches are
	   PORT_CONFNAME + PORT_TOGGLE for the same reason. */
	PORT_START("S1")
	PORT_CONFNAME(0x0f, 1, "Selecao de timbre (AUTO/MANUAL + PGRF/M1-M7)") PORT_TOGGLE
	PORT_CONFSETTING(0, "AUTO (o computador escolhe)")
	PORT_CONFSETTING(1, "MANUAL: PGRF (painel grafico)")
	PORT_CONFSETTING(2, "MANUAL: M1")
	PORT_CONFSETTING(3, "MANUAL: M2")
	PORT_CONFSETTING(4, "MANUAL: M3")
	PORT_CONFSETTING(5, "MANUAL: M4")
	PORT_CONFSETTING(6, "MANUAL: M5")
	PORT_CONFSETTING(7, "MANUAL: M6")
	PORT_CONFSETTING(8, "MANUAL: M7")

	/* THE GRAPHIC PANEL (PGRF): SIXTEEN SAMPLES, FOUR BITS EACH.

	   The real control, counted by chapter 11's parts list, is "16 chaves de
	   entrada" PER BIT UNIT, and there are four bit units -- 64 toggle switches
	   in all, one per bit, laid out as four bit planes of sixteen samples.
	   What is offered here is SIXTEEN SLIDERS OF SIXTEEN POSITIONS, one per
	   sample, each position being the four bits of that sample.

	   That is a usability choice by the project owner, made knowing the real
	   panel had 64 switches, and it is recorded as a choice and not passed off
	   as a reading.  The information is the same either way (16 x 4 bits), and
	   the reason for the swap is the name of the thing: PGRF is the PAINEL
	   GRAFICO, and chapter 3 describes drawing at it -- "Conjunto de 16 chaves
	   deslizantes de 16 posicoes cada uma no qual se desenha MEIO CICLO de uma
	   forma de onda IMPAR".  A row of cursor heights DRAWS the wave; a matrix
	   of on/off switches spells it out in binary.

	   THESE SIXTEEN ARE HALF A CYCLE.  They feed store 0 and nothing else;
	   sound_stream_update() plays them forwards and then negated, exactly as it
	   already did.  Sample 0 is the FIRST of the half cycle, which is the same
	   ordering commands 1 to 8 use (sample k is bit 15-k of each plane).

	   THE DEFAULT IS 15 ON EVERY SLIDER, which is the all-maximum half cycle --
	   that is, the square this device has always come up with.  The reason for
	   the square is in device_reset(): silence with no error is the worst
	   failure mode this project has, and PGRF is exactly the store the tapes
	   select without ever writing it.

	   The values are written to cfg/patinho.cfg, so a waveform drawn by hand
	   survives between sessions -- the one thing the paper tape could not
	   carry. */
	/* The samples are numbered 0 to 15, not 1 to 16, because that is what the
	   front panel prints under each slider and what every index in this file
	   means -- m_store[s][k], the _n below, and the bit 15-k of a bit plane.
	   A user reading the panel and a user reading Tab -> Slider Controls have
	   to be reading the same number. */
#define PORT_PGRF_AMOSTRA(_n, _rotulo) \
	PORT_START("AM" #_n) \
	PORT_ADJUSTER(15, "PGRF: amostra " _rotulo " (de 16)") PORT_MINMAX(0, 15) \
	PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(epusp_synth_device::pgrf_alterado), _n)

	PORT_PGRF_AMOSTRA(0,  " 0")
	PORT_PGRF_AMOSTRA(1,  " 1")
	PORT_PGRF_AMOSTRA(2,  " 2")
	PORT_PGRF_AMOSTRA(3,  " 3")
	PORT_PGRF_AMOSTRA(4,  " 4")
	PORT_PGRF_AMOSTRA(5,  " 5")
	PORT_PGRF_AMOSTRA(6,  " 6")
	PORT_PGRF_AMOSTRA(7,  " 7")
	PORT_PGRF_AMOSTRA(8,  " 8")
	PORT_PGRF_AMOSTRA(9,  " 9")
	PORT_PGRF_AMOSTRA(10, "10")
	PORT_PGRF_AMOSTRA(11, "11")
	PORT_PGRF_AMOSTRA(12, "12")
	PORT_PGRF_AMOSTRA(13, "13")
	PORT_PGRF_AMOSTRA(14, "14")
	PORT_PGRF_AMOSTRA(15, "15")

#undef PORT_PGRF_AMOSTRA

	/* THE SEVEN "TRANSFERE" SWITCHES.

	   Chapter 11, verbatim: "Um timbre programado manualmente nesta entrada
	   pode ser transferido (manualmente) para uma de 7 memorias (numeradas de 1
	   a 7) pelos comandos TRANSFERE."  The parts list of a bit unit counts them
	   as inputs: "7 chaves de transferencia".

	   So the SOURCE is the panel and the DESTINATION is memory 1 to 7.  They
	   are the manual twin of the computer's GRTMB, whose source is instead the
	   serial store of chapter 11's "8.a memoria" (the en1/en0 inputs, which
	   commands 1 to 8 fill).  The panel is never a destination -- it is a
	   physical control, which is also why GRTMB,0 is refused in command_w().

	   Momentary and not toggles: the chapter calls them "comandos", and the
	   transfer happens on the press. */
	PORT_START("TRANSF")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("TRANSFERE 1 (painel -> M1)")
		PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(epusp_synth_device::transfere), 1)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("TRANSFERE 2 (painel -> M2)")
		PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(epusp_synth_device::transfere), 2)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("TRANSFERE 3 (painel -> M3)")
		PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(epusp_synth_device::transfere), 3)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("TRANSFERE 4 (painel -> M4)")
		PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(epusp_synth_device::transfere), 4)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("TRANSFERE 5 (painel -> M5)")
		PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(epusp_synth_device::transfere), 5)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("TRANSFERE 6 (painel -> M6)")
		PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(epusp_synth_device::transfere), 6)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("TRANSFERE 7 (painel -> M7)")
		PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(epusp_synth_device::transfere), 7)

	/* THE KEYBOARD OF CHAPTER 6, and its two switches from chapter 3.

	   "Chave manual--automatico" decides WHO supplies the note and the gate:
	   the 49 keys, or the computer's commands 0 and 11.  Chapter 3 spells the
	   automatic side out under CHV -- "Na posicao automatico o sinal de
	   chaveamento e designado por comando especifico (0 Volts para 'nota
	   silenciosa' e 5 Volts para 'nota')" -- which is exactly the pair of
	   commands this device already decodes.

	   THE DEFAULT IS AUTOMATICO, and that is not neutrality: every surviving
	   tape is the computer playing, and a keyboard that stole the voice by
	   default would silence all of them. */
	PORT_START("TECL")
	PORT_CONFNAME(0x01, 0x01, "Teclado: chave manual-automatico") PORT_TOGGLE
		PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(epusp_synth_device::teclado_alterado), 0)
	PORT_CONFSETTING(0x00, "MANUAL (as 49 teclas mandam)")
	PORT_CONFSETTING(0x01, "AUTOMATICO (o computador manda)")

	/* "Chave de 7 posicoes para transposicao de oitavas" (chapter 3).  With 49
	   keys and seven positions twelve semitones apart the reachable codes are
	   n = 12p + k for k = 0..48 and p = 0..6, that is 0 to 120 -- 121 values,
	   which is what chapter 3 counts for the manual position, with its top at
	   16.3516 x 2^10 = 16 743.9 Hz = the "16 744,0 Hz" printed there.

	   THE DEFAULT IS POSITION 2, and the reason is honesty in the key names.
	   MAME labels the keys with General MIDI names (PORT_GM_C2 and friends),
	   and at p = 2 the key called C2 really does sound 16.3516 x 2^2 =
	   65.4 Hz, which is what C2 means everywhere else.  Any other position
	   would print one note on the key and sound another. */
	PORT_START("TRANSP")
	PORT_CONFNAME(0x07, 2, "Teclado: transposicao de oitavas") PORT_TOGGLE
		PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(epusp_synth_device::teclado_alterado), 0)
	PORT_CONFSETTING(0, "1 -- do0 (16,35 Hz) a do4 (261,6 Hz)")
	PORT_CONFSETTING(1, "2 -- do1 (32,7 Hz) a do5 (523,3 Hz)")
	PORT_CONFSETTING(2, "3 -- do2 (65,4 Hz) a do6 (1046,5 Hz)")
	PORT_CONFSETTING(3, "4 -- do3 (130,8 Hz) a do7 (2093,0 Hz)")
	PORT_CONFSETTING(4, "5 -- do4 (261,6 Hz) a do8 (4186,0 Hz)")
	PORT_CONFSETTING(5, "6 -- do5 (523,3 Hz) a do9 (8372,0 Hz)")
	PORT_CONFSETTING(6, "7 -- do6 (1046,5 Hz) a do10 (16744,0 Hz)")

	/* "Manual de 49 teclas" (chapter 3), C2 to C6 in the General MIDI naming
	   MAME uses, which also makes them playable from a real keyboard through
	   -midiin.  The parameter is the key index k = 0..48; the pitch byte comes
	   from it and the transposition switch in recalcula_nota_do_teclado(). */
#define PORT_TECLA(_mascara, _k, _nota) \
	PORT_BIT(_mascara, IP_ACTIVE_HIGH, IPT_OTHER) _nota \
		PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(epusp_synth_device::tecla), _k)

	PORT_START("TECL0")
	PORT_TECLA(0x0001,  0, PORT_GM_C2)
	PORT_TECLA(0x0002,  1, PORT_GM_CS2)
	PORT_TECLA(0x0004,  2, PORT_GM_D2)
	PORT_TECLA(0x0008,  3, PORT_GM_DS2)
	PORT_TECLA(0x0010,  4, PORT_GM_E2)
	PORT_TECLA(0x0020,  5, PORT_GM_F2)
	PORT_TECLA(0x0040,  6, PORT_GM_FS2)
	PORT_TECLA(0x0080,  7, PORT_GM_G2)
	PORT_TECLA(0x0100,  8, PORT_GM_GS2)
	PORT_TECLA(0x0200,  9, PORT_GM_A2)
	PORT_TECLA(0x0400, 10, PORT_GM_AS2)
	PORT_TECLA(0x0800, 11, PORT_GM_B2)
	PORT_TECLA(0x1000, 12, PORT_GM_C3)
	PORT_TECLA(0x2000, 13, PORT_GM_CS3)
	PORT_TECLA(0x4000, 14, PORT_GM_D3)
	PORT_TECLA(0x8000, 15, PORT_GM_DS3)

	PORT_START("TECL1")
	PORT_TECLA(0x0001, 16, PORT_GM_E3)
	PORT_TECLA(0x0002, 17, PORT_GM_F3)
	PORT_TECLA(0x0004, 18, PORT_GM_FS3)
	PORT_TECLA(0x0008, 19, PORT_GM_G3)
	PORT_TECLA(0x0010, 20, PORT_GM_GS3)
	PORT_TECLA(0x0020, 21, PORT_GM_A3)
	PORT_TECLA(0x0040, 22, PORT_GM_AS3)
	PORT_TECLA(0x0080, 23, PORT_GM_B3)
	PORT_TECLA(0x0100, 24, PORT_GM_C4)
	PORT_TECLA(0x0200, 25, PORT_GM_CS4)
	PORT_TECLA(0x0400, 26, PORT_GM_D4)
	PORT_TECLA(0x0800, 27, PORT_GM_DS4)
	PORT_TECLA(0x1000, 28, PORT_GM_E4)
	PORT_TECLA(0x2000, 29, PORT_GM_F4)
	PORT_TECLA(0x4000, 30, PORT_GM_FS4)
	PORT_TECLA(0x8000, 31, PORT_GM_G4)

	PORT_START("TECL2")
	PORT_TECLA(0x0001, 32, PORT_GM_GS4)
	PORT_TECLA(0x0002, 33, PORT_GM_A4)
	PORT_TECLA(0x0004, 34, PORT_GM_AS4)
	PORT_TECLA(0x0008, 35, PORT_GM_B4)
	PORT_TECLA(0x0010, 36, PORT_GM_C5)
	PORT_TECLA(0x0020, 37, PORT_GM_CS5)
	PORT_TECLA(0x0040, 38, PORT_GM_D5)
	PORT_TECLA(0x0080, 39, PORT_GM_DS5)
	PORT_TECLA(0x0100, 40, PORT_GM_E5)
	PORT_TECLA(0x0200, 41, PORT_GM_F5)
	PORT_TECLA(0x0400, 42, PORT_GM_FS5)
	PORT_TECLA(0x0800, 43, PORT_GM_G5)
	PORT_TECLA(0x1000, 44, PORT_GM_GS5)
	PORT_TECLA(0x2000, 45, PORT_GM_A5)
	PORT_TECLA(0x4000, 46, PORT_GM_AS5)
	PORT_TECLA(0x8000, 47, PORT_GM_B5)

	PORT_START("TECL3")
	PORT_TECLA(0x0001, 48, PORT_GM_C6)

#undef PORT_TECLA
INPUT_PORTS_END

ioport_constructor epusp_synth_device::device_input_ports() const
{
	return INPUT_PORTS_NAME(epusp_synth);
}

/* THE INSTRUMENT BRINGS ITS OWN LOUDSPEAKER AND ITS OWN TAPE RECORDER.

   Both used to be built by the Patinho Feio driver, which then handed the tag
   of the recorder back to this device -- so a host machine had to know that
   this instrument records to tape, and had to spell that plumbing out before
   it could sound a note.  That is exactly the coupling this port exists to
   remove: everything that belongs to the instrument is built here, and a host
   configures a connector and three wires.

   TWO CONSEQUENCES A USER WILL NOTICE, both of them correct and neither of
   them obvious:

     - with the port empty there is no speaker in the machine at all, so a
       recorded WAV has fewer channels than it used to.  A machine with no
       instrument attached genuinely has nothing to listen to.

     - the tape recorder's media option ("-cassette") only exists when the
       port is filled, because the image device only exists then.  Asking for
       a cassette without attaching the instrument -- that is, "-cassette
       tape.wav" with no "-io6:duplex:port synth" -- fails with 'unknown
       option', which is a loud failure and the right one: there is nothing to
       put the tape into.

       (This paragraph said "-synthport \"\"" until 2026-08-16.  That option
       belonged to the machine-level connector this driver used to have, and
       which never existed in the real computer; the instrument now hangs off
       the duplex board it was actually cabled to.  See epusp.h.)

   THE RECORDER'S TWO CHANNELS: 0 is the audio, 1 is the 1 kHz sync tone.
   Chapter 15 of the synthesiser manual is explicit that the frame sync is NOT
   recorded, "devido a dificuldades com a resposta em frequencia do gravador",
   so channel 1 carries ticks and nothing else. */
void epusp_synth_device::device_add_mconfig(machine_config &config)
{
	SPEAKER(config, "speaker").front_center();
	add_route(ALL_OUTPUTS, "speaker", 1.0);

	static cassette_image::Options const tape_opts
	{
		2,        // channels: audio and sync
		16,       // bits per sample
		44100     // sample frequency
	};
	CASSETTE(config, m_tape);
	m_tape->set_formats(cassette_default_formats);
	m_tape->set_create_opts(&tape_opts);
	m_tape->set_default_state(CASSETTE_STOPPED);
	m_tape->set_interface("patinho_tape");

	/* AND ITS OWN FRONT PANEL.

	   src/emu/layout/epusp_synth.lay -- a DEVICE layout, which is why it lives
	   under src/emu/layout and not src/mame/layout: it belongs to the
	   instrument, not to the computer, and it must come along whatever machine
	   the instrument is plugged into.  MAME loads it beside the driver's own
	   layout and names the view after this device's tag, so with the
	   instrument on channel /6 it shows up as

	       io6:duplex:port:synth Painel do sintetizador

	   in Tab -> Video Options, and can be asked for straight away with
	   -view "Painel do sintetizador".

	   The panel draws the PGRF waveform, which is the whole point of it: the
	   sixteen sliders are the half cycle, and their heights are the drawing. */
	config.set_default_layout(layout_epusp_synth);
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

/* THE PANEL IS THE ONLY WRITER OF STORE 0.

   Reads the sixteen sliders and lays them into m_store[PGRF], scaled to 0..1
   the same way unpack() scales the computer's planes -- 0..15 out of a unipolar
   converter, so the sample maps to the POSITIVE half and its negation to the
   other.  No mean is subtracted: these sixteen are half a cycle, so the full
   period is odd by construction and its mean is exactly zero.

   Called from the slider handler, from a TRANSFERE press (the panel is the
   source, so it is read fresh), and from device_reset() -- which matters,
   because a waveform restored from cfg/patinho.cfg has to be in the store
   before the first note, and the change handlers only fire on a CHANGE. */
void epusp_synth_device::redesenha_pgrf()
{
	for (unsigned k = 0; k < AMOSTRAS; k++)
		m_store[PGRF][k] = double(m_pgrf_amostra[k]->read() & 0x0F) / 15.0;
}

INPUT_CHANGED_MEMBER(epusp_synth_device::pgrf_alterado)
{
	m_stream->update();
	redesenha_pgrf();
	LOGMASKED(LOG_TIMBRE, "PGRF: amostra %u = %u\n", param + 1, newval);
}

/* TRANSFERE n: the panel into memory n, on the press.

   Chapter 11: "Um timbre programado manualmente nesta entrada pode ser
   transferido (manualmente) para uma de 7 memorias (numeradas de 1 a 7) pelos
   comandos TRANSFERE."  Source is the panel, destination is M1 to M7, and the
   panel itself is never a destination. */
INPUT_CHANGED_MEMBER(epusp_synth_device::transfere)
{
	if (!newval)
		return;   // acts on the press, not on the release

	unsigned const n = param;
	if (n == PGRF || n >= STORES)
		return;   // cannot happen from the port list; cheap to keep true

	m_stream->update();
	redesenha_pgrf();
	std::copy(std::begin(m_store[PGRF]), std::end(m_store[PGRF]), std::begin(m_store[n]));
	LOGMASKED(LOG_TIMBRE, "TRANSFERE %u: painel grafico -> M%u\n", n, n);
}

/* THE 49 KEYS.

   Chapter 3 gives the keyboard's outputs as frf (a square at 32x the note) and
   CHV (0 V with no key down, 5 V otherwise), and chapter 9 shows the computer's
   note interface driving the SAME octave dividers -- so in this device the
   keyboard is simply a second source for the pitch byte and the gate.

   MONOPHONIC, LAST KEY WINS, and on release the highest key still held takes
   over.  NOTHING IN THE MANUAL SAYS WHICH RULE THE INSTRUMENT USED: chapter 6
   is a schematic of 74121 monostables and 7475/7400 latches and chapter 3 says
   only that CHV is 5 V "se nenhuma tecla do manual e acionada ... em caso
   contrario".  This is a declared choice, not a reading.

   L AND D ARE NOT GENERATED.  Chapter 3 defines them as narrow pulses at each
   press ("Controle de inicio de nota -- L") and each release ("Fim de nota
   (D)"), and their only consumers are the envelope generators of chapter 7,
   which are not modelled.  A pulse with nothing on the far end would be
   decoration, so it is not emitted.  VBR, the vibrato control voltage, is not
   modelled either. */
INPUT_CHANGED_MEMBER(epusp_synth_device::tecla)
{
	unsigned const k = param;
	if (k >= TECLAS)
		return;

	m_stream->update();

	uint64_t const bit = uint64_t(1) << k;
	if (newval)
	{
		m_teclas_apertadas |= bit;
		m_tecla_atual = int32_t(k);
		m_tecl_gate = true;
	}
	else
	{
		m_teclas_apertadas &= ~bit;
		if (m_tecla_atual == int32_t(k))
		{
			/* The voice's own key went up.  Hand it to the highest key still
			   held, or drop the gate if the hands left the manual. */
			if (m_teclas_apertadas)
			{
				m_tecla_atual = int32_t(std::bit_width(m_teclas_apertadas)) - 1;
			}
			else
			{
				m_tecl_gate = false;   // this is where D would fire
			}
		}
	}

	recalcula_nota_do_teclado();
	LOGMASKED(LOG_NOTE, "teclado: tecla %u %s -> /%02X, CHV=%d\n",
			k, newval ? "apertada" : "solta", m_tecl_pitch, m_tecl_gate ? 1 : 0);
}

/* The manual-automatico switch and the octave transposition.  Both change what
   the keyboard is saying without any key moving, so both have to push the sound
   stream up to now before they take effect. */
INPUT_CHANGED_MEMBER(epusp_synth_device::teclado_alterado)
{
	m_stream->update();
	recalcula_nota_do_teclado();
}

/* THE PITCH BYTE A KEY PRODUCES.

       n = 12 * transposicao + tecla,   byte = ((n / 12) << 4) | (n % 12)

   which is the same octave-in-the-high-nibble, semitone-in-the-low-nibble code
   commands 0 and 11 carry -- because on the real instrument they reach the same
   dividers.  n runs 0 to 120, the 121 values chapter 3 counts for the manual
   position; see the header comment and
   scripts/sintetizador/teclado_121_valores.py. */
void epusp_synth_device::recalcula_nota_do_teclado()
{
	if (m_tecla_atual < 0)
		return;   // no key has ever been pressed; keep the last byte

	unsigned const n = 12 * (m_tecl_transp->read() & 0x07) + unsigned(m_tecla_atual);
	m_tecl_pitch = uint8_t(((n / 12) << 4) | (n % 12));
}

bool epusp_synth_device::teclado_manda() const
{
	return !(m_tecl_modo->read() & 0x01);   // 0 = MANUAL, 1 = AUTOMATICO
}

/* Which source the note and the gate come from.  The intensity does NOT switch
   with them: INT is D/A 1, a control voltage into the gain-controlled
   amplifier, and on the real instrument it stays wherever the computer or the
   front-panel attenuators left it while the keyboard plays. */
uint8_t epusp_synth_device::nota_corrente() const
{
	return teclado_manda() ? m_tecl_pitch : m_pitch;
}

bool epusp_synth_device::chaveamento_corrente() const
{
	return teclado_manda() ? m_tecl_gate : m_gate;
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
	m_sync_timer = timer_alloc(FUNC(epusp_synth_device::sync_edge), this);
	m_sync_off_timer = timer_alloc(FUNC(epusp_synth_device::sync_off), this);

	save_item(NAME(m_pitch));
	save_item(NAME(m_gate));
	save_item(NAME(m_intensity));
	save_item(NAME(m_timbre));
	save_item(NAME(m_computer_half));
	save_item(NAME(m_store));
	save_item(NAME(m_auto_select));
	save_item(NAME(m_teclas_apertadas));
	save_item(NAME(m_tecla_atual));
	save_item(NAME(m_tecl_gate));
	save_item(NAME(m_tecl_pitch));
	save_item(NAME(m_phase));
	save_item(NAME(m_sync_scan));
	save_item(NAME(m_sync_written));
	save_item(NAME(m_sync_level));
}

/* Where the tape head is, in seconds.  cassette_image_device keeps the
   position for us whether it is playing or recording. */
double epusp_synth_device::tape_position() const
{
	return (m_tape && m_tape->exists()) ? m_tape->get_position() : 0.0;
}

bool epusp_synth_device::tape_moving() const
{
	return m_tape && m_tape->exists()
			&& (m_tape->is_playing() || m_tape->is_recording());
}

/* RECORDING THE 1 kHz.  cassette_image_device already knows how to hold a
   level and paint it into a channel as the tape moves, which is exactly what a
   sync track is.  Pointing it at channel 1 does two jobs at once: it writes the
   tone for us, and it stops its own update() from painting channel 0 -- which
   it would otherwise fill with silence and wipe the audio we are recording. */
void epusp_synth_device::tick_w(int state)
{
	if (!m_tape || !m_tape->exists())
		return;

	/* set_channel() HAS to be done here and not once at start-up, because
	   cassette_image_device::call_load() resets the channel to 0 when an image
	   is mounted.  Setting it in device_start() looked right and was silently
	   undone: the sync tone went to channel 0, on top of the audio, which came
	   out saturated while channel 1 stayed empty. */
	m_tape->set_channel(SYNC_CHANNEL);

	/* THE PULSE NEEDS WIDTH, or there is no tone at all.

	   The time base generator raises and drops its tick in the same instant,
	   which is right for an interrupt line and useless for a tape: the level
	   between the two edges lasts zero seconds, so the recorder paints a
	   constant DC and channel 1 comes back with no edges to recover.  The
	   first recording did exactly that -- full scale RMS, zero crossings.

	   So the tone is built here: high on the tick, low again half a tick
	   later, which lays down a square wave at the tick rate.  SYNC_HALF is
	   500 us because every surviving music tape uses TEMPI = 0, whose tick is
	   1 ms; a tape written at another TEMPI would need this to follow it. */
	if (state)
	{
		m_tape->output(1.0);
		m_sync_off_timer->adjust(SYNC_HALF);
	}
}

TIMER_CALLBACK_MEMBER(epusp_synth_device::sync_off)
{
	if (m_tape && m_tape->exists())
	{
		m_tape->set_channel(SYNC_CHANNEL);
		m_tape->output(-1.0);
	}
}

/* RECOVERING THE 1 kHz.  Scans channel 1 forward for the next rising edge and
   schedules a timer to it, so the tick the CPU sees lands where it was written
   instead of at a sound-stream boundary.  The music's whole timing hangs off
   these edges; quantising them to the audio chunk would be audible. */
void epusp_synth_device::schedule_next_sync()
{
	/* "A fita esta andando" is playing OR recording, and the difference cost a
	   debugging round: an overdub is both at once, but MAME's cassette UI
	   state is one or the other, and is_playing() is FALSE while recording.
	   Guarding the edge hunt on is_playing() alone meant the tape never drove
	   the clock during the very pass that needed it. */
	if (!m_tape || !m_tape->exists() || !tape_moving())
		return;

	cassette_image *const img = m_tape->get_image();
	double const passo = 1.0 / double(std::max(1U, img->get_info().sample_frequency));
	double const limite = m_sync_scan + 0.25;   // um quarto de segundo por busca

	int32_t anterior = 0;
	img->get_sample(SYNC_CHANNEL, m_sync_scan, passo, &anterior);
	for (double t = m_sync_scan + passo; t < limite; t += passo)
	{
		int32_t v = 0;
		img->get_sample(SYNC_CHANNEL, t, passo, &v);
		if (anterior <= 0 && v > 0)
		{
			m_sync_scan = t;
			double const agora = tape_position();
			m_sync_timer->adjust(attotime::from_double(std::max(0.0, t - agora)));
			return;
		}
		anterior = v;
	}
	m_sync_scan = limite;
	m_sync_timer->adjust(attotime::from_double(0.25));
}

TIMER_CALLBACK_MEMBER(epusp_synth_device::sync_edge)
{
	output_sync(1);
	output_sync(0);

	/* THE SYNC TRACK PASSES THROUGH on an overdub, and it has to.

	   MAME's cassette paints its selected channel with a held level whenever
	   it is in RECORD, and pass 2 is in RECORD because that is what makes the
	   image get saved.  Left alone it would flatten the 1 kHz it is reading --
	   the tape would come back with the audio of both voices and no way to
	   record a third.  Re-recording each recovered edge keeps the track alive,
	   which is what a real sync system does with it anyway. */
	tick_w(1);
	m_sync_scan += 0.0002;   // sai da borda que acabou de disparar
	schedule_next_sync();
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
	   reading.

	   SINCE 2026-08-16 STORE 0 IS NO LONGER FILLED HERE: the graphic panel has
	   its sixteen sliders, and redesenha_pgrf() copies whatever they are
	   showing.  The default of every slider is 15, so an untouched machine
	   still comes up with the same square in PGRF as before -- but a waveform
	   drawn by hand, and saved in cfg/patinho.cfg, now survives a restart.

	   Reading the ports here is safe and is the point: MAME loads the cfg
	   before the soft reset, and PORT_CHANGED_MEMBER only fires on a CHANGE, so
	   without this read a restored waveform would sit in the port and never
	   reach the store. */
	static const uint8_t QUADRADA[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	std::copy(std::begin(QUADRADA), std::end(QUADRADA), std::begin(m_timbre));
	unpack(m_timbre, m_computer_half);
	for (auto &s : m_store)
		std::copy(std::begin(m_computer_half), std::end(m_computer_half), std::begin(s));
	redesenha_pgrf();

	/* The keyboard comes up with no key down and no note ever played.  The
	   pitch byte stays 0 until a key is pressed, which is the bottom C -- but
	   it is never heard, because the gate is what decides. */
	m_teclas_apertadas = 0;
	m_tecla_atual = -1;
	m_tecl_gate = false;
	m_tecl_pitch = 0;

	m_auto_select = PGRF;
	m_phase = 0.0;
	m_sync_scan = 0.0;
	m_sync_level = 0;
	m_sync_written = 0.0;
	schedule_next_sync();
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
	/* THE LIVE VOICE FIRST, then the tape on top.

	   Both halves must run on EVERY update, and the reason is the overdub: if
	   this returned early while the note is off, the tape would neither be
	   heard nor rewritten during the rests, and voice 1's audio would come out
	   of the second pass full of holes.  So silence is written as silence, not
	   skipped. */
	/* WHO IS PLAYING: the computer, or the 49 keys.  The manual-automatico
	   switch of chapter 3 decides, and on the real instrument it is a selector
	   in front of the same octave dividers -- so downstream of here there is no
	   difference at all between the two sources. */
	double const f = frequency(nota_corrente());
	bool const soando = chaveamento_corrente() && m_intensity != 0
			&& f > 0.0 && f < stream.sample_rate() / 2.0;

	if (!soando)
	{
		stream.fill(0, 0.0);
	}
	else
	{
		/* THE WAVETABLE.  The store holds sixteen samples that are HALF a
		   cycle of an odd waveform, so the period is 32 steps: the sixteen
		   forwards, then the same sixteen negated.  Chapter 3 states it twice
		   (the panel draws "meio ciclo de uma forma de onda impar"; SAU is a
		   "funcao impar cujo primeiro semi-ciclo e o registrado") and the
		   clock proves it a third time -- frl is "32 vezes a da forma de onda
		   de saida", and 32 = 16 x 2.

		   Earlier attempts fed the sixteen samples in as a whole cycle and had
		   to subtract their mean to stop a DC offset from swamping the signal;
		   a half-range ramp read that way sits off-centre, went silent, and
		   looked like a missing indirection in GRTMB/LETMB.  It was not: it
		   was the half cycle.

		   A FRACTIONAL PHASE ACCUMULATOR, not an integer step counter.  The
		   first square-wave version counted down from int(rate / (2*f)), which
		   quantises the period to whole samples: every note came out up to
		   1.3% sharp, and scripts/sintetizador/analisar_wav.py in the
		   PatinhoFeio repository saw a systematic error in the same direction
		   on every note.

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

	mix_tape(stream);
}

/* THE OVERDUB, on channel 0.

   What comes out of the speaker while the tape runs is the sum of what is
   already on the tape and what the synthesiser is playing now -- which is what
   the musician heard, and what the recorder wrote if it was in record.  So:

       ouvido = fita + vivo
       fita   = ouvido        (quando gravando)

   Reading before writing is what makes it an OVERDUB and not an erase: voice 2
   adds to voice 1 instead of replacing it.  The sum is clipped, because a real
   recorder saturates and silently wrapping would sound like a fault that is not
   there. */
void epusp_synth_device::mix_tape(sound_stream &stream)
{
	if (!m_tape || !m_tape->exists())
		return;

	bool const gravando = m_tape->is_recording();
	if (!tape_moving())
		return;

	/* The device's own record path writes m_channel, so it must point at the
	   sync track at all times -- not only when a tick happens to arrive.  In
	   the overdub pass no internal tick ever fires, and with the channel left
	   at 0 the recorder quietly wiped the audio it was supposed to be adding
	   to. */
	m_tape->set_channel(SYNC_CHANNEL);

	cassette_image *const img = m_tape->get_image();
	double const pos = m_tape->get_position();
	double const passo = 1.0 / double(stream.sample_rate());

	/* START THE EDGE HUNT HERE, and not at reset.

	   device_reset() runs before any tape is mounted -- the harness loads the
	   image after the machine is up -- so a scan armed there finds no image,
	   returns, and is never armed again.  The first overdub ran with the
	   internal oscillator doing all the work and looked perfectly fine: the
	   music was right, because both clocks are exactly 1 kHz in emulation.
	   Only the counter of ticks-taken-from-tape showed it, which is why that
	   counter exists. */
	if (!m_sync_timer->enabled())
	{
		m_sync_scan = pos;
		schedule_next_sync();
	}
	int const n = stream.samples();

	for (int i = 0; i < n; i++)
	{
		double const t = pos + i * passo;
		int32_t bruto = 0;
		img->get_sample(AUDIO_CHANNEL, t, passo, &bruto);
		double const fita = double(bruto) / 2147483648.0;

		/* THE RECORDING LEVEL, which is a knob the musician set and nobody
		   wrote down.  A voice recorded at full scale leaves no room for the
		   next one, and the overdub would clip on the very first note.  Half
		   scale per voice lets two sum to full, which is what the surviving
		   scores need -- they come in "1a. VOZ" and "2a. VOZ" pairs.  Declared
		   choice, not a reading. */
		double soma = fita + stream.get_output(0, i) * RECORD_LEVEL;
		soma = std::clamp(soma, -1.0, 1.0);

		stream.put(0, i, soma);
		if (gravando)
			img->put_sample(AUDIO_CHANNEL, t, passo, int32_t(soma * 2147483000.0));
	}
}
