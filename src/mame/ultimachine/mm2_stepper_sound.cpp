// license:GPL-2.0+
// copyright-holders:Felipe Sanches

/*
    Metamaquina 2 stepper motor sound.

    Five Allegro A4982 microstepping drivers on an UltiMachine RAMBo board:
    X, Y, Z (two motors on two M8 screws sharing one channel) and two
    extruders.  Winding current is set per channel by a digipot.
*/

#include "emu.h"
#include "mm2_stepper_sound.h"

#include <algorithm>
#include <cmath>


namespace {

// Radial (Maxwell) pull, which goes as |I|^2.  The A4982's 16-entry sine
// table quantises it, and the quantised table's quarter-wave symmetry puts
// the ripple energy only on harmonics of the full step rate.
constexpr double RADIAL = 2.0;

// Tangential kick of each commutation, sized from the jump the current
// vector makes: sqrt(dia^2 + dib^2).  The train of them is at the step pulse
// rate, which is the fundamental.
constexpr double TANGENTIAL = 200.0;

// The chopper regulating the holding current is not modelled, so a motor
// standing still is silent.

// Winding slew limit, A/s.  A coil is an inductor and the chopper can do no
// better than put the whole supply across it: di/dt <= V/L, here 12 V into a
// NEMA 17 winding of around 4 mH.
constexpr double SLEW = 3000.0;

// Rotor snapping into the nearest detent when a channel is energised.  A
// stated size, not derived.
constexpr double DETENT_SNAP = 0.5;

// Five voices sum into one channel.
constexpr double VOICE_GAIN = 0.06;

// the loudest a single motor may get, leaving room for the others
constexpr double CEILING = 0.6;

// What separates a ripple from a change of regime.  The A4982's table moves
// the current vector's magnitude by 1.2 % over a full step, so a change in
// |I|^2 of a quarter did not come from stepping: it is the output FETs
// switching or the digipot being written, i.e. a change in the standing
// force, which must be absorbed rather than rung out.
constexpr double REGIME_CHANGE = 0.25;

// Excitation below which a voice stops being computed.  Not an envelope; the
// resonators ring down on their own.
constexpr double SILENT_BELOW = 1e-5;

} // anonymous namespace


/*
    Rotor frequencies solve f_n = sqrt(Nr*Th/J)/2pi with the transmission
    ratios taken from the firmware's steps/mm.  The rotor inertia and the
    moving masses in J are estimates, so these are model figures; the
    structural frequencies are a model outright.

    Z has one A4982 channel driving two motors on two M8 screws, so it has two
    rotors and a pair of close modes rather than one.
*/
const mm2_stepper_sound_device::mechanics mm2_stepper_sound_device::MECHANICS[VOICES] =
{
	// X: hot end carriage on a GT2 belt
	{ { { 151.2, 9.0, 1.60, true }, { 430.0, 4.0, 0.55, false }, { 1900.0, 3.0, 0.30, false } }, 0.50 },
	// Y: heated bed on a GT2 belt, the heaviest load
	{ { { 107.4, 9.0, 1.80, true }, { 300.0, 4.0, 0.60, false }, { 1500.0, 3.0, 0.28, false } }, 0.50 },
	// Z: two motors, two M8 screws, one channel
	{ { { 275.1, 8.0, 1.30, true }, { 283.4, 8.0, 1.30, true }, { 2300.0, 3.0, 0.35, false } }, 0.55 },
	// E0: geared extruder on the moving carriage
	{ { { 276.6, 7.0, 1.10, true }, { 800.0, 4.0, 0.40, false }, { 2600.0, 3.0, 0.30, false } }, 0.45 },
	// E1: the same part, not fitted on a standard Metamaquina 2
	{ { { 276.6, 7.0, 1.10, true }, { 800.0, 4.0, 0.40, false }, { 2600.0, 3.0, 0.30, false } }, 0.45 }
};


DEFINE_DEVICE_TYPE(MM2_STEPPER_SOUND, mm2_stepper_sound_device, "mm2_stepper_sound", "Metamaquina 2 stepper motors")

mm2_stepper_sound_device::mm2_stepper_sound_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, MM2_STEPPER_SOUND, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
{
}


//-------------------------------------------------
//  resonator::configure - a two-pole bandpass with
//  constant skirt gain and unity gain at the peak
//-------------------------------------------------

void mm2_stepper_sound_device::lowpass::configure(double freq, double rate)
{
	m_a = 1.0 - std::exp(-2.0 * M_PI * std::clamp(freq, 10.0, rate * 0.45) / rate);
}


void mm2_stepper_sound_device::resonator::configure(double freq, double q, double rate)
{
	freq = std::clamp(freq, 10.0, rate * 0.45);
	const double r = std::exp(-M_PI * freq / (q * rate));
	const double w = 2.0 * M_PI * freq / rate;

	b1 = 2.0 * r * std::cos(w);
	b2 = -r * r;
	a = (1.0 - r * r) * 0.5;
}

double mm2_stepper_sound_device::resonator::run(double x)
{
	const double y = a * (x - x2) + b1 * y1 + b2 * y2;
	x2 = x1; x1 = x;
	y2 = y1; y1 = y;
	return y;
}


