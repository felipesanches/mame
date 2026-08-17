// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Sound synthesiser of the EPUSP, by Guido Stolfi -- first stage

    See synth.h for the command set and where it comes from.

***************************************************************************/

#include "emu.h"
#include "synth.h"

#include "bus/midi/midi.h"
#include "bus/midi/midiinport.h"

#include "speaker.h"

#include "epusp_synth.lh"

#include <bit>
#include <cmath>

#define LOG_CMD    (1U << 1)   // every command that arrives
#define LOG_NOTE   (1U << 2)   // only the notes, with the frequency
#define LOG_TIMBRE (1U << 3)   // timbre loads, and whether S1 can hear them
#define LOG_MIDI   (1U << 4)   // bytes arriving from the host MIDI controller

#define VERBOSE (0)
#include "logmacro.h"

DEFINE_DEVICE_TYPE(EPUSP_SYNTH, epusp_synth_device, "epusp_synth", "EPUSP sound synthesiser (Guido Stolfi)")

epusp_synth_device::epusp_synth_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, EPUSP_SYNTH, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, device_serial_interface(mconfig, *this)
	, device_epusp_synth_port_interface(mconfig, *this)
	, m_s1(*this, "S1")
	, m_pgrf_amostra(*this, "AM%u", 0U)
	, m_tecl_modo(*this, "TECL")
	, m_tecl_transp(*this, "TRANSP")
	, m_tecl_porta(*this, "TECL%u", 0U)
	, m_tape(*this, "tape")
{
	std::fill(std::begin(m_timbre), std::end(m_timbre), 0);
	std::fill(std::begin(m_computer_half), std::end(m_computer_half), 0.0);
	for (auto &s : m_store)
		std::fill(std::begin(s), std::end(s), 0.0);
}

/* The mode/selection control of chapter 3: "Chave de 2 posicoes (AUTO --
   MANUAL) + chave de 8 posicoes (PGRF-M1-M2...M7)", the nine-position switch
   chapter 11 counts.  A machine configuration rather than a DIP switch: it is
   a front-panel control turned between pieces.

   The default "MANUAL: PGRF" is a declared choice.  On AUTO the computer's
   LETMB decides, and FITA#023 then stores an all-zero waveform into M7 while
   a note is still gated on, chopping it in two; chapter 3 says a memory
   switched to BLOQUEIA keeps its waveform regardless of the computer's
   commands, so no document settles which of the two happened. */
