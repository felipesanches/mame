// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Time base generator of the EPUSP sound synthesiser -- channel /4

    See tbgen.h for where every function name comes from.

***************************************************************************/

#include "emu.h"
#include "tbgen.h"

#define LOG_TICK   (1U << 1)
#define LOG_FRAME  (1U << 2)

#define VERBOSE (0)
#include "logmacro.h"

DEFINE_DEVICE_TYPE(PATINHO_TBGEN, patinho_tbgen_device, "patinho_tbgen", "EPUSP synthesiser time base (TBGEN)")

patinho_tbgen_device::patinho_tbgen_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, PATINHO_TBGEN, tag, owner, clock)
	, device_patinho_io_card_interface(mconfig, *this)
{
}

void patinho_tbgen_device::device_start()
{
	m_tick_timer = timer_alloc(FUNC(patinho_tbgen_device::tick), this);

	save_item(NAME(m_tempi));
	save_item(NAME(m_sync_pulse));
	save_item(NAME(m_frame_count));
	save_item(NAME(m_frame_ticks));
	save_item(NAME(m_ext_sync));
	save_item(NAME(m_external));
	/* THE TICK COUNTER IS A MEASURING INSTRUMENT, so it is saved too.  It
	   drives one LOGMASKED line today, but it is the counter that revealed the
	   overdub bug (see the comment in bus/epusp/synth.cpp about it): an
	   instrument that goes back to zero when a state is loaded lies about the
	   next measurement it is used for. */
	save_item(NAME(m_ext_ticks));

	/* NOT SAVED:

	     m_tick_timer  pointer; the timer's own state (period, expiry, whether
	                   it is armed at all) is saved by the scheduler, so the
	                   oscillator comes back running -- or, if the state was
	                   taken in external mode where ext_sync_w() had reset it,
	                   comes back stopped, which is equally correct.  The tape
	                   re-arms nothing: in external mode the tape IS the clock.

	   Also note m_frame_ticks above: it is configuration rather than running
	   state (set_frame_ticks() is never called), and saving it is harmless.  It
	   is registered because a future sweep could make it settable, and a saved
	   state taken during such a sweep must carry the value it was taken with. */
}

void patinho_tbgen_device::device_reset()
{
	m_tick_timer->reset();
	m_tempi = 0;
	m_sync_pulse = false;
	m_frame_count = 0;
	m_external = false;
	m_ext_sync = false;
}

void patinho_tbgen_device::card_reset()
{
	device_patinho_io_card_interface::card_reset();

	m_tick_timer->reset();
	m_tempi = 0;
	m_sync_pulse = false;
	m_frame_count = 0;
	m_external = false;
	m_ext_sync = false;
}

/* The period is 10^(TEMPI-3) seconds, so TEMPI = 0 is the 1 kHz of chapter 15
   of the synthesiser manual.  TEMPI is not read off the tape directly: the
   score compiler adds 3 to the second argument of "TEMPO,n,e" before punching
   it (FITA#007.txt line 1696), so the "TEMPO,12,-3" of FITA#020 becomes
   TEMPI = 0 in the object FITA#023.  All four surviving musical tapes carry
   TEMPI = 0 -- measured by scripts/sintetizador/inventario_comandos.py in the
   PatinhoFeio repository.

   TEMPI is a whole byte, and nothing stops a program writing something absurd
   into it.  Clamped rather than trusted, because 10^252 seconds is not a
   timer, it is a hang. */
attotime patinho_tbgen_device::tick_period() const
{
	if (m_tempi > 6)
	{
		logerror("TEMPI = %u would ask for a period of 10^%d s; clamped to 10^3\n",
				m_tempi, int(m_tempi) - 3);
		return attotime::from_seconds(1000);
	}

	// 10^(TEMPI-3) s, built without floating point: attotime::from_ticks with
	// a power-of-ten rate is exact.
	static const int RATE[7] = { 1000, 100, 10, 1, 0, 0, 0 };
	if (m_tempi <= 3)
		return attotime::from_hz(RATE[m_tempi]);

	unsigned secs = 1;
	for (unsigned i = 3; i < m_tempi; i++)
		secs *= 10;
	return attotime::from_seconds(secs);
}

void patinho_tbgen_device::restart()
{
	attotime const p = tick_period();
	m_frame_count = 0;
	m_tick_timer->adjust(p, 0, p);
}

void patinho_tbgen_device::control_w(int state)
{
	// "FNC /47", named "LIMPA CONTROLE (TBGEN)" in the executor, stops it.
	if (state)
		restart();
	else
		m_tick_timer->reset();
}

void patinho_tbgen_device::func_w(uint8_t cmd)
{
	switch (cmd)
	{
	case 1:
		// "AVISA QUE A INTERFACE MANDA": the computer's own oscillator drives
		// the tick.  The bus has already cleared ESTADO for us, which on this
		// board is what "internal" means.
		m_external = false;
		break;

	case 2:
		/* "AVISA QUE O SINTETIZADOR MANDA": the tick comes from the 1 kHz
		   track recovered from the tape recorder, played back through the
		   synthesiser.  The bus has already set ESTADO, which here means
		   "external".  See ext_sync_w() for what does and does not travel on
		   the tape, and why the frame pulse stays local.

		   The clock is started here because voice 2 of the executor runs
		   "FNC /42" and NEVER "SAI /40" -- FITA#011 tests the voice with
		   "PLAZ LIZ", and only the voice 1 branch at LIZ reaches "SAI /40".
		   Without this the board would never run at all.  With a tape
		   mounted, the first recovered tick stands the oscillator down and
		   the tape takes over. */
		m_external = true;
		set_control(true);
		break;

	case 3:
		// "LIMPA F.F. PULSO SINC."
		m_sync_pulse = false;
		break;

	case 0:  // "NAO PERM. INT. (TBGEN)"  -- the bus clears PERMITE
	case 4:  // "LIMPA F.F. PED. INT."    -- the bus clears PEDIDO
	case 5:  // "PERM. INT. (TBGEN)"      -- the bus sets PERMITE
	case 7:  // "LIMPA CONTROLE (TBGEN)"  -- the bus clears CONTROLE
		break;

	default:
		device_patinho_io_card_interface::func_w(cmd);
		break;
	}
}

