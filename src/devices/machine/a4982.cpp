// license:BSD-3-Clause
// copyright-holders:Felipe Sanches

#include "emu.h"
#include "a4982.h"

#define LOG_STEP    (1U << 1)
#define LOG_MODE    (1U << 2)
#define LOG_CONTROL (1U << 3)
#define LOG_CURRENT (1U << 4)
#define LOG_TIMING  (1U << 5)

#define VERBOSE (0)
//#define LOG_OUTPUT_FUNC osd_printf_info
#include "logmacro.h"

#define LOGSTEP(...)    LOGMASKED(LOG_STEP, __VA_ARGS__)
#define LOGMODE(...)    LOGMASKED(LOG_MODE, __VA_ARGS__)
#define LOGCONTROL(...) LOGMASKED(LOG_CONTROL, __VA_ARGS__)
#define LOGCURRENT(...) LOGMASKED(LOG_CURRENT, __VA_ARGS__)
#define LOGTIMING(...)  LOGMASKED(LOG_TIMING, __VA_ARGS__)


DEFINE_DEVICE_TYPE(A4982, a4982_device, "a4982", "Allegro A4982 Microstepping Motor Driver")


const u8 a4982_device::s_dac_level[MICROSTEPS_PER_FULL_STEP + 1] =
{
	0, 6, 12, 19, 24, 30, 36, 41, 45, 49, 53, 56, 59, 61, 63, 64, 64
};


a4982_device::a4982_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock) :
	device_t(mconfig, A4982, tag, owner, clock),
	m_step_cb(*this),
	m_rsense(0.1),
	m_finest_microstep(FINEST_A4982),
	m_step(false),
	m_dir(false),
	m_enable_n(true),
	m_ms1(false),
	m_ms2(false),
	m_reset_n(true),
	m_sleep_n(true),
	m_vref(0.0),
	m_position(0),
	m_phase(HOME_PHASE),
	m_microsteps(1),
	m_last_step_edge(attotime::zero)
{
}


void a4982_device::device_validity_check(validity_checker &valid) const
{
	if (m_rsense <= 0.0)
		osd_printf_error("Sense resistor value must be greater than zero\n");

	if ((m_finest_microstep != FINEST_A4982) && (m_finest_microstep != FINEST_A4984))
		osd_printf_error("Finest microstep resolution must be %d (A4982) or %d (A4984)\n", int(FINEST_A4982), int(FINEST_A4984));
}


void a4982_device::device_start()
{
	save_item(NAME(m_step));
	save_item(NAME(m_dir));
	save_item(NAME(m_enable_n));
	save_item(NAME(m_ms1));
	save_item(NAME(m_ms2));
	save_item(NAME(m_reset_n));
	save_item(NAME(m_sleep_n));
	save_item(NAME(m_vref));
	save_item(NAME(m_position));
	save_item(NAME(m_phase));
	save_item(NAME(m_microsteps));
	save_item(NAME(m_last_step_edge));
}


void a4982_device::device_reset()
{
	m_position = 0;
	m_phase = HOME_PHASE;
	m_microsteps = decode_microsteps();
	m_last_step_edge = attotime::zero;
}


u8 a4982_device::decode_microsteps() const
{
	if (!m_ms1 && !m_ms2)
		return 1;
	else if (m_ms1 && !m_ms2)
		return 2;
	else if (!m_ms1 && m_ms2)
		return 4;
	else
		return m_finest_microstep;
}


double a4982_device::dac_fraction(u8 phase)
{
	u8 const quadrant = u8(phase / MICROSTEPS_PER_FULL_STEP);
	u8 const index = u8(phase % MICROSTEPS_PER_FULL_STEP);

	u8 const level = BIT(quadrant, 0) ? s_dac_level[MICROSTEPS_PER_FULL_STEP - index] : s_dac_level[index];
	double const fraction = double(level) / 64.0;

	return BIT(quadrant, 1) ? -fraction : fraction;
}


