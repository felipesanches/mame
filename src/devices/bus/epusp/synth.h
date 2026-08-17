// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Sound synthesiser of the EPUSP, by Guido Stolfi

    Control interface only, made audible with a square wave: no VCF,
    envelopes, portamento or vibrato.  What is modelled is the command set
    of chapter 5 of the synthesiser manual, "Comandos do Computador":

         0   NOTA TOCADA          pitch, with the gate on
         1-8 armazenamento de timbre (TIMBRE)
         9   GRAVA TIMBRE (GRTMB)
        10   LE TIMBRE (LETMB)
        11   nota SILENCIOSA      the same pitch byte, gate off
        12   D/A 1 (INT.)         intensity
        13   D/A 2
        14   D/A 3
        15   D/A 1,2,3
        16-19 D/A 4, 5, 6 and 4,5,6
        20   PORTAMENTO
        21-24 ENVL. 1 to 4
        31   dados genericos na interface = SINC
       255   NOP

    Of those, 0, 11 and 12 sound; the rest are decoded and stored only.  The
    pitch code is not MIDI: nibble alto = oitava, nibble baixo = semitom,
    over the ten octaves of twelve semitones chapter 3 places between "o do
    de 16,35 Hz" and "o si de 15 804,3 Hz":

        f(codigo) = 16.3516 Hz * 2^( (codigo >> 4) + (codigo & 15) / 12 )

    so /39 -> 220.0 Hz and /40 -> 261.6 Hz, the LA,4 and DO,5 of FITA#020.

    A timbre's sixteen samples are half a cycle, not a whole one: chapter 3
    has "16 chaves deslizantes de 16 posicoes ... no qual se desenha MEIO
    CICLO de uma forma de onda IMPAR", and clock frl runs at "32 VEZES" the
    output = 16 samples x 2 half-cycles.  The period is w(k) then -w(k), odd
    and of zero mean, so no DC offset is to be subtracted; SAU is -5 to +5 V.

    Chapter 3 gives the timbre selector as "chave de 2 posicoes (AUTO --
    MANUAL) + chave de 8 posicoes (PGRF-M1-M2...M7)": nine positions, hence
    seven memories and a graphic panel, not eight.  Its "LEIA" and
    "TRANSFIRA" are LETMB (10) and GRTMB (9); commands 1 to 8 fill the
    computer's store, GRTMB copies it into memory n, and LETMB picks the
    store heard while the selector is on AUTO.

    Store 0 is PGRF, the graphic panel, drawn by hand; FITA#023 plays most of
    its length with "LETMB,0" and that drawing did not survive, so PGRF comes
    up holding a square -- a declared modelling choice.  A memory's
    AUTO/TRANSF. -- BLOQUEIA switch keeps "a ultima forma de onda armazenada
    [...] inalterada"; write protection is unrecorded, so all are writable.

    The real panel is 64 toggle switches, chapter 11's "16 chaves de entrada"
    on each of four bit units, one bit of one sample each; modelled here as
    sixteen 16-position sliders of identical information content, a declared
    usability choice.  One IPT_ADJUSTER each (as in fixfreq.cpp) keeps a
    drawing in cfg and in Tab -> Slider Controls without layout or plugin.
    The seven TRANSFERE switches ("7 chaves de transferencia" per bit unit)
    copy the panel into memory n, the manual twin of GRTMB, which copies the
    computer's store (chapter 11's "8.a memoria", filled serially through
    en1/en0).  The panel is a physical control and never a destination.

    The 49-key manual, from chapter 6 (schematics) and chapter 3 (function),
    outputs frf (a square wave at 32x the note) and CHV (0 V with no key
    down, 5 V with one); chapter 9 labels the note interface decoder output
    "(Ao teclado -- oitava mais alta)", so the computer injects into the very
    octave dividers the keys drive, and the manual-automatico switch selects
    which of the two supplies note and gate.  With 49 keys (k = 0..48) and
    the seven-position octave transposition (p = 0..6, twelve semitones),

        n = 12 * p + k,   pitch byte = ((n / 12) << 4) | (n % 12)

    which spans the 121 values chapter 3 counts for the manual position
    ("entre 121 (manualmente) ou 120 (automaticamente) valores"), topped by
    16.3516 x 2^10 = 16 743.9 Hz, chapter 3's "16 744,0 Hz"; automatic gets
    120 because the byte holds octave 0-9 and semitone 0-11.  That is a
    derivation from those two numbers, not a reading.  Neither L and D, the
    pulses that fire the chapter 7 envelopes, nor VBR, the vibrato voltage,
    is generated.  The monophonic rule is undocumented and so a declared
    choice: last key wins, and on release the highest key still held.

    A host MIDI controller is an input device of the emulator, not a MIDI
    socket this instrument acquired: the receiver ends in aciona_tecla(),
    where a PC key and a layout click also end, so a note goes through the
    same switches a finger does.  Velocity, channel and after-touch are
    ignored because CHV is a contact giving 0 V or 5 V; only note-on velocity
    0 = note-off is honoured.  A plain MIDI_PORT() would advertise a socket
    the hardware has not got, so option_set() marks the slot fixed, which
    clifront.cpp, emuopts.cpp and ui/slotopt.cpp skip; "-midiin" survives as
    an image option, and with no source selected no byte arrives.

    Deliberately absent: the general-purpose potentiometers of chapter 17
    (no fixed function, no analogue block to feed) and any waveform display.

