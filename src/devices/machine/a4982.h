// license:BSD-3-Clause
// copyright-holders:Felipe Sanches
/**********************************************************************

    Allegro MicroSystems A4982 / A4984

    DMOS microstepping bipolar stepper motor driver with built-in
    translator and overcurrent protection.  STEP/DIR interface, up to
    35 V and +/-2 A of output drive, selectable microstep resolution.

    Microstep resolution -- datasheet Table 1:

        +-----+-----+---------------+---------------+
        | MS1 | MS2 | A4982         | A4984         |
        +-----+-----+---------------+---------------+
        |  L  |  L  | full step     | full step     |
        |  H  |  L  | half step     | half step     |
        |  L  |  H  | quarter step  | quarter step  |
        |  H  |  H  | sixteenth     | eighth step   |
        +-----+-----+---------------+---------------+

    The parts are otherwise pin-, timing- and protocol-compatible; call
    set_finest_microstep(8) to configure this device as an A4984.  MS1
    and MS2 have internal pull-downs, so they read low when unconnected.

    Logic interface timing -- datasheet Figure 1: STEP minimum HIGH and
    LOW pulse widths 1 us; setup and hold, input change to STEP, 200 ns.
    The translator advances on the low-to-high transition of STEP; DIR,
    MS1 and MS2 take effect only on that edge.

    Control inputs, all three active low:

        /ENABLE  turns the output FETs off; STEP, DIR, MS1, MS2 and the
                 sequencing logic stay active, so the translator keeps
                 counting.
        /RESET   forces the translator to its home state and turns the
                 outputs off; STEP is ignored until it goes high again.
        /SLEEP   powers down the outputs, the current regulator and the
                 charge pump; restarts at the home microstep position,
                 and wants 1 ms of charge pump settling before the first
                 STEP command.

    The home microstep position is step angle 45 degrees, both phases at
    70.31 % of ITripMax (datasheet Table 2), common to all four step
    modes.

    Maximum current limiting -- datasheet, Internal PWM Current Control:

        ITripMax = VREF / (8 x RS)

    with RS the sense resistor in ohms and VREF the voltage on the REF
    pin (input range 0 to 4 V).

    Allegro datasheets A4982 rev. 7 (5 April 2022) and A4984 rev. 7
    (23 March 2022).

**********************************************************************/

#ifndef MAME_MACHINE_A4982_H
#define MAME_MACHINE_A4982_H

#pragma once


//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

// ======================> a4982_device

class a4982_device : public device_t
{
public:
	// finest microstep resolution, i.e. the MS1 = H, MS2 = H row of Table 1
	static inline constexpr u8 FINEST_A4982 = 16;
	static inline constexpr u8 FINEST_A4984 = 8;

	// construction/destruction
	a4982_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	// configuration
	// fires on every accepted STEP edge; new position as a two's complement u64
	auto step_cb() { return m_step_cb.bind(); }
	a4982_device &set_sense_resistor(double ohms) { m_rsense = ohms; return *this; }
	a4982_device &set_finest_microstep(u8 steps) { m_finest_microstep = steps; return *this; }

	// logic inputs
	void step_w(int state);      // low-to-high transition advances the translator
	void dir_w(int state);       // sampled on the STEP rising edge
	void enable_w(int state);    // /ENABLE, active low: 0 enables the output FETs
	void ms1_w(int state);       // sampled on the STEP rising edge
	void ms2_w(int state);       // "
	void reset_w(int state);     // /RESET, active low
	void sleep_w(int state);     // /SLEEP, active low

	// REF pin voltage
	void set_vref(double vref);

	// translator state
	s64 position() const { return m_position; }              // in sixteenth steps
	s64 position_full_steps() const;                         // floor(position / 16)
	u8 microsteps() const { return m_microsteps; }           // 1, 2, 4, 8 or 16
	u8 phase() const { return m_phase; }                     // 0..63, sixteenth steps
	bool outputs_enabled() const { return !m_enable_n && m_reset_n && m_sleep_n; }

	// analog state
	double current_limit() const;                            // ITripMax, in amps
	double coil_current(int coil) const;                     // signed amps: coil 0 = phase 1, coil 1 = phase 2

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_validity_check(validity_checker &valid) const override;

private:
	// the translator is modelled on a common sixteenth step grid, so a position
	// count stays comparable across a resolution change
	static inline constexpr u8 MICROSTEPS_PER_FULL_STEP = 16;
	static inline constexpr u8 PHASE_STATES = 4 * MICROSTEPS_PER_FULL_STEP;
	static inline constexpr u8 HOME_PHASE = 8; // step angle 45 degrees

	// DAC levels for one quadrant of the phase current sequence
	static const u8 s_dac_level[MICROSTEPS_PER_FULL_STEP + 1];

	// internal helpers
	u8 decode_microsteps() const;
	static double dac_fraction(u8 phase);

	// device callbacks
	devcb_write64 m_step_cb;

	// configuration parameters
	double m_rsense;
	u8 m_finest_microstep;

	// input pin state
	bool m_step;
	bool m_dir;
	bool m_enable_n;
	bool m_ms1;
	bool m_ms2;
	bool m_reset_n;
	bool m_sleep_n;
	double m_vref;

	// translator state
	s64 m_position;
	u8 m_phase;
	u8 m_microsteps;
	attotime m_last_step_edge;
};

// device type definition
DECLARE_DEVICE_TYPE(A4982, a4982_device)

#endif // MAME_MACHINE_A4982_H