// Advance the windings toward what the A4982 is asking for.  This is the
// inductor limit, not a smoothing filter: below the limit the current follows
// the table exactly.
void mm2_stepper_sound_device::slew(voice &v, double dt)
{
	if (dt <= 0.0)
		return;

	const double reach = SLEW * std::min(dt, 1.0);
	v.act_ia += std::clamp(v.cmd_ia - v.act_ia, -reach, reach);
	v.act_ib += std::clamp(v.cmd_ib - v.act_ib, -reach, reach);
	v.radial = v.act_ia * v.act_ia + v.act_ib * v.act_ib;
}


void mm2_stepper_sound_device::device_start()
{
	m_stream = stream_alloc(0, 1, 48000);

	for (int i = 0; i < VOICES; i++)
	{
		voice &v = m_voice[i];
		save_item(NAME(v.cmd_ia), i);
		save_item(NAME(v.cmd_ib), i);
		save_item(NAME(v.act_ia), i);
		save_item(NAME(v.act_ib), i);
		save_item(NAME(v.last_slew), i);
		save_item(NAME(v.radial), i);
		save_item(NAME(v.carry), i);
		save_item(NAME(v.dc_x1), i);
		save_item(NAME(v.dc_y1), i);
		save_item(NAME(v.current), i);
		save_item(NAME(v.enabled), i);
	}

	// the step queues hold at most one stream update of pending edges, so they
	// are not part of the machine's state
}

void mm2_stepper_sound_device::device_reset()
{
	for (voice &v : m_voice)
	{
		v.head = v.tail = 0;
		v.cmd_ia = v.cmd_ib = v.act_ia = v.act_ib = v.radial = 0.0;
		v.last_slew = 0.0;
		v.carry = v.thunk = v.dc_x1 = v.dc_y1 = 0.0;
		v.prime = true;
		v.tuned_at = -1.0;
		for (resonator &r : v.mode)
			r = resonator();
		v.radiate = lowpass();
	}
	m_dropped = 0;
}

void mm2_stepper_sound_device::device_post_load()
{
	for (voice &v : m_voice)
	{
		v.head = v.tail = 0;
		v.carry = 0.0;
		v.prime = true;
	}
}


// A rotor mode's restoring force is the holding torque, which is proportional
// to winding current, so its frequency goes as the square root of the current.
void mm2_stepper_sound_device::retune(int axis, double rate)
{
	voice &v = m_voice[axis];
	const mechanics &m = MECHANICS[axis];
	const double scale = std::sqrt(std::max(v.current, 0.05) / 1.0);

	for (int i = 0; i < MODES; i++)
		v.mode[i].configure(m.mode[i].rotor ? (m.mode[i].hz * scale) : m.mode[i].hz, m.mode[i].q, rate);

	// A plain roll-off, with the corner high enough to pass the step-rate
	// harmonic comb: TI SLVAES8A prints a measured spectrum for a motor stepped
	// at 2000 pulses per second with harmonics marked at 4, 6, 8 and 10 kHz.
	v.radiate.configure(8000.0, rate);

	v.tuned_at = v.current;
}


// One microstep, with the winding currents that followed it.  The edge is
// queued with its timestamp rather than acted on, because the CPU emulating
// the firmware runs ahead of the sound stream.
void mm2_stepper_sound_device::step_edge(int axis, double ia, double ib)
{
	if (unsigned(axis) >= VOICES)
		return;

	voice &v = m_voice[axis];
	const u32 next = (v.head + 1) % QUEUE;
	if (next == v.tail)
	{
		// drop the oldest edge rather than the newest, so the pitch stays right
		v.tail = (v.tail + 1) % QUEUE;
		if (!m_dropped++)
			logerror("step queue overflowed on axis %d; sound will be thin until the stream catches up\n", axis);
	}

	v.when[v.head] = machine().time().as_double();
	v.ia[v.head] = ia;
	v.ib[v.head] = ib;
	v.head = next;
}

// The standing state of one channel.  The holding currents are only taken
// while nothing is queued: during a move the queued edges carry the times the
// steps really happened and are the more exact account.
void mm2_stepper_sound_device::set_holding(int axis, bool enabled, double ia, double ib, double itrip)
{
	if (unsigned(axis) >= VOICES)
		return;

	voice &v = m_voice[axis];

	// Called from a timer, and it changes state the stream reads, so bring the
	// stream up to the present before writing the change in.
	if ((v.enabled != enabled) || (v.current != itrip))
		m_stream->update();

	if (v.enabled != enabled)
	{
		v.enabled = enabled;
		v.prime = true;
		v.thunk += DETENT_SNAP * itrip;
	}

	if (v.current != itrip)
	{
		// a digipot write changes the standing force, not a ripple
		v.current = itrip;
		v.prime = true;

	}

	if (v.tail == v.head)
	{
		const double want_ia = enabled ? ia : 0.0;
		const double want_ib = enabled ? ib : 0.0;
		const double had = v.cmd_ia * v.cmd_ia + v.cmd_ib * v.cmd_ib;
		const double gets = want_ia * want_ia + want_ib * want_ib;
		if (std::abs(gets - had) > REGIME_CHANGE * std::max(had, gets))
			v.prime = true;

		v.cmd_ia = want_ia;
		v.cmd_ib = want_ib;
	}
}


