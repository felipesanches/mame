// license:GPL-2.0+
// copyright-holders:Felipe Sanches

#include "emu.h"
#include "mm2_stepper_sound.h"

#include <algorithm>
#include <cmath>


namespace {

constexpr double RADIAL = 2.0;
constexpr double TANGENTIAL = 200.0;
constexpr double SLEW = 3000.0; // A/s
constexpr double DETENT_SNAP = 0.5;
constexpr double VOICE_GAIN = 0.06;
constexpr double CEILING = 0.6;
constexpr double REGIME_CHANGE = 0.25;
constexpr double SILENT_BELOW = 1e-5;

}


const mm2_stepper_sound_device::mechanics mm2_stepper_sound_device::MECHANICS[VOICES] =
{
	{ { { 151.2, 9.0, 1.60, true }, { 430.0, 4.0, 0.55, false }, { 1900.0, 3.0, 0.30, false } }, 0.50 },
	{ { { 107.4, 9.0, 1.80, true }, { 300.0, 4.0, 0.60, false }, { 1500.0, 3.0, 0.28, false } }, 0.50 },
	{ { { 275.1, 8.0, 1.30, true }, { 283.4, 8.0, 1.30, true }, { 2300.0, 3.0, 0.35, false } }, 0.55 },
	{ { { 276.6, 7.0, 1.10, true }, { 800.0, 4.0, 0.40, false }, { 2600.0, 3.0, 0.30, false } }, 0.45 },
	{ { { 276.6, 7.0, 1.10, true }, { 800.0, 4.0, 0.40, false }, { 2600.0, 3.0, 0.30, false } }, 0.45 }
};


DEFINE_DEVICE_TYPE(MM2_STEPPER_SOUND, mm2_stepper_sound_device, "mm2_stepper_sound", "Metamaquina 2 stepper motors")

mm2_stepper_sound_device::mm2_stepper_sound_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, MM2_STEPPER_SOUND, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
{
}


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


void mm2_stepper_sound_device::retune(int axis, double rate)
{
	voice &v = m_voice[axis];
	const mechanics &m = MECHANICS[axis];
	const double scale = std::sqrt(std::max(v.current, 0.05) / 1.0);

	for (int i = 0; i < MODES; i++)
		v.mode[i].configure(m.mode[i].rotor ? (m.mode[i].hz * scale) : m.mode[i].hz, m.mode[i].q, rate);

	v.radiate.configure(8000.0, rate);

	v.tuned_at = v.current;
}


void mm2_stepper_sound_device::step_edge(int axis, double ia, double ib)
{
	if (unsigned(axis) >= VOICES)
		return;

	voice &v = m_voice[axis];
	const u32 next = (v.head + 1) % QUEUE;
	if (next == v.tail)
	{
		v.tail = (v.tail + 1) % QUEUE;
		if (!m_dropped++)
			logerror("step queue overflowed on axis %d; sound will be thin until the stream catches up\n", axis);
	}

	v.when[v.head] = machine().time().as_double();
	v.ia[v.head] = ia;
	v.ib[v.head] = ib;
	v.head = next;
}

void mm2_stepper_sound_device::set_holding(int axis, bool enabled, double ia, double ib, double itrip)
{
	if (unsigned(axis) >= VOICES)
		return;

	voice &v = m_voice[axis];

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


void mm2_stepper_sound_device::sound_stream_update(sound_stream &stream)
{
	if (getenv("MM2_NO_SOUND")) return;
	const int samples = stream.samples();
	const double rate = stream.sample_rate();
	const double t0 = stream.start_time().as_double();

	for (int i = 0; i < VOICES; i++)
	{
		voice &v = m_voice[i];

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
			const double opens = t0 + double(s) / rate;
			const double closes = opens + 1.0 / rate;

			double area = 0.0;
			double covered = 0.0;
			double impulse = v.carry;
			v.carry = 0.0;

			while (v.tail != v.head && v.when[v.tail] < closes)
			{
				const double at = std::clamp((v.when[v.tail] - opens) * rate, covered, 1.0);

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

				const double amp = TANGENTIAL * std::sqrt(jump_ia * jump_ia + jump_ib * jump_ib);
				impulse += amp * (1.0 - at);
				v.carry += amp * at;

				v.tail = (v.tail + 1) % QUEUE;
			}

			slew(v, closes - v.last_slew);
			v.last_slew = closes;
			area += v.radial * (1.0 - covered);

			const double excite = area * RADIAL + impulse;

			if (v.prime)
			{
				v.dc_x1 = excite;
				v.dc_y1 = 0.0;

				v.prime = (std::abs(v.cmd_ia - v.act_ia) + std::abs(v.cmd_ib - v.act_ib)) > 1e-6;
			}
			const double hp = excite - v.dc_x1 + 0.999 * v.dc_y1;
			v.dc_x1 = excite;
			v.dc_y1 = hp;

			const double drive = hp + v.thunk;
			v.thunk = 0.0;

			double out = m.radiation * v.radiate.run(drive);
			for (int k = 0; k < MODES; k++)
				out += m.mode[k].gain * v.mode[k].run(drive);

			stream.add(0, s, float(CEILING * std::tanh(out * VOICE_GAIN / CEILING)));
		}
	}
}
