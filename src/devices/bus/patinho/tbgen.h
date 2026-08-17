// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Time base generator of the EPUSP sound synthesiser -- channel /4

    Built by the LSD for Guido Stolfi's synthesiser after the July 1977
    assembler manual, whose equipment table (chapter 12) lists /0 /5 /6 /7 /8
    /9 /A /B /E and then "Os outros enderecos estao vagos"; hence the vacant
    channel, and a use of the four standard flip-flops that does not follow
    the standard peripheral semantics.

    Function names as given in Guido Stolfi's source comments, FITA#011.txt:

        FNC /40   "NAO PERM. INT.       (TBGEN)"          line 206
        FNC /41   "AVISA QUE A INTERFACE MANDA"           line 370
        FNC /42   "AVISA QUE O SINTETIZADOR MANDA (TBGEN)" line 408
        FNC /43   "LIMPA F.F. PULSO SINC."                line 60
        FNC /44   "LIMPA F.F. PED. INT."                  line 28
        FNC /45   "PERM. INT.          (TBGEN)"           line 410
        FNC /47   "LIMPA CONTROLE      (TBGEN)"           line 204
        SAL /43   "SALTA SE NAO FOR PULSO DE SINCRONISMO" line 36
        SAI /40   "SAI DADO, SETA CONTROLE E COMECA A CONTAR TEMPO REAL"
                                                          line 374

    ESTADO selects the tick source here rather than signalling busy/ready:
    "a interface manda" (the computer's own oscillator) or "o sintetizador
    manda" (the 1 kHz track recovered from a tape recorder).  Hence
    irq_from_status() returns false; the page 12.15 rule that ESTADO turning
    on sets PEDIDO would fire on every change of clock source.

***************************************************************************/
#ifndef MAME_BUS_PATINHO_TBGEN_H
#define MAME_BUS_PATINHO_TBGEN_H

#pragma once

#include "iobus.h"


// ======================> patinho_tbgen_device

class patinho_tbgen_device : public device_t,
		public device_patinho_io_card_interface
{
public:
	patinho_tbgen_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// How many ticks make one frame sync pulse.  A HYPOTHESIS, not a reading:
	// see the comment over frame_pulse() in tbgen.cpp.  Exposed so that the
	// eventual sweep can try other values without touching the card.
	void set_frame_ticks(unsigned n) { m_frame_ticks = n; }

	/* The external time base: with "FNC /42" the tick comes from the 1 kHz
	   tone on the second channel of the audio tape instead of from this
	   board's oscillator, which is what allows overdubbing.  The frame pulse
	   still comes from this board's own counter: the executor reloads CSQ from
	   CEM (99) at each frame pulse and decrements it once per tick, so tape
	   drift leaves CSQ non-zero and FITA#011's "SINC" routine corrects for it.
	   Counting the frame off the recovered ticks would leave CSQ always zero
	   and that correction dead. */
	virtual void ext_sync_w(int state) override;

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// device_patinho_io_card_interface implementation
	virtual void func_w(uint8_t cmd) override;
	virtual void data_w(uint8_t cmd, uint8_t data) override;
	virtual bool skip_cond(uint8_t cmd) const override;
	virtual void control_w(int state) override;
	virtual void card_reset() override;

	// ESTADO selects the clock source on this board, so the standard rule
	// "ESTADO ligado liga o PEDIDO" must not apply.  The tick is what raises
	// the request here.
	virtual bool irq_from_status() const override { return false; }

private:
	TIMER_CALLBACK_MEMBER(tick);
	void advance();
	attotime tick_period() const;
	void restart();

	emu_timer *m_tick_timer = nullptr;

	uint8_t m_tempi = 0;         // decade exponent; period = 10^(TEMPI-3) s
	bool m_sync_pulse = false;   // f.f. PULSO SINC., only FNC /43 clears it
	unsigned m_frame_count = 0;  // ticks since the last frame pulse
	unsigned m_frame_ticks = 100;
	bool m_external = false;     // FNC /42 chose the tape; FNC /41 the crystal
	bool m_ext_sync = false;     // last level seen on the recovered 1 kHz
	uint32_t m_ext_ticks = 0;    // how many ticks have arrived from the tape
};

DECLARE_DEVICE_TYPE(PATINHO_TBGEN, patinho_tbgen_device)

#endif // MAME_BUS_PATINHO_TBGEN_H
