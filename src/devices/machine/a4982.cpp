// license:BSD-3-Clause
// copyright-holders:Felipe Sanches
/**********************************************************************

    Allegro MicroSystems A4982 / A4984 microstepping motor driver

    Datasheet references -- the microstep truth table, the STEP/DIR
    timing, the home microstep position and the current limit formula --
    are in a4982.h.

    Modelled: the translator.  STEP, DIR, MS1 and MS2 sequencing, the
    home state forced by /RESET and by leaving Sleep mode, and the
    ITripMax set by VREF and the sense resistor.  Position is a signed
    64 bit count of sixteenth steps whatever the selected resolution, so
    one full step is always 16 counts.

    Not modelled: the output stage.  Fixed off-time PWM current
    regulation, mixed and slow decay, blanking, charge pump,
    undervoltage lockout, thermal shutdown, short-to-ground and
    shorted-load protection.  None of these are observable through the
    STEP/DIR interface.  The device has no time base of its own; it only
    reacts to writes on its inputs.

**********************************************************************/

#include "emu.h"
#include "a4982.h"

#define LOG_STEP    (1U << 1)
#define LOG_MODE    (1U << 2)
#define LOG_CONTROL (1U << 3)
#define LOG_CURRENT (1U << 4)
#define LOG_TIMING  (1U << 5)

// LOG_STEP is extremely noisy: one line per microstep
#define VERBOSE (0)
//#define LOG_OUTPUT_FUNC osd_printf_info
#include "logmacro.h"

#define LOGSTEP(...)    LOGMASKED(LOG_STEP, __VA_ARGS__)
#define LOGMODE(...)    LOGMASKED(LOG_MODE, __VA_ARGS__)
#define LOGCONTROL(...) LOGMASKED(LOG_CONTROL, __VA_ARGS__)
#define LOGCURRENT(...) LOGMASKED(LOG_CURRENT, __VA_ARGS__)
#define LOGTIMING(...)  LOGMASKED(LOG_TIMING, __VA_ARGS__)


//**************************************************************************
//  GLOBAL VARIABLES
//**************************************************************************

// device type definition
DEFINE_DEVICE_TYPE(A4982, a4982_device, "a4982", "Allegro A4982 Microstepping Motor Driver")


// Phase current DAC levels for one quadrant, in 1/64ths of ITripMax: A4982
// datasheet Table 2, sixteenth step positions 1 to 17, each entry being
// round(64 x sin(n x 5.625 deg)).  The A4984 lists unquantised sine values for
// the same angles, differing by at most 0.6 % of full scale, within the +/-5 %
// trip level error both parts specify, so one table serves both.
const u8 a4982_device::s_dac_level[MICROSTEPS_PER_FULL_STEP + 1] =
{
	0, 6, 12, 19, 24, 30, 36, 41, 45, 49, 53, 56, 59, 61, 63, 64, 64
};


//**************************************************************************
//  A4982 DEVICE
//**************************************************************************

//-------------------------------------------------
//  a4982_device - constructor
//-------------------------------------------------

a4982_device::a4982_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock) :
	device_t(mconfig, A4982, tag, owner, clock),
	m_step_cb(*this),
	m_rsense(0.1),                    // value fitted on most carrier boards
	m_finest_microstep(FINEST_A4982),
	m_step(false),
	m_dir(false),
	m_enable_n(true),                 // outputs off until something drives /ENABLE low
	m_ms1(false),                     // internal pull-downs, so an unconnected MS pin reads low
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


//-------------------------------------------------
//  device_validity_check - validate a device after
//  the configuration has been constructed
//-------------------------------------------------

void a4982_device::device_validity_check(validity_checker &valid) const
{
	if (m_rsense <= 0.0)
		osd_printf_error("Sense resistor value must be greater than zero\n");

	if ((m_finest_microstep != FINEST_A4982) && (m_finest_microstep != FINEST_A4984))
		osd_printf_error("Finest microstep resolution must be %d (A4982) or %d (A4984)\n", int(FINEST_A4982), int(FINEST_A4984));
}