static INPUT_PORTS_START(epusp_synth)
	/* PORT_TOGGLE is what lets a layout item bound to this field turn the
	   knob: frame_update() answers set_value(1) with select_next_setting()
	   only on a toggle field (src/emu/ioport.cpp).  A toggle CONFNAME folds
	   its value into the port default and reports no digital bit of its own,
	   so read() is unchanged; same pattern as atari/a2600.cpp. */
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

	/* The graphic panel (PGRF): sixteen samples of four bits each.
	   Chapter 3: "Conjunto de 16 chaves deslizantes de 16 posicoes cada uma
	   no qual se desenha meio ciclo de uma forma de onda impar" -- one
	   control here per slider.  Chapter 11 counts the same panel from the
	   circuit side, "4 grupos de 16 chaves de selecao de timbres", which is
	   64 signals and not 64 switches: one sixteen-position slider emits four
	   bits, one per bit plane.

	   The sixteen are half a cycle and feed store 0 only;
	   sound_stream_update() plays them forwards and then negated, sample 0
	   first, the same ordering commands 1 to 8 use.  The default of 15 on
	   every slider is the all-maximum half cycle, i.e. a square: a declared
	   choice, since the tapes select this store without ever writing it.  The
	   values are saved in the cfg file, so a hand-drawn waveform survives
	   between sessions. */
	/* Samples are numbered 0 to 15, as the front panel prints under each
	   slider and as every index here means: m_store[s][k], the _n below, bit
	   15-k of a bit plane. */
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

	/* The seven TRANSFERE switches.  Chapter 11: "Um timbre programado
	   manualmente nesta entrada pode ser transferido (manualmente) para uma
	   de 7 memorias (numeradas de 1 a 7) pelos comandos TRANSFERE", and a bit
	   unit's parts list counts "7 chaves de transferencia".  Source is the
	   panel, destination M1 to M7; the panel is never a destination, which is
	   also why command_w() refuses GRTMB,0.  Momentary and not toggles: the
	   chapter calls them commands, and the transfer happens on the press. */
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

	/* The keyboard of chapter 6 and its two switches from chapter 3.  "Chave
	   manual--automatico" decides who supplies the note and the gate: the 49
	   keys, or the computer's commands 0 and 11 -- chapter 3, on CHV,
	   "0 Volts para 'nota silenciosa' e 5 Volts para 'nota'".  The default is
	   AUTOMATICO because every surviving tape is the computer playing. */
	PORT_START("TECL")
	PORT_CONFNAME(0x01, 0x01, "Teclado: chave manual-automatico") PORT_TOGGLE
		PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(epusp_synth_device::teclado_alterado), 0)
	PORT_CONFSETTING(0x00, "MANUAL (as 49 teclas mandam)")
	PORT_CONFSETTING(0x01, "AUTOMATICO (o computador manda)")

	/* "Chave de 7 posicoes para transposicao de oitavas" (chapter 3).  With
	   49 keys and seven positions twelve semitones apart the reachable codes
	   are n = 12p + k, k = 0..48, p = 0..6: 0 to 120, the 121 values
	   chapter 3 counts for the manual position, topping out at 16.3516 x 2^10
	   = 16 743.9 Hz ("16 744,0 Hz" there).  Default position 2 makes the key
	   MAME names C2 sound 16.3516 x 2^2 = 65.4 Hz, so label and pitch
	   agree. */
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

	/* "Manual de 49 teclas" (chapter 3), C2 to C6 in MAME's General MIDI
	   naming; the parameter is the key index k = 0..48.  PORT_GM_xx only sets
	   the field name (field_set_gm_note does nothing else), so it is not what
	   makes the keys reachable from -midiin; the receiver built in
	   device_add_mconfig() is.  It does make the numbering line up: the first
	   key is C2 = 36, so midi_byte() maps note n to contact n - 36. */
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

/* The instrument brings its own loudspeaker and its own tape recorder, so a
   host machine only has to configure the connector and three wires.  Two
   visible consequences: with the port empty there is no speaker in the
   machine at all, so a recorded WAV has fewer channels; and the recorder's
   "-cassette" option only exists when the port is filled, so asking for a
   cassette without attaching the instrument fails with 'unknown option'.

   Recorder channels: 0 is the audio, 1 is the 1 kHz sync tone.  Chapter 15 of
   the synthesiser manual states that the frame sync is not recorded, "devido
   a dificuldades com a resposta em frequencia do gravador", so channel 1
   carries ticks and nothing else. */
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

	/* A device layout, which is why it lives in src/emu/layout and not
	   src/mame/layout: it belongs to the instrument and must follow it into
	   whatever machine it is plugged into.  MAME names the view after this
	   device's tag, so with the instrument on channel /6 it appears as
	   "io6:duplex:port:synth Painel do sintetizador". */
	config.set_default_layout(layout_epusp_synth);

	/* Host plumbing that closes and opens the 49 key contacts; not a socket
	   of the instrument.  It ends in aciona_tecla(), where the PC keyboard
	   and the layout also end, so the octave transposition and
	   manual-automatico switches apply unchanged.  rxd carries bits, not
	   bytes: the instrument has no UART, so this device's
	   device_serial_interface assembles them at MIDI's 31250 bps 8N1.

	   The slot is fixed deliberately -- configured the usual way, bus/midi's
	   midi_port_device would advertise a MIDI socket this 1975 instrument
	   never had.  option_set() marks it fixed, and every place that offers
	   slots to the user skips fixed slots (clifront.cpp -listslots,
	   emuopts.cpp, ui/slotopt.cpp), so no slot option is created; the image
	   option "-midiin" is unaffected.  The port still appears in
	   -listdevices, hence the tag "kbdmidi" rather than the customary "mdin",
	   which would read as a panel socket. */
	midi_port_device &kbdmidi(MIDI_PORT(config, "kbdmidi"));
	kbdmidi.option_set("midiin", MIDIIN_PORT);
	kbdmidi.set_display_name("Entrada MIDI do hospedeiro (toca as 49 teclas)");
	kbdmidi.rxd_handler().set(*this, FUNC(epusp_synth_device::midi_rxd_w));
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