s64 a4982_device::position_full_steps() const
{
	s64 const bias = (m_position < 0) ? (MICROSTEPS_PER_FULL_STEP - 1) : 0;
	return (m_position - bias) / MICROSTEPS_PER_FULL_STEP;
}


double a4982_device::current_limit() const
{
	return (m_rsense > 0.0) ? (m_vref / (8.0 * m_rsense)) : 0.0;
}


double a4982_device::coil_current(int coil) const
{
	if (!outputs_enabled())
		return 0.0;

	u8 const phase = (coil == 0) ? u8((m_phase + MICROSTEPS_PER_FULL_STEP) % PHASE_STATES) : m_phase;

	return dac_fraction(phase) * current_limit();
}


void a4982_device::step_w(int state)
{
	bool const level = bool(state);
	if (m_step == level)
		return;
	m_step = level;

	attotime const now = machine().time();
	attotime const elapsed = now - m_last_step_edge;
	if (!m_last_step_edge.is_zero() && (elapsed < attotime::from_usec(1)))
		LOGTIMING("STEP held %s for %s, below the 1 us datasheet minimum\n", level ? "low" : "high", elapsed.as_string());
	m_last_step_edge = now;

	if (!level)
		return;

	if (!m_reset_n || !m_sleep_n)
	{
		LOGSTEP("STEP ignored, %s asserted\n", m_reset_n ? "/SLEEP" : "/RESET");
		return;
	}

	m_microsteps = decode_microsteps();
	int const increment = MICROSTEPS_PER_FULL_STEP / m_microsteps;
	int const delta = m_dir ? increment : -increment;

	m_position += delta;
	m_phase = u8((m_phase + PHASE_STATES + delta) % PHASE_STATES);

	LOGSTEP("1/%d step %s, position %d, phase %d\n", int(m_microsteps), m_dir ? "forward" : "reverse", m_position, int(m_phase));

	m_step_cb(u64(m_position));
}


void a4982_device::dir_w(int state)
{
	bool const level = bool(state);
	if (m_dir == level)
		return;
	m_dir = level;

	LOGMODE("DIR %s\n", level ? "forward" : "reverse");
}


void a4982_device::ms1_w(int state)
{
	bool const level = bool(state);
	if (m_ms1 == level)
		return;
	m_ms1 = level;

	LOGMODE("MS1 %d, pending resolution 1/%d\n", level ? 1 : 0, int(decode_microsteps()));
}


void a4982_device::ms2_w(int state)
{
	bool const level = bool(state);
	if (m_ms2 == level)
		return;
	m_ms2 = level;

	LOGMODE("MS2 %d, pending resolution 1/%d\n", level ? 1 : 0, int(decode_microsteps()));
}


void a4982_device::enable_w(int state)
{
	bool const level = bool(state);
	if (m_enable_n == level)
		return;
	m_enable_n = level;

	LOGCONTROL("outputs %s\n", level ? "disabled" : "enabled");
}


void a4982_device::reset_w(int state)
{
	bool const level = bool(state);
	if (m_reset_n == level)
		return;
	m_reset_n = level;

	if (!level)
	{
		m_phase = HOME_PHASE;
		LOGCONTROL("/RESET asserted, translator forced to the home microstep position\n");
	}
	else
	{
		LOGCONTROL("/RESET released\n");
	}
}


void a4982_device::sleep_w(int state)
{
	bool const level = bool(state);
	if (m_sleep_n == level)
		return;
	m_sleep_n = level;

	if (level)
	{
		m_phase = HOME_PHASE;
		LOGCONTROL("/SLEEP released, translator homed\n");
	}
	else
	{
		LOGCONTROL("/SLEEP asserted\n");
	}
}


void a4982_device::set_vref(double vref)
{
	m_vref = vref;

	if ((vref < 0.0) || (vref > 4.0))
		LOGCURRENT("VREF %f V is outside the 0 to 4 V input range\n", vref);

	LOGCURRENT("VREF %f V, ITripMax %f A\n", m_vref, current_limit());
}
