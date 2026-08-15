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

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void sound_stream_update(sound_stream &stream) override;

private:
	sound_stream *m_stream = nullptr;

	uint8_t m_pitch = 0;      // the last pitch byte, from command 0 or 11
	bool m_gate = false;      // command 0 turns it on, command 11 off
	uint8_t m_intensity = 0;  // D/A 1, command 12

	// Bit planes of the timbre, commands 1 to 8.  Not sounded yet: kept so
	// that the wavetable stage does not have to revisit the interface.
	uint8_t m_timbre[8];

	// Square wave state: a fractional phase in [0,1), so that the period is
	// not quantised to whole samples. See sound_stream_update().
	double m_phase = 0.0;
};

DECLARE_DEVICE_TYPE(EPUSP_SYNTH, epusp_synth_device)

#endif // MAME_USP_EPUSP_SYNTH_H
