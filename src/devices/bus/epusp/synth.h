// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Sound synthesiser of the EPUSP, by Guido Stolfi

    This is the FIRST STAGE only: the control interface, made audible with a
    square wave.  None of the analogue blocks are modelled -- no VCF, no
    envelopes, no portamento, no vibrato.  What it does model is what the
    computer can actually address: the command set of chapter 5 of the
    synthesiser manual, "Comandos do Computador".

    The commands, transcribed from that chapter:

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

    Of those, this stage sounds 0, 11 and 12.  The rest are decoded, stored
    and logged, so that adding a block later is a matter of using state that
    is already being kept correctly.

    THE PITCH CODE IS NOT MIDI.  It is

        nibble alto = oitava,  nibble baixo = semitom

    and the frequency follows from chapter 3, which puts the scale between
    "o do de 16,35 Hz" and "o si de 15 804,3 Hz" -- ten octaves of twelve
    semitones:

        f(codigo) = 16.3516 Hz * 2^( (codigo >> 4) + (codigo & 15) / 12 )

    That gives /39 -> 220.0 Hz and /40 -> 261.6 Hz, which is what the score
    FITA#020 calls LA,4 and DO,5.  The MIDI reading would give 69 instead of
    /49 for LA,5 and is refuted by test C1.2 of
    scripts/sintetizador/verificar.py in the PatinhoFeio repository.

    THE SIXTEEN SAMPLES ARE HALF A CYCLE, NOT A WHOLE ONE.

    Chapter 3, "Gerador de Timbres (TMBR-PGRF-MEMR)", is explicit twice over:

        "Conjunto de 16 chaves deslizantes de 16 posicoes cada uma no qual se
         desenha MEIO CICLO de uma forma de onda IMPAR."

        "Saida de audio selecionavel (SAU).  Sinal analogico (-5 a +5 Volts)
         -- FUNCAO IMPAR -- cujo PRIMEIRO SEMI-CICLO e o registrado no painel
         de chaves (posicao PAINEL) ou os armazenados nas memorias."

    and the clock rate proves it independently:

        "Frequencia de relogio (frl).  Sinal digital (onda quadrada) de
         frequencia 32 VEZES a da forma de onda de saida (TMB)."

    32 = 16 samples x 2 half-cycles.  So the waveform is w(k) for the first
    half of the period and -w(k) for the second, which makes it odd by
    construction and therefore EXACTLY ZERO MEAN.  The earlier version treated
    the 16 samples as a full cycle and subtracted their mean to kill a DC
    offset; the offset was an artefact of that mistake, and the subtraction is
    gone.

    WHO CHOOSES WHICH TIMBRE PLAYS

    Chapter 3 again, and it corrects a first reading taken from chapter 11:

        "Chave de selecao de modo de operacao.  Chave de 2 posicoes (AUTO --
         MANUAL) + chave de 8 posicoes (PGRF-M1-M2...M7) + botao de comando.

         AUTO -- funcionamento automatico (computador).  Nesta posicao o
         computador pode selecionar para a saida SAU a forma de onda do painel
         ou as das memorias, e pode armazenar formas de ondas nas memorias que
         estiverem disponiveis (AUTO/TRANSF.)."

    So the nine positions chapter 11 counts are AUTO plus PGRF plus M1 to M7 --
    not "external plus memories 1 to 8".  There are SEVEN memories and a
    GRAPHIC PANEL, not eight memories.

    And the two computer commands are named there too:

        "Entradas do computador -- 8 linhas de dados mais 8 linhas de comandos
         de timbre, alem de um comando 'LEIA' e um comando 'TRANSFIRA'
         destinados a controle automatico do timbre."

    "leia" is LETMB (command 10) and "transfira" is GRTMB (command 9).  That
    ends a question this project had open for four sessions: commands 1 to 8
    fill the computer's own store, GRTMB copies that store into memory n, and
    LETMB selects which of PGRF/M1..M7 reaches the output.

    WHAT STILL CANNOT BE REPRODUCED, AND WHY THAT IS THE HONEST ANSWER

    Position 0 is PGRF, the graphic panel: sixteen sliding switches the
    composer set BY HAND.  FITA#023 selects it with "LETMB,0" for most of the
    piece.  That drawing is not on the tape and did not survive, so the one
    thing needed to reproduce the piece exactly is the one thing no tape can
    carry.  PGRF comes up holding a square here -- a declared modelling choice,
    for the same reason as before: silence with no error is the worst failure
    mode this project has.

    Each memory also has an AUTO/TRANSF. -- BLOQUEIA switch, and in BLOQUEIA
    "a ultima forma de onda armazenada permanece inalterada, independentemente
    do modo de operacao e dos comandos do computador".  Whether a given memory
    was write-protected in 1977 is likewise unrecorded; they default to
    writable here, which is what a tape issuing GRTMB evidently assumes.

    THE FRONT PANEL, added 2026-08-16: the drawing that no tape could carry

    Until now the only thing a user could turn on this instrument was S1.  The
    PGRF waveform -- the very thing FITA#023 plays for most of its length, and
    the very thing that did not survive -- was a hardcoded square.  Three
    controls of the real machine are now here, and each one is a decision worth
    stating rather than a schematic being copied.

    1. THE GRAPHIC PANEL: SIXTEEN SLIDERS OF SIXTEEN POSITIONS.

       Chapter 11 counts the real control exactly: "16 chaves de entrada" per
       BIT UNIT, and there are four bit units -- that is 4 x 16 = 64 toggle
       switches, one per bit of one sample, arranged as four bit planes.  What
       is modelled here is SIXTEEN SLIDERS, one per sample, each with sixteen
       positions carrying the four bits of that sample.

       That is not the real control and is not claimed to be: it is a usability
       choice made by the project owner, in full knowledge of the 64 switches.
       The information content is identical -- 16 samples x 4 bits either way --
       and the reason for the swap is in the name of the thing.  PGRF is the
       PAINEL GRAFICO, and chapter 3 says what happens at it: "Conjunto de 16
       chaves deslizantes de 16 posicoes cada uma no qual se desenha MEIO CICLO
       de uma forma de onda IMPAR".  A row of sixteen cursor positions DRAWS the
       waveform; a matrix of 64 on/off switches spells it out in binary.  The
       chapter-3 wording is itself evidence that at least one revision of the
       instrument had sliding switches, one per sample.

       These sixteen are HALF A CYCLE, exactly as everything else in this file:
       they feed store 0 and nothing else, and sound_stream_update() extends
       them oddly as before.  Nothing about that logic changed.

       The MAME idiom is one IPT_ADJUSTER per sample (see fixfreq.cpp and
       paia/fatman.cpp).  Two consequences that matter here:
         - the values are saved in cfg/patinho.cfg, so a waveform drawn by hand
           SURVIVES BETWEEN SESSIONS.  That is precisely the thing the paper
           tape could not carry;
         - every adjuster appears in Tab -> Slider Controls with no layout and
           no plugin, so the panel is usable even with -noplugins.

    2. THE SEVEN "TRANSFERE" SWITCHES.

       Chapter 11: "Um timbre programado manualmente nesta entrada pode ser
       transferido (manualmente) para uma de 7 memorias (numeradas de 1 a 7)
       pelos comandos TRANSFERE", and its parts list has "7 chaves de
       transferencia" per bit unit.  So they copy THE PANEL into memory n --
       they are the manual twin of GRTMB, which copies the COMPUTER's store
       (chapter 11's "8.a memoria", filled serially through en1/en0) into the
       same memories.  Two sources, seven destinations, and the panel is never
       a destination: it is a physical control and cannot be written.

    3. THE 49-KEY MANUAL, from chapter 6 (schematics) and chapter 3 (function).

       It is REALLY IMPLEMENTED and not a picture, because the signal path it
       drives is the one this device already models.  Chapter 3 lists the
       keyboard's outputs as frf (a square wave at 32x the note) and CHV (0 V
       with no key down, 5 V with one), and chapter 9 labels the note interface
       decoder output "(Ao teclado -- oitava mais alta)": the computer injects
       into the SAME octave dividers the keys drive.  The manual-automatico
       switch chooses WHO supplies note and gate; downstream is identical.  In
       this device that means the keyboard is a second source for m_pitch and
       m_gate, and nothing else.

       THE CODE A KEY PRODUCES.  With 49 keys (k = 0..48) and the seven-position
       octave transposition (p = 0..6, twelve semitones apart),

           n = 12 * p + k,   pitch byte = ((n / 12) << 4) | (n % 12)

       which spans n = 0..120 -- 121 values, and 121 is exactly what chapter 3
       counts for the manual position ("entre 121 (manualmente) ou 120
       (automaticamente) valores").  The top of that range is
       16.3516 x 2^10 = 16 743.9 Hz, which is chapter 3's "16 744,0 Hz"; the
       automatic position gets 120 because the computer's byte holds octave 0-9
       and semitone 0-11, the same 120 that inventario_comandos.py measured
       across the surviving tapes.  This arithmetic is a DERIVATION from two
       numbers in chapter 3, not a sentence read off the page; it is checked by
       scripts/sintetizador/teclado_121_valores.py in the PatinhoFeio
       repository.

       WHAT THE KEYBOARD HONESTLY DOES NOT DO, all three declared here:
         - L and D, the note-start and note-end pulses, exist to fire the
           envelopes of chapter 7, and no envelope is modelled.  They are not
           generated, because generating a signal with nothing on the other end
           would be decoration;
         - VBR, the vibrato control voltage, is not modelled either;
         - the monophonic priority rule is NOT DOCUMENTED ANYWHERE.  Last key
           wins, and on release the highest key still held takes over.  Declared
           choice, not a reading.

    WHAT IS DELIBERATELY ABSENT: the general-purpose potentiometers of chapter
    17 (they have no fixed function, and no analogue block is modelled that they
    could feed) and any display of the resulting waveform.

***************************************************************************/
#ifndef MAME_BUS_EPUSP_SYNTH_H
#define MAME_BUS_EPUSP_SYNTH_H

#pragma once

#include "epusp.h"

#include "imagedev/cassette.h"


// ======================> epusp_synth_device

class epusp_synth_device : public device_t, public device_sound_interface, public device_epusp_synth_port_interface
{
public:
	epusp_synth_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// One transfer from the coupled duplex boards: high byte = command
	// register (channel /6), low byte = data register (channel /7).
	virtual void command_w(uint16_t pair) override;

	// Frequency of a pitch code, in Hz.  Public because the regression test
	// wants to check it without going through the sound stream.
	static double frequency(uint8_t code);

	/* THE TAPE RECORDER, and why it hangs off the synthesiser.

	   Overdubbing is how the pieces were made: the first voice is recorded to
	   an audio tape, and the following voices are played and recorded ON TOP
	   while the tape runs.  Staying in time is the hard part, and the machine
	   solves it with a two-channel tape:

	       channel 0   the audio
	       channel 1   a 1 kHz tone

	   On voice 1 the computer's own oscillator runs the music ("FNC /41",
	   AVISA QUE A INTERFACE MANDA) and the 1 kHz is laid onto channel 1.  On
	   voice 2 the tape is played back, its 1 kHz recovered and fed to the time
	   base generator ("FNC /42", AVISA QUE O SINTETIZADOR MANDA), and the new
	   voice is mixed into channel 0 -- so the second voice is slaved to the
	   first, however the tape drifted.

	   The recorder connects to the SYNTHESISER, not to a computer channel,
	   which is why it lives here -- and, since 2026-08-15, why it is a
	   SUBDEVICE of this one, built in device_add_mconfig().  It used to be a
	   device of the Patinho Feio driver that the synthesiser was handed a tag
	   for, which made a host machine responsible for a piece of the
	   instrument's own plumbing.  Nothing outside needs to know it is here. */

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

private:
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

	/* THE KEYBOARD'S SIDE OF m_pitch/m_gate.

	   Kept separate from the computer's, and not merged into it, so that
	   flipping manual-automatico back and forth does not lose either source --
	   which is what the real switch does, since the two feed the same dividers
	   through a selector and neither erases the other. */
	uint64_t m_teclas_apertadas = 0;  // bitmap of the 49 keys, bit k = key k
	int32_t m_tecla_atual = -1;       // the key that owns the voice, -1 = none
	bool m_tecl_gate = false;         // CHV, from the keys
	uint8_t m_tecl_pitch = 0;         // the pitch byte those keys produce

	// Phase in [0,1) over the WHOLE period, which is 32 steps: 16 samples
	// forward, then the same 16 negated.  Fractional, so the period is not
	// quantised to whole samples.
	double m_phase = 0.0;
};

DECLARE_DEVICE_TYPE(EPUSP_SYNTH, epusp_synth_device)

#endif // MAME_BUS_EPUSP_SYNTH_H