//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void a4982_device::device_start()
{
	// register internal state
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


//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void a4982_device::device_reset()
{
	// power-on puts the translator in the home state
	m_position = 0;
	m_phase = HOME_PHASE;
	m_microsteps = decode_microsteps();
	m_last_step_edge = attotime::zero;

	// input pin levels belong to whatever drives them, so they are not reset here
}


//-------------------------------------------------
//  decode_microsteps - Table 1 of the datasheet
//-------------------------------------------------

u8 a4982_device::decode_microsteps() const
{
	if (!m_ms1 && !m_ms2)
		return 1;                   // L L - full step, 2 phase
	else if (m_ms1 && !m_ms2)
		return 2;                   // H L - half step, 1-2 phase
	else if (!m_ms1 && m_ms2)
		return 4;                   // L H - quarter step, W1-2 phase
	else
		return m_finest_microstep;  // H H - sixteenth step on an A4982, eighth on an A4984
}


//-------------------------------------------------
//  dac_fraction - phase current for one translator
//  position, as a signed fraction of ITripMax
//-------------------------------------------------

double a4982_device::dac_fraction(u8 phase)
{
	u8 const quadrant = u8(phase / MICROSTEPS_PER_FULL_STEP);
	u8 const index = u8(phase % MICROSTEPS_PER_FULL_STEP);

	// the quadrant table is walked backwards in the second and fourth quadrants
	u8 const level = BIT(quadrant, 0) ? s_dac_level[MICROSTEPS_PER_FULL_STEP - index] : s_dac_level[index];
	double const fraction = double(level) / 64.0;

	return BIT(quadrant, 1) ? -fraction : fraction;
}


//-------------------------------------------------
//  position_full_steps - the microstep count in
//  whole steps
//-------------------------------------------------

s64 a4982_device::position_full_steps() const
{
	// floor division, so that the result stays monotonic across zero
	s64 const bias = (m_position < 0) ? (MICROSTEPS_PER_FULL_STEP - 1) : 0;
	return (m_position - bias) / MICROSTEPS_PER_FULL_STEP;
}


//-------------------------------------------------
//  current_limit - ITripMax, in amps
//-------------------------------------------------

double a4982_device::current_limit() const
{
	// the programmed trip level; independent of /ENABLE
	return (m_rsense > 0.0) ? (m_vref / (8.0 * m_rsense)) : 0.0;
}


//-------------------------------------------------
//  coil_current - winding current for one phase,
//  signed, in amps
//-------------------------------------------------

double a4982_device::coil_current(int coil) const
{
	if (!outputs_enabled())
		return 0.0;

	// Table 2: phase 1 leads phase 2 by 90 electrical degrees
	u8 const phase = (coil == 0) ? u8((m_phase + MICROSTEPS_PER_FULL_STEP) % PHASE_STATES) : m_phase;

	return dac_fraction(phase) * current_limit();
}


//-------------------------------------------------
//  step_w - handle the STEP input
//-------------------------------------------------

void a4982_device::step_w(int state)
{
	bool const level = bool(state);
	if (m_step == level)
		return;
	m_step = level;

	// tA and tB are 1 us minimums; report violations, never act on them
	attotime const now = machine().time();
	attotime const elapsed = now - m_last_step_edge;
	if (!m_last_step_edge.is_zero() && (elapsed < attotime::from_usec(1)))
		LOGTIMING("STEP held %s for %s, below the 1 us datasheet minimum\n", level ? "low" : "high", elapsed.as_string());
	m_last_step_edge = now;

	// the translator advances on the low-to-high transition only
	if (!level)
		return;

	// /RESET holds the translator home and ignores STEP; /SLEEP powers the
	// sequencing logic down.  /ENABLE does neither, so it is not tested here.
	if (!m_reset_n || !m_sleep_n)
	{
		LOGSTEP("STEP ignored, %s asserted\n", m_reset_n ? "/SLEEP" : "/RESET");
		return;
	}

	// DIR, MS1 and MS2 take effect here rather than when they were written
	m_microsteps = decode_microsteps();
	int const increment = MICROSTEPS_PER_FULL_STEP / m_microsteps;
	int const delta = m_dir ? increment : -increment;

	m_position += delta;
	m_phase = u8((m_phase + PHASE_STATES + delta) % PHASE_STATES);

	LOGSTEP("1/%d step %s, position %d, phase %d\n", int(m_microsteps), m_dir ? "forward" : "reverse", m_position, int(m_phase));

	// the position is passed as a two's complement u64; cast it back in the handler
	m_step_cb(u64(m_position));
}


//-------------------------------------------------
//  dir_w - handle the DIR input
//-------------------------------------------------

void a4982_device::dir_w(int state)
{
	bool const level = bool(state);
	if (m_dir == level)
		return;
	m_dir = level;

	// DIR high increments the translator position (Table 2, DIR = H); which way
	// the shaft turns depends on the motor wiring, so that is the driver's business
	LOGMODE("DIR %s\n", level ? "forward" : "reverse");
}


//-------------------------------------------------
//  ms1_w - handle the MS1 input
//-------------------------------------------------

void a4982_device::ms1_w(int state)
{
	bool const level = bool(state);
	if (m_ms1 == level)
		return;
	m_ms1 = level;

	// the resolution change takes effect on the next STEP rising edge
	LOGMODE("MS1 %d, pending resolution 1/%d\n", level ? 1 : 0, int(decode_microsteps()));
}


//-------------------------------------------------
//  ms2_w - handle the MS2 input
//-------------------------------------------------

void a4982_device::ms2_w(int state)
{
	bool const level = bool(state);
	if (m_ms2 == level)
		return;
	m_ms2 = level;

	LOGMODE("MS2 %d, pending resolution 1/%d\n", level ? 1 : 0, int(decode_microsteps()));
}


//-------------------------------------------------
//  enable_w - handle the /ENABLE input
//-------------------------------------------------

void a4982_device::enable_w(int state)
{
	bool const level = bool(state);
	if (m_enable_n == level)
		return;
	m_enable_n = level;

	// /ENABLE gates the output FETs and nothing else: the microstep counter must
	// not be reset or frozen here
	LOGCONTROL("outputs %s\n", level ? "disabled" : "enabled");
}


//-------------------------------------------------
//  reset_w - handle the /RESET input
//-------------------------------------------------

void a4982_device::reset_w(int state)
{
	bool const level = bool(state);
	if (m_reset_n == level)
		return;
	m_reset_n = level;

	if (!level)
	{
		// the translator goes home and the outputs turn off; the part has no step
		// counter of its own, so m_position is left alone
		m_phase = HOME_PHASE;
		LOGCONTROL("/RESET asserted, translator forced to the home microstep position\n");
	}
	else
	{
		LOGCONTROL("/RESET released\n");
	}
}


//-------------------------------------------------
//  sleep_w - handle the /SLEEP input
//-------------------------------------------------

void a4982_device::sleep_w(int state)
{
	bool const level = bool(state);
	if (m_sleep_n == level)
		return;
	m_sleep_n = level;

	if (level)
	{
		// leaving Sleep mode homes the translator.  The 1 ms of charge pump
		// settling the datasheet asks for before the first STEP is not enforced.
		m_phase = HOME_PHASE;
		LOGCONTROL("/SLEEP released, translator homed\n");
	}
	else
	{
		LOGCONTROL("/SLEEP asserted\n");
	}
}


//-------------------------------------------------
//  set_vref - set the REF pin voltage
//-------------------------------------------------

void a4982_device::set_vref(double vref)
{
	m_vref = vref;

	if ((vref < 0.0) || (vref > 4.0))
		LOGCURRENT("VREF %f V is outside the 0 to 4 V input range\n", vref);

	LOGCURRENT("VREF %f V, ITripMax %f A\n", m_vref, current_limit());
}
