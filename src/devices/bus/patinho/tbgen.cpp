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

/* Period = 10^(TEMPI-3) seconds, so TEMPI = 0 is the 1 kHz of chapter 15 of
   the synthesiser manual.  The score compiler adds 3 to the second argument
   of "TEMPO,n,e" before punching it (FITA#007.txt line 1696), so "TEMPO,12,-3"
   in FITA#020 becomes TEMPI = 0 in the object tape FITA#023; all four
   surviving musical tapes carry TEMPI = 0.  TEMPI is a whole byte, so
   out-of-range values are clamped rather than turned into 10^252 s periods. */
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
		   track recovered from the tape recorder; the bus has already set
		   ESTADO, which on this board means "external".  The clock is started
		   here because voice 2 of the executor runs "FNC /42" and never
		   "SAI /40" (FITA#011), so nothing else would start it. */
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

	/* "SAI DADO, SETA CONTROLE E COMECA A CONTAR TEMPO REAL".  The bus has
	   already loaded the 8 bit register, cleared ESTADO and set CONTROLE.
	   ESTADO is the clock source on this board, not busy/ready, so that
	   clearing does not select the internal clock: voice 1 does that
	   explicitly with "FNC /41" two instructions earlier. */
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

/* INFERENCE, not documented: 100 ticks per frame.  The executor's counter CSQ
   runs 99 down to 0 and reloads from CEM, which is 99 in the preserved image
   (at /3BF).  The pulse is generated in both clock modes by choice; since the
   frame sync is not recorded on tape (see ext_sync_w), a voice 2 tape may
   carry no frame pulse at all. */
TIMER_CALLBACK_MEMBER(patinho_tbgen_device::tick)
{
	/* The internal oscillator is what gets recorded onto the tape's second
	   channel while voice 1 plays.  Pulsed on the backplane rather than wired
	   to the recorder, because this board does not know what is listening. */
	bus().tick_w(1);
	bus().tick_w(0);
	advance();
}

/* The recovered 1 kHz, arriving from the tape through the synthesiser.
   Chapter 15 of the synthesiser manual: the circuit "nao grava o sincronismo
   de quadro devido a dificuldades com a resposta em frequencia do gravador",
   so only the 1 kHz reaches the tape; this line carries ticks and nothing
   else, and the frame pulse stays local to this board.  Declared choice: the
   internal oscillator stands down while the tape supplies ticks and resumes if
   the tape stops, so a voice 2 tape also plays with no cassette mounted. */
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
	/* Page 12.15 generalised: PEDIDO is set only if the board's PERMITE/IMPEDE
	   flip-flop allows it (the executor sets it with "FNC /45" and clears it
	   with "FNC /40" in VPARAR).  PEDIDO does not clear itself -- only
	   "FNC /44" clears it -- so a tick arriving while a request is still
	   pending is lost, as on the real board. */
	if (irq_enable())
		set_irq_request(true);

	if (++m_frame_count >= m_frame_ticks)
	{
		m_frame_count = 0;
		m_sync_pulse = true;   // edge; only FNC /43 clears it
		LOGMASKED(LOG_FRAME, "frame sync pulse\n");
	}
}
