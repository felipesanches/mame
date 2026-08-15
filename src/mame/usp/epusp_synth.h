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

    THE COMPUTER DOES NOT CHOOSE WHICH TIMBRE PLAYS.

    Chapter 11 of the manual, "gerador de timbres", page 5 of the original:

        "Um timbre pode ser selecionado pelos 4 grupos de 16 chaves de selecao
         de timbres.  Um timbre programado manualmente nesta entrada pode ser
         transferido (manualmente) para uma de 7 memorias (numeradas de 1 a 7)
         pelos comandos TRANSFERE.  As entradas en1 e en0 sao entradas serie de
         '1' e '0' para a 8.a memoria, a ser usada pelo computador.  A saida
         m_ij executa o timbre armazenado na i-esima memoria (i = 1, 2, ..., 8)
         [...].  Qualquer destas saidas pode ser selecionada pela chave S1 de 9
         posicoes, 4 polos."

    The chip list agrees, per bit: eight 4005 "memoria de 16 bits", sixteen
    input switches, seven transfer switches, and the two serial lines en1/en0.

    So the instrument has EIGHT timbre memories.  The computer owns exactly one
    of them, the eighth, and writes it serially -- and commands 1 to 8 are that
    serial fill, 16 samples x 4 bits = 64 bits = the 8 bytes.  Which memory
    reaches the D/A is decided by S1, a NINE-POSITION FRONT PANEL SWITCH: the
    external switch bank, or any of memories 1 to 8.

    S1 is modelled here as a machine configuration.  Memories 1 to 7 and the
    switch bank held whatever the operator dialled in at the front panel in
    1974; nothing about their contents survives, so they start empty.  Choosing
    one of those positions therefore gives silence BECAUSE WE DO NOT KNOW what
    was in it, not because the instrument was silent.

***************************************************************************/
#ifndef MAME_USP_EPUSP_SYNTH_H
#define MAME_USP_EPUSP_SYNTH_H

#pragma once


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

	// Which of the eight memories the computer owns.  Chapter 11: "as entradas
	// en1 e en0 sao entradas serie [...] para a 8.a memoria, a ser usada pelo
	// computador."  Zero-based here, so memory 8 is index 7.
	static constexpr unsigned COMPUTER_MEMORY = 7;

	// S1 positions, as the nine-position switch is wired.
	static constexpr unsigned S1_EXTERNAL = 0;   // the 4 x 16 front-panel switches
	// 1 to 8 select the memory of that number

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual ioport_constructor device_input_ports() const override ATTR_COLD;
	virtual void sound_stream_update(sound_stream &stream) override;

private:
	// Unpacks the eight bit-plane bytes into sixteen samples centred on zero.
	static void unpack(const uint8_t planes[8], double wave[16]);

	// The waveform S1 is currently pointing at, or nullptr for the external
	// switch bank, whose contents did not survive.
	const double *selected_wave() const;

	sound_stream *m_stream = nullptr;
	required_ioport m_s1;

	uint8_t m_pitch = 0;      // the last pitch byte, from command 0 or 11
	bool m_gate = false;      // command 0 turns it on, command 11 off
	uint8_t m_intensity = 0;  // D/A 1, command 12

	// Bit planes as commands 1 to 8 deliver them.  On the real instrument
	// these ARE the serial fill of memory 8, so there is no separate staging
	// buffer: they are kept only so a partial load can be unpacked.
	uint8_t m_timbre[8];

	// The eight timbre memories, unpacked and centred on zero.  Only index
	// COMPUTER_MEMORY is ever written from here; 0 to 6 were loaded by hand at
	// the front panel and nothing survives about what was in them.
	double m_mem[8][16];

	// Square wave state: a fractional phase in [0,1), so that the period is
	// not quantised to whole samples. See sound_stream_update().
	double m_phase = 0.0;
};

DECLARE_DEVICE_TYPE(EPUSP_SYNTH, epusp_synth_device)

#endif // MAME_USP_EPUSP_SYNTH_H
