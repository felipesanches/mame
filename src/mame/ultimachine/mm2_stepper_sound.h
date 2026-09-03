// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Metamaquina 2 stepper motor sound.

    The excitation is built from the A4982 coil currents, not from the step
    rate: the radial (Maxwell) force goes as ia^2 + ib^2, which ripples only
    because the A4982 sine table is quantised to sixteen entries, and each
    commanded microstep adds a tangential torque impulse.  Both are periodic
    over the electrical cycle (four full steps, or 64 STEP pulses at 1/16
    microstepping), so the line spectrum sits on multiples of the full step
    rate.

    Each axis then runs through a rotor torsional mode plus two structural
    modes standing in for the frame.  The rotor mode sits at
    f_n = sqrt(Nr * Th / J) / 2pi and moves with the winding current, since the
    holding torque Th is proportional to it.  The moving masses, the rotor
    inertia and the structural modes are estimates.

***************************************************************************/

#ifndef MAME_ULTIMACHINE_MM2_STEPPER_SOUND_H
#define MAME_ULTIMACHINE_MM2_STEPPER_SOUND_H

#pragma once


class mm2_stepper_sound_device : public device_t, public device_sound_interface
{
public:
	static constexpr int VOICES = 5;

	mm2_stepper_sound_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	// one accepted microstep on a channel, with the winding currents the A4982
	// is putting out now that it has taken it
	void step_edge(int axis, double ia, double ib);

	// standing state of one channel, pushed periodically rather than on step
	// edges: whether the output FETs are on, the currents the windings hold
	// while nothing is stepping, and ITripMax from the digipot
	void set_holding(int axis, bool enabled, double ia, double ib, double itrip);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_post_load() override;
	virtual void sound_stream_update(sound_stream &stream) override;

private:
	// rotor mode, plus two structural modes standing in for the frame
	static constexpr int MODES = 3;

	// microsteps that may arrive between two stream updates
	static constexpr int QUEUE = 2048;

	// one-pole low pass, for the direct radiation path
	struct lowpass
	{
		void configure(double freq, double rate);
		double run(double x) { m_y += m_a * (x - m_y); return m_y; }

		double m_a = 1.0;
		double m_y = 0.0;
	};

	// two-pole resonator, constant skirt gain, unity gain at the peak
	struct resonator
	{
		void configure(double freq, double q, double rate);
		double run(double x);

		double a = 0.0, b1 = 0.0, b2 = 0.0;
		double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
	};

	// one resonance; a rotor mode's frequency moves with the winding current,
	// a structural mode's does not
	struct mode_spec
	{
		double hz;              // at 1.0 A, for a rotor mode
		double q;
		double gain;
		bool rotor;
	};

	// per-axis mechanical model
	struct mechanics
	{
		mode_spec mode[MODES];
		double radiation;       // how much of the raw excitation escapes directly
	};

	static const mechanics MECHANICS[VOICES];

	struct voice
	{
		// pending step edges, as absolute machine seconds and the coil
		// currents that followed them
		double when[QUEUE] = { 0.0 };
		double ia[QUEUE] = { 0.0 };
		double ib[QUEUE] = { 0.0 };
		u32 head = 0;           // next slot to write
		u32 tail = 0;           // next slot to read

		// state held between blocks
		double cmd_ia = 0.0;    // what the A4982 is asking the coils to carry
		double cmd_ib = 0.0;
		double act_ia = 0.0;    // what the windings have managed to reach
		double act_ib = 0.0;
		double last_slew = 0.0; // machine time the windings were last advanced
		double radial = 0.0;    // act_ia^2 + act_ib^2, in A^2
		double carry = 0.0;     // impulse energy owed to the next block's sample 0
		double thunk = 0.0;     // pending detent snap, from an enable change
		double dc_x1 = 0.0;     // the DC blocker's memory
		double dc_y1 = 0.0;

		double current = 0.0;   // ITripMax, amps
		bool enabled = false;
		bool prime = true;      // absorb the next sample's step in the DC blocker

		double tuned_at = -1.0; // the current the resonators were tuned for
		resonator mode[MODES];
		lowpass radiate;        // the direct path's roll-off
	};

	void retune(int axis, double rate);
	static void slew(voice &v, double dt);

	sound_stream *m_stream = nullptr;
	voice m_voice[VOICES];
	u32 m_dropped = 0;
};

DECLARE_DEVICE_TYPE(MM2_STEPPER_SOUND, mm2_stepper_sound_device)

#endif // MAME_ULTIMACHINE_MM2_STEPPER_SOUND_H