/* The only writer of store 0.  Reads the sixteen sliders into m_store[PGRF],
   scaled to 0..1 as unpack() scales the computer's planes (0..15 out of a
   unipolar converter).  No mean is subtracted: the sixteen are half a cycle,
   so the period is odd by construction and its mean is exactly zero.  Also
   called from device_reset(): the change handlers only fire on a change, so a
   waveform restored from the cfg file would not otherwise reach the store. */
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

/* TRANSFERE n: the panel into memory n, on the press.  Chapter 11 -- the
   source is the panel, the destination M1 to M7, never the panel itself. */
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

/* Chapter 3 gives the keyboard's outputs as frf (a square at 32x the note)
   and CHV (0 V with no key down, 5 V otherwise), and chapter 9 shows the
   computer's note interface driving the same octave dividers, so here the
   keyboard is a second source for the pitch byte and the gate.  Monophonic,
   last key wins, and on release the highest key still held takes over: a
   declared choice, as chapters 3 and 6 say nothing about the rule.  L and D,
   the narrow start- and end-of-note pulses of chapter 3, and VBR are not
   emitted -- their only consumers are the chapter 7 envelope generators,
   which are not modelled. */
INPUT_CHANGED_MEMBER(epusp_synth_device::tecla)
{
	aciona_tecla(param, newval != 0);
}

/* The contact itself, reached by a key of the PC keyboard, a click on the
   layout and a host MIDI note alike.  Idempotent: asking for the state the
   contact already has returns without touching the sound stream, which is
   what lets the MIDI path both act at once and push the ioport field, whose
   own change handler then arrives a frame later as a no-op. */
void epusp_synth_device::aciona_tecla(unsigned k, bool apertada)
{
	if (k >= TECLAS)
		return;

	uint64_t const bit = uint64_t(1) << k;
	if (bool(m_teclas_apertadas & bit) == apertada)
		return;

	m_stream->update();

	if (apertada)
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
			k, apertada ? "apertada" : "solta", m_tecl_pitch, m_tecl_gate ? 1 : 0);
}

/* One byte at a time, assembled by this device's device_serial_interface from
   the bits midi_port delivers, ending in aciona_tecla() and in a push on the
   ioport field a PC key presses.  Accepted: note-on and note-off on any
   channel for the 49 notes there are contacts for (C2 = 36 to C6 = 84).
   Running status is honoured; real-time bytes (>= 0xF8) leave it undisturbed
   and system-common bytes clear it, as the standard requires.  Velocity,
   channel and after-touch are discarded -- chapter 3 gives this keyboard only
   frf and a gate CHV of 0 or 5 V -- and velocity is read solely for the
   note-on velocity 0 spelling of a release.  Notes outside the 49 are dropped
   rather than folded into range; the octave transposition switch is what
   moves the window. */