***************************************************************************/
#ifndef MAME_BUS_EPUSP_SYNTH_H
#define MAME_BUS_EPUSP_SYNTH_H

#pragma once

#include "epusp.h"

#include "imagedev/cassette.h"

// For the host MIDI input path: the byte assembly at 31250 bps 8N1.  It models
// no hardware of this instrument -- see the note above.
#include "diserial.h"


// ======================> epusp_synth_device

class epusp_synth_device : public device_t, public device_sound_interface, public device_serial_interface, public device_epusp_synth_port_interface
{
public:
	epusp_synth_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// One transfer from the coupled duplex boards: high byte = command
	// register (channel /6), low byte = data register (channel /7).
	virtual void command_w(uint16_t pair) override;

	// Frequency of a pitch code, in Hz.  Public because the regression test
	// wants to check it without going through the sound stream.
	static double frequency(uint8_t code);

	/* The tape recorder hangs off the synthesiser, not off a computer
	   channel, so it is a subdevice built in device_add_mconfig().  The
	   pieces were overdubbed, kept in time by a two-channel tape: channel 0
	   the audio, channel 1 a 1 kHz tone.  Voice 1 has the computer's
	   oscillator run the music ("FNC /41", AVISA QUE A INTERFACE MANDA) and
	   lays the 1 kHz onto channel 1; voice 2 plays the tape back, feeds the
	   recovered 1 kHz to the time base generator ("FNC /42", AVISA QUE O
	   SINTETIZADOR MANDA) and mixes into channel 0, slaved to voice 1. */

	// The host's time base, offered for recording onto channel 1.
	virtual void tick_w(int state) override;

	// Front-panel controls.  All four are reached only from the input port
	// definitions, but MAME needs them public for the delegates.
	DECLARE_INPUT_CHANGED_MEMBER(pgrf_alterado);   // one of the sixteen sliders
	DECLARE_INPUT_CHANGED_MEMBER(transfere);       // TRANSFERE n: panel -> Mn
	DECLARE_INPUT_CHANGED_MEMBER(tecla);           // one of the 49 keys
	DECLARE_INPUT_CHANGED_MEMBER(teclado_alterado); // manual/auto, transposition

	// The eight selectable waveform stores: index 0 is PGRF, the graphic
	// panel, and 1 to 7 are memories M1 to M7.
	static constexpr unsigned STORES = 8;
	static constexpr unsigned PGRF = 0;

	// S1 positions.  Nine of them: AUTO, then PGRF, then M1 to M7.
	static constexpr unsigned S1_AUTO = 0;

	// The sixteen samples of the graphic panel, and the "Manual de 49 teclas"
	// of chapter 3.
	static constexpr unsigned AMOSTRAS = 16;
	static constexpr unsigned TECLAS = 49;

	// The 49 keys are named C2 to C6 in the General MIDI naming the port list
	// uses, so a host controller's note n closes contact n - 36.  Nothing else
	// about MIDI reaches the instrument; see the header comment.
	static constexpr uint8_t MIDI_NOTA_BASE = 36;   // C2

	// Channel 0 is the audio, channel 1 the 1 kHz. Chapter 15: the frame sync
	// is NOT recorded, "devido a dificuldades com a resposta em frequencia do
	// gravador", so channel 1 carries ticks and nothing else.
	static constexpr int AUDIO_CHANNEL = 0;
	static constexpr int SYNC_CHANNEL = 1;

	// Half of the 1 ms tick every surviving tape uses (TEMPI = 0).
	static inline attotime const SYNC_HALF = attotime::from_usec(500);

	// How loud each voice goes onto the tape. See mix_tape().
	static constexpr double RECORD_LEVEL = 0.5;

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;
	virtual ioport_constructor device_input_ports() const override ATTR_COLD;
	virtual void sound_stream_update(sound_stream &stream) override;

	// A whole MIDI byte has arrived on the host input path.
	virtual void rcv_complete() override;

private:
	/* The one place a key contact opens or closes: PC keyboard, layout click
	   and host MIDI all end here.  Idempotent, because the MIDI path acts at
	   once and also pushes the ioport field, whose handler repeats the call. */
	void aciona_tecla(unsigned k, bool apertada);