void patinho_tbgen_device::data_w(uint8_t cmd, uint8_t data)
{
	if (cmd != 0)
	{
		logerror("unknown SAI /4%X\n", cmd);
		return;
	}

	/* "SAI DADO, SETA CONTROLE E COMECA A CONTAR TEMPO REAL".

	   The bus has already put the accumulator in the 8 bit register, cleared
	   ESTADO and set CONTROLE.  Clearing ESTADO happens to be right here --
	   voice 1 asks for the internal clock with "FNC /41" two instructions
	   earlier -- but it is right by coincidence, not by design, since on this
	   board ESTADO is the clock source and not busy/ready.  Recorded so that
	   nobody later reads the coincidence as intent. */
	m_tempi = data;
	restart();
}

bool patinho_tbgen_device::skip_cond(uint8_t cmd) const
{
	// "SAL /43", named "SALTA SE NAO FOR PULSO DE SINCRONISMO": it skips when
	// there is NO frame pulse, so the sense is inverted.
	if (cmd == 3)
		return !m_sync_pulse;

	return device_patinho_io_card_interface::skip_cond(cmd);
}

/* HOW LONG IS A FRAME?  A HYPOTHESIS.

   The executor keeps a counter CSQ that runs 99 down to 0 and reloads from
   CEM, and CEM is 99 in the preserved image (at /3BF).  A frame of 100 ticks
   is the natural reading of that, and it is what set_frame_ticks() defaults
   to.  But it is a reading, not a statement: no document in the project says
   how many ticks the frame sync pulse takes.

   And there is a third possibility, not just two.  Chapter 15 of the
   synthesiser manual says the frame sync "e enviado pelo fio amarelo no pino 3
   da INTF. REC." but that the circuit "nao grava o sincronismo de quadro
   devido a dificuldades com a resposta em frequencia do gravador".  Only the
   1 kHz reaches the magnetic tape.  So on a voice 2 tape, whose clock is
   recovered from a recording, there may be NO frame pulse at all.

   Modelled here as: the pulse is generated in both modes.  If the sweep later
   shows voice 2 should have none, this is the place to make it conditional on
   the clock source. */
TIMER_CALLBACK_MEMBER(patinho_tbgen_device::tick)
{
	/* The internal oscillator is what gets recorded onto the tape's second
	   channel while voice 1 plays.  Pulsed on the backplane rather than wired
	   to the recorder, because this board does not know what is listening. */
	bus().tick_w(1);
	bus().tick_w(0);
	advance();
}

/* THE RECOVERED 1 kHz, arriving from the tape through the synthesiser.

   Chapter 15 of the synthesiser manual settles what is on the tape: the frame
   sync "e enviado pelo fio amarelo no pino 3 da INTF. REC.", but the circuit
   "nao grava o sincronismo de quadro devido a dificuldades com a resposta em
   frequencia do gravador".  ONLY THE 1 kHz REACHES THE MAGNETIC TAPE.  So this
   line carries ticks and nothing else, and the frame pulse keeps being counted
   by this board -- which is exactly what makes the executor's drift correction
   meaningful.

   THE INTERNAL OSCILLATOR STANDS DOWN once a tape starts supplying ticks, and
   comes back if the tape stops.  That is a declared convenience, not a reading:
   a real board would have the source switch and nothing else.  It exists so
   that a voice 2 tape still plays with no cassette mounted, which is how
   FITA#015 was rendered and verified before the tape recorder was modelled. */
void patinho_tbgen_device::ext_sync_w(int state)
{
	bool const rising = (state != 0) && !m_ext_sync;
	m_ext_sync = (state != 0);

	if (!rising || !m_external)
		return;

	if (!m_ext_ticks)
		LOGMASKED(LOG_FRAME, "base de tempo assumida pela fita\n");
	m_tick_timer->reset();   // the tape is the clock now
	m_ext_ticks++;
	advance();
}

void patinho_tbgen_device::advance()
{
	/* Page 12.15 generalised: whatever sets PEDIDO does so only if the
	   PERMITE/IMPEDE flip-flop of the board allows it.  The executor turns it
	   on with "FNC /45" at startup and off with "FNC /40" in VPARAR.

	   The flip-flop does NOT clear itself.  Only "FNC /44" clears it, so a
	   tick arriving while the previous request is still pending is simply
	   lost -- which is the historical behaviour the burst budget in
	   scripts/sintetizador/orcamento_tx.py is about. */
	if (irq_enable())
		set_irq_request(true);

	if (++m_frame_count >= m_frame_ticks)
	{
		m_frame_count = 0;
		m_sync_pulse = true;   // edge; only FNC /43 clears it
		LOGMASKED(LOG_FRAME, "frame sync pulse\n");
	}
}