void epusp_synth_device::midi_byte(uint8_t b)
{
	LOGMASKED(LOG_MIDI, "midi: %02X\n", b);

	if (BIT(b, 7))
	{
		if (b >= 0xF8)
			return;                                // real time: no effect on running status
		m_midi_status = (b < 0xF0) ? b : 0;        // system common clears running status
		m_midi_tem_nota = false;
		return;
	}

	uint8_t const cmd = m_midi_status & 0xF0;      // the channel nibble is dropped on purpose
	if (cmd != 0x90 && cmd != 0x80)
		return;                                    // only note-on and note-off

	if (!m_midi_tem_nota)
	{
		m_midi_nota = b;
		m_midi_tem_nota = true;
		return;
	}

	uint8_t const nota = m_midi_nota;
	bool const apertada = (cmd == 0x90) && (b != 0);   // b is the velocity, and this is its ONLY use
	m_midi_tem_nota = false;                           // ready for the next note in running status

	if ((nota < MIDI_NOTA_BASE) || (nota >= MIDI_NOTA_BASE + TECLAS))
		return;                                    // no contact there

	unsigned const k = nota - MIDI_NOTA_BASE;
	aciona_tecla(k, apertada);

	/* And push the same field the PC keyboard presses, so the key moves on the
	   layout too.  set_value() is an override ORed with the physical input
	   (ioport.cpp: "curstate = m_digital_value || seq_pressed"), so a key held
	   by hand is not released by a MIDI note-off of the same key. */
	if (ioport_field *const campo = m_tecl_porta[k / 16]->field(ioport_value(1) << (k % 16)))
		campo->set_value(apertada ? 1 : 0);
}

void epusp_synth_device::rcv_complete()
{
	receive_register_extract();
	midi_byte(get_received_char());
}

/* The manual-automatico switch and the octave transposition.  Both change what
   the keyboard is saying without any key moving, so both have to push the sound
   stream up to now before they take effect. */
INPUT_CHANGED_MEMBER(epusp_synth_device::teclado_alterado)
{
	m_stream->update();
	recalcula_nota_do_teclado();
}

/* The pitch byte a key produces is n = 12 * transposicao + tecla encoded as
   ((n / 12) << 4) | (n % 12) -- the same octave-in-the-high-nibble,
   semitone-in-the-low-nibble code commands 0 and 11 carry, because on the
   instrument they reach the same dividers.  n runs 0 to 120, the 121 values
   chapter 3 counts for the manual position. */
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

/* Chapter 3 puts the scale between "o do de 16,35 Hz" and "o si de
   15 804,3 Hz" -- ten octaves of twelve semitones -- and the pitch byte holds
   the octave in the high nibble and the semitone in the low one, so /00 is
   that bottom C and f = 16.3516 * 2^(octave + semitone/12).  The low nibble
   runs 0 to 11; 12 to 15 are not notes and occur in no surviving tape, so
   reaching the clamp below means something upstream is wrong. */
double epusp_synth_device::frequency(uint8_t code)
{
	unsigned const octave = code >> 4;
	unsigned const semitone = code & 0x0F;
	return 16.3516 * std::pow(2.0, double(octave) + double(std::min(semitone, 11U)) / 12.0);
}