	// Host MIDI input: line level -> serial receiver, and byte -> key contact.
	void midi_rxd_w(int state) { rx_w(state); }
	void midi_byte(uint8_t b);
	// Unpacks the eight bit-plane bytes into the sixteen samples of the FIRST
	// HALF CYCLE, scaled to +-1.  No DC removal: the full period is the odd
	// extension of these, so its mean is zero by construction.
	static void unpack(const uint8_t planes[8], double half[16]);

	// The store that reaches the output right now: S1 either names one, or is
	// on AUTO and the computer's last LETMB names it.
	unsigned selected_store() const;

	// Copies the sixteen panel sliders into store 0.  The panel is a physical
	// control, so this is the ONLY way store 0 is ever written.
	void redesenha_pgrf();

	// Turns the held key plus the transposition switch into a pitch byte.
	void recalcula_nota_do_teclado();

	// True when the manual-automatico switch of chapter 3 is on MANUAL, in
	// which case the keys -- and not commands 0 and 11 -- supply note and gate.
	bool teclado_manda() const;
	uint8_t nota_corrente() const;
	bool chaveamento_corrente() const;

	// Sums the tape into the stream, and writes the sum back when recording.
	void mix_tape(sound_stream &stream);
	// Finds the next rising edge of the recovered 1 kHz and schedules it.
	void schedule_next_sync();
	TIMER_CALLBACK_MEMBER(sync_edge);
	TIMER_CALLBACK_MEMBER(sync_off);
	double tape_position() const;
	// Playing OR recording: an overdub is both, and is_playing() is false then.
	bool tape_moving() const;

	sound_stream *m_stream = nullptr;
	required_ioport m_s1;
	// One adjuster per sample of the graphic panel; see the header comment for
	// why sixteen sliders stand in for the real 64 switches.
	required_ioport_array<AMOSTRAS> m_pgrf_amostra;
	required_ioport m_tecl_modo;    // manual / automatico
	required_ioport m_tecl_transp;  // seven-position octave transposition
	// TECL0..TECL3, the 49 key contacts themselves.  Held so that the MIDI
	// path can push the very same fields the PC keyboard presses, which is
	// what makes the key light up on the layout.
	required_ioport_array<4> m_tecl_porta;
	required_device<cassette_image_device> m_tape;

	emu_timer *m_sync_timer = nullptr;
	emu_timer *m_sync_off_timer = nullptr;
	double m_sync_scan = 0.0;    // where the edge search has got to, in seconds
	double m_sync_written = 0.0; // channel 1 is written up to here
	int m_sync_level = 0;        // the level the time base generator last gave

	uint8_t m_pitch = 0;      // the last pitch byte, from command 0 or 11
	bool m_gate = false;      // command 0 turns it on, command 11 off
	uint8_t m_intensity = 0;  // D/A 1, command 12

	// Bit planes as commands 1 to 8 deliver them: the computer's own store,
	// which GRTMB copies into one of the memories.  Chapter 11 calls it the
	// eighth memory and fills it serially through en1/en0; these eight bytes
	// are those 64 bits.
	uint8_t m_timbre[8];
	double m_computer_half[16];

	// PGRF and M1 to M7, each the first half cycle of an odd waveform.
	double m_store[STORES][16];

	// Set by LETMB, used when S1 is on AUTO.
	uint8_t m_auto_select = PGRF;

	/* The keyboard's side of m_pitch/m_gate: the manual-automatico switch is
	   a selector, so neither source erases the other. */
	uint64_t m_teclas_apertadas = 0;  // bitmap of the 49 keys, bit k = key k
	int32_t m_tecla_atual = -1;       // the key that owns the voice, -1 = none
	bool m_tecl_gate = false;         // CHV, from the keys
	uint8_t m_tecl_pitch = 0;         // the pitch byte those keys produce

	/* THE HOST MIDI RECEIVER'S OWN STATE -- three bytes of parser, and none of
	   it is state of the emulated instrument.  Running status is honoured
	   because controllers use it; real-time bytes are ignored; system-common
	   clears it; anything that is not note-on or note-off is dropped. */
	uint8_t m_midi_status = 0;        // running status byte
	uint8_t m_midi_nota = 0;          // first data byte, the note number
	bool m_midi_tem_nota = false;     // ...and whether it has arrived

	// Phase in [0,1) over the WHOLE period, which is 32 steps: 16 samples
	// forward, then the same 16 negated.  Fractional, so the period is not
	// quantised to whole samples.
	double m_phase = 0.0;
};

DECLARE_DEVICE_TYPE(EPUSP_SYNTH, epusp_synth_device)

#endif // MAME_BUS_EPUSP_SYNTH_H
