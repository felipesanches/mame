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

***************************************************************************/
#ifndef MAME_USP_EPUSP_SYNTH_H
#define MAME_USP_EPUSP_SYNTH_H

#pragma once

#include "imagedev/cassette.h"


// ======================> epusp_synth_device

class epusp_synth_device : public device_t, public device_sound_interface
{
public:
	epusp_synth_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// One transfer from the coupled duplex boards: high byte = command
	// register (channel /6), low byte = data register (channel /7).
	void command_w(uint16_t pair);

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
	   which is why it lives here.  "O sintetizador manda" is the manual's own
	   name for that path. */
	auto sync_handler() { return m_sync_handler.bind(); }
	template <typename T> void set_tape(T &&tag) { m_tape.set_tag(std::forward<T>(tag)); }

	// The time base generator's tick, offered for recording onto channel 1.
	void tape_tick_w(int state);

	// The eight selectable waveform stores: index 0 is PGRF, the graphic
	// panel, and 1 to 7 are memories M1 to M7.
	static constexpr unsigned STORES = 8;
	static constexpr unsigned PGRF = 0;

	// S1 positions.  Nine of them: AUTO, then PGRF, then M1 to M7.
	static constexpr unsigned S1_AUTO = 0;

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
	optional_device<cassette_image_device> m_tape;
	devcb_write_line m_sync_handler;

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

	// Phase in [0,1) over the WHOLE period, which is 32 steps: 16 samples
	// forward, then the same 16 negated.  Fractional, so the period is not
	// quantised to whole samples.
	double m_phase = 0.0;
};

DECLARE_DEVICE_TYPE(EPUSP_SYNTH, epusp_synth_device)

#endif // MAME_USP_EPUSP_SYNTH_H