/*
    Per sample, per voice: the radial force |I|^2 is piecewise constant between
    step edges, so it is integrated across the sample rather than point
    sampled -- microsteps can arrive faster than this stream runs.  Each
    tangential impulse is deposited across the two samples it falls between,
    weighted by where in the interval it landed.  The result is high-passed --
    only the ripple radiates -- and fed to the resonances.
*/
void mm2_stepper_sound_device::sound_stream_update(sound_stream &stream)
{
	if (getenv("MM2_NO_SOUND")) return;
	const int samples = stream.samples();
	const double rate = stream.sample_rate();
	const double t0 = stream.start_time().as_double();

	for (int i = 0; i < VOICES; i++)
	{
		voice &v = m_voice[i];

		// Drop the voice once there is nothing left for it to do.  "enabled" is
		// deliberately not part of the test: an axis whose FETs have just been
		// switched off is still slewing its windings to zero, and that is audible.
		double ringing = std::abs(v.radiate.m_y);
		for (const resonator &r : v.mode)
			ringing += std::abs(r.y1);
		const double settling = std::abs(v.cmd_ia - v.act_ia) + std::abs(v.cmd_ib - v.act_ib);
		if ((v.tail == v.head) && (v.thunk == 0.0)
				&& (settling < 1e-6) && (ringing < SILENT_BELOW))
			continue;

		if (v.current != v.tuned_at)
			retune(i, rate);

		const mechanics &m = MECHANICS[i];

		for (int s = 0; s < samples; s++)
		{
			// the window this sample covers, in machine seconds
			const double opens = t0 + double(s) / rate;
			const double closes = opens + 1.0 / rate;

			double area = 0.0;      // integral of |I|^2 across the sample
			double covered = 0.0;   // how much of it has been accounted for
			double impulse = v.carry;
			v.carry = 0.0;

			while (v.tail != v.head && v.when[v.tail] < closes)
			{
				// where in this sample the edge falls, 0 to 1
				const double at = std::clamp((v.when[v.tail] - opens) * rate, covered, 1.0);

				// the windings are advanced before the span they cover is
				// integrated, so the excitation a sample carries is the one the
				// next sample will see
				slew(v, v.when[v.tail] - v.last_slew);
				v.last_slew = v.when[v.tail];

				area += v.radial * (at - covered);
				covered = at;

				const double jump_ia = v.ia[v.tail] - v.cmd_ia;
				const double jump_ib = v.ib[v.tail] - v.cmd_ib;

				const double had = v.cmd_ia * v.cmd_ia + v.cmd_ib * v.cmd_ib;
				const double gets = v.ia[v.tail] * v.ia[v.tail] + v.ib[v.tail] * v.ib[v.tail];
				if (std::abs(gets - had) > REGIME_CHANGE * std::max(had, gets))
					v.prime = true;

				v.cmd_ia = v.ia[v.tail];
				v.cmd_ib = v.ib[v.tail];

				// the kick is as big as the jump; see TANGENTIAL above
				const double amp = TANGENTIAL * std::sqrt(jump_ia * jump_ia + jump_ib * jump_ib);
				impulse += amp * (1.0 - at);
				v.carry += amp * at;

				v.tail = (v.tail + 1) % QUEUE;
			}

			slew(v, closes - v.last_slew);
			v.last_slew = closes;
			area += v.radial * (1.0 - covered);

			const double excite = area * RADIAL + impulse;

			// only the ripple radiates; the standing pull just loads the bearings.
			// A known change in the standing part -- the FETs switching, the
			// digipot being programmed -- is absorbed here rather than rung out.
			if (v.prime)
			{
				v.dc_x1 = excite;
				v.dc_y1 = 0.0;

				// hold the cancellation until the windings have arrived: the ramp
				// lasts a third of a millisecond, a dozen samples
				v.prime = (std::abs(v.cmd_ia - v.act_ia) + std::abs(v.cmd_ib - v.act_ib)) > 1e-6;
			}
			const double hp = excite - v.dc_x1 + 0.999 * v.dc_y1;
			v.dc_x1 = excite;
			v.dc_y1 = hp;

			// the detent snap is a mechanical event, not a change in the standing
			// magnetic force, so it goes in after the DC blocker rather than being
			// absorbed along with the transition that caused it
			const double drive = hp + v.thunk;
			v.thunk = 0.0;

			double out = m.radiation * v.radiate.run(drive);
			for (int k = 0; k < MODES; k++)
				out += m.mode[k].gain * v.mode[k].run(drive);

			// saturate rather than clip: a detent snap at the digipot's power-up
			// current is a far larger event than anything a moving axis produces
			stream.add(0, s, float(CEILING * std::tanh(out * VOICE_GAIN / CEILING)));
		}
	}
}