/* Sixteen 4-bit samples sent as four bit planes.  Commands 1 to 8 carry four
   16-bit words, most significant plane first, each split into high byte then
   low byte; sample k contributes its bit b to bit (15-k) of plane b, so the
   first sample is the most significant bit of each word.  The tapes pin the
   order down: "TIMBRE,0011223344556677" in the score FITA#020 appears in the
   digitised object FITA#023 as /01../08 = 00 00 00 FF 0F 0F 33 33, which
   unpacks to the ramp 0,0,1,1,...,7,7.

   The sixteen are half a cycle and not a whole one: chapter 3 has the panel
   drawing "meio ciclo de uma forma de onda impar" and SAU as a "funcao impar
   cujo primeiro semi-ciclo e o registrado", and frl is "32 vezes a da forma
   de onda de saida", 32 = 16 x 2.  So this returns the first half cycle only,
   and the odd period it extends to has mean exactly zero: subtracting a mean
   would be wrong.  Samples are unsigned 0..15 out of a unipolar converter
   (chapter 1: 0 V for 00000000, +5 V for 11111111) with SAU swinging -5 to
   +5 V, so a sample maps to the positive half and its negation to the
   other. */
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

	/* device_serial_interface saves the bits of the byte in flight, but not
	   the bytes this parser has already assembled.  Without these three, a
	   state loaded in the middle of a message comes back with no running
	   status and no pending note number: the next note in running status is
	   dropped, and a velocity byte can be taken for a note number, sounding a
	   note nobody played. */
	save_item(NAME(m_midi_status));
	save_item(NAME(m_midi_nota));
	save_item(NAME(m_midi_tem_nota));

	/* Not registered here, and why: m_stream and the two timers are pointers
	   whose owners save themselves (sound_stream in src/emu/disound.cpp; each
	   timer's period, expiry and enabled flag in emu_timer::register_save()).
	   Both timers are allocated unconditionally here with named FUNC()s,
	   which is what makes the saved index mean the same thing on reload, and
	   mix_tape() re-arms m_sync_timer whenever it finds it disabled with the
	   tape moving.  m_tape is a finder, and cassette_image_device saves its
	   own state, position included.

	   The ioport finders are absent because MAME puts no ioport in a save
	   state: they are live host input, and their settings persist in the cfg
	   file instead.  So after a load the sound follows the saved
	   m_store[PGRF] while the sliders still show their current positions, and
	   the first slider moved calls redesenha_pgrf(), replacing the restored
	   drawing.  The tape samples are a host file and are not in the state
	   either, so a rewind or re-record between save and load leaves the sync
	   timer pointing at an edge that is no longer there. */
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

/* Recording the 1 kHz.  cassette_image_device holds a level and paints it
   into its selected channel as the tape moves; pointing it at channel 1 also
   stops its own update() from filling channel 0 with silence and wiping the
   audio. */
void epusp_synth_device::tick_w(int state)
{
	if (!m_tape || !m_tape->exists())
		return;

	/* cassette_image_device::call_load() resets the channel to 0 when an
	   image is mounted, so set_channel() has to be repeated here; done once
	   in device_start() it would leave the sync tone on channel 0, over the
	   audio. */
	m_tape->set_channel(SYNC_CHANNEL);

	/* The pulse needs width: the time base generator raises and drops its
	   tick in the same instant, so the level between the edges lasts zero
	   seconds, the recorder paints constant DC, and channel 1 has no
	   recoverable edges.  The tone is built here instead -- high on the tick,
	   low again half a tick later -- a square wave at the tick rate.
	   SYNC_HALF is 500 us because every surviving music tape uses TEMPI = 0,
	   whose tick is 1 ms; another TEMPI would need this to follow it. */
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
	/* An overdub is playing and recording at once, but MAME's cassette state
	   is one or the other and is_playing() is false while recording, so the
	   edge hunt must be guarded on tape_moving() and not on is_playing(). */
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

	/* The sync track has to pass through on an overdub: MAME's cassette
	   paints its selected channel with a held level whenever it is in RECORD,
	   and the overdub pass is in RECORD, so re-recording each recovered edge
	   is what keeps the 1 kHz alive for a further pass. */
	tick_w(1);
	m_sync_scan += 0.0002;   // sai da borda que acabou de disparar
	schedule_next_sync();
}

