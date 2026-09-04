// license:GPL-2.0+
// copyright-holders:Felipe Sanches

#ifndef MAME_ULTIMACHINE_MM2_STEPPER_SOUND_H
#define MAME_ULTIMACHINE_MM2_STEPPER_SOUND_H

#pragma once


class mm2_stepper_sound_device : public device_t, public device_sound_interface
{
public:
	static constexpr int VOICES = 5;

	mm2_stepper_sound_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	void step_edge(int axis, double ia, double ib);

	void set_holding(int axis, bool enabled, double ia, double ib, double itrip);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_post_load() override;
	virtual void sound_stream_update(sound_stream &stream) override;

private:
	static constexpr int MODES = 3;

	static constexpr int QUEUE = 2048;

	struct lowpass
	{
		void configure(double freq, double rate);
		double run(double x) { m_y += m_a * (x - m_y); return m_y; }

		double m_a = 1.0;
		double m_y = 0.0;
	};

	struct resonator
	{
		void configure(double freq, double q, double rate);
		double run(double x);

		double a = 0.0, b1 = 0.0, b2 = 0.0;
		double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
	};

	struct mode_spec
	{
		double hz;
		double q;
		double gain;
		bool rotor;
	};

	struct mechanics
	{
		mode_spec mode[MODES];
		double radiation;
	};

	static const mechanics MECHANICS[VOICES];

	struct voice
	{
		double when[QUEUE] = { 0.0 };
		double ia[QUEUE] = { 0.0 };
		double ib[QUEUE] = { 0.0 };
		u32 head = 0;
		u32 tail = 0;

		double cmd_ia = 0.0;
		double cmd_ib = 0.0;
		double act_ia = 0.0;
		double act_ib = 0.0;
		double last_slew = 0.0;
		double radial = 0.0;
		double carry = 0.0;
		double thunk = 0.0;
		double dc_x1 = 0.0;
		double dc_y1 = 0.0;

		double current = 0.0;
		bool enabled = false;
		bool prime = true;

		double tuned_at = -1.0;
		resonator mode[MODES];
		lowpass radiate;
	};

	void retune(int axis, double rate);
	static void slew(voice &v, double dt);

	sound_stream *m_stream = nullptr;
	voice m_voice[VOICES];
	u32 m_dropped = 0;
};

DECLARE_DEVICE_TYPE(MM2_STEPPER_SOUND, mm2_stepper_sound_device)

#endif // MAME_ULTIMACHINE_MM2_STEPPER_SOUND_H