void epusp_synth_device::device_reset()
{
	m_pitch = 0;
	m_gate = false;

	/* Intensity comes up at full scale, a declared choice.  The mixer treats
	   intensity 0 as silence, and two of the four surviving music tapes never
	   send command 12 at all: FITA#015, the Bachianinha, uses only /0B and
	   /00 for all 432 of its notes, and FITA#024 likewise.  INT is D/A 1, a
	   control voltage into the gain-controlled amplifier, so such a tape
	   played at whatever the front-panel attenuators (POTM, chapter 3) were
	   left at, and those settings did not survive. */
	m_intensity = 0xFF;

	/* Every store comes up holding a square -- the first half cycle all at
	   maximum, whose odd extension is the square wave.  A declared choice:
	   FITA#023 selects PGRF with "LETMB,0" for most of the piece and the
	   hand-set panel drawing is not on the tape, and FITA#015, the
	   Bachianinha, sends no TIMBRE command at all, so it played with whatever
	   the previous session had left in the stores.  Both would be silent from
	   a zero store.

	   Store 0 comes from the sliders instead, via redesenha_pgrf(); their
	   default of 15 is the same square.  Reading the ports here is required:
	   MAME loads the cfg file before the soft reset and PORT_CHANGED_MEMBER
	   only fires on a change, so a restored waveform would otherwise never
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

	/* MIDI is 31250 bps 8N1 and the line idles high.  rx_w(1) is required,
	   not decoration: the receiver arms on a 1 -> 0 edge and its shift
	   register comes up all zeros, so without being told the idle level once
	   it would swallow the first byte of the session while hunting for a
	   start bit. */
	set_data_frame(1, 8, PARITY_NONE, STOP_BITS_1);
	set_rate(31250);
	rx_w(1);
	m_midi_status = 0;
	m_midi_nota = 0;
	m_midi_tem_nota = false;

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
		/* Armazenamento de timbre: the computer's own store, sixteen 4-bit
		   samples transposed into four bit planes (see unpack()).  These
		   eight bytes are the serial fill of chapter 11's eighth memory
		   through en1/en0 -- 16 x 4 = 64 bits -- and do not reach the output
		   until GRTMB moves them. */
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
	/* The live voice first, then the tape on top.  Both halves must run on
	   every update: returning early while the note is off would leave the
	   tape neither heard nor rewritten during the rests, punching holes in
	   voice 1 on the second pass. */
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
		/* The store holds half a cycle of an odd waveform, so the period is
		   32 steps: the sixteen forwards, then the same sixteen negated
		   (chapter 3; frl is "32 vezes a da forma de onda de saida", and
		   32 = 16 x 2).

		   A fractional phase accumulator, not an integer step counter:
		   counting whole samples per half period quantises the period and
		   puts notes up to 1.3% sharp.  No interpolation, because the
		   instrument steps its D/A once per frl edge and holds -- zero-order
		   hold. */
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

/* The overdub, on channel 0: what the speaker gives while the tape runs is
   the sum of tape and live voice (ouvido = fita + vivo), and when recording
   that sum goes back to the tape (fita = ouvido).  Reading before writing is
   what makes this an overdub and not an erase; the sum is clipped because a
   real recorder saturates and a silent wrap would sound like a fault. */
void epusp_synth_device::mix_tape(sound_stream &stream)
{
	if (!m_tape || !m_tape->exists())
		return;

	bool const gravando = m_tape->is_recording();
	if (!tape_moving())
		return;

	/* The record path writes m_channel, so it must point at the sync track at
	   all times and not only when a tick arrives: no internal tick fires
	   during an overdub, and with the channel at 0 the recorder wipes the
	   audio it should add to. */
	m_tape->set_channel(SYNC_CHANNEL);

	cassette_image *const img = m_tape->get_image();
	double const pos = m_tape->get_position();
	double const passo = 1.0 / double(stream.sample_rate());

	/* The edge hunt starts here, not at reset: device_reset() runs before any
	   tape is mounted, so a scan armed there finds no image, returns, and is
	   never armed again.  The failure is inaudible, both clocks being exactly
	   1 kHz in emulation, so only the count of ticks taken from tape reveals
	   it. */
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

		/* The recording level is a knob nobody wrote down; half scale per
		   voice lets two sum to full without clipping, which is what the
		   surviving scores need -- they come in "1a. VOZ" and "2a. VOZ"
		   pairs.  Declared choice, not a reading. */
		double soma = fita + stream.get_output(0, i) * RECORD_LEVEL;
		soma = std::clamp(soma, -1.0, 1.0);

		stream.put(0, i, soma);
		if (gravando)
			img->put_sample(AUDIO_CHANNEL, t, passo, int32_t(soma * 2147483000.0));
	}
}
