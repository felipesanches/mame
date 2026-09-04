// license:BSD-3-Clause
// copyright-holders:Felipe Sanches

#ifndef MAME_MACHINE_A4982_H
#define MAME_MACHINE_A4982_H

#pragma once

class a4982_device : public device_t
{
public:
	static inline constexpr u8 FINEST_A4982 = 16;
	static inline constexpr u8 FINEST_A4984 = 8;

	a4982_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	auto step_cb() { return m_step_cb.bind(); }
	a4982_device &set_sense_resistor(double ohms) { m_rsense = ohms; return *this; }
	a4982_device &set_finest_microstep(u8 steps) { m_finest_microstep = steps; return *this; }

	void step_w(int state);
	void dir_w(int state);
	void enable_w(int state);
	void ms1_w(int state);
	void ms2_w(int state);
	void reset_w(int state);
	void sleep_w(int state);

	void set_vref(double vref);

	s64 position() const { return m_position; } // in sixteenth steps
	s64 position_full_steps() const;
	u8 microsteps() const { return m_microsteps; }
	u8 phase() const { return m_phase; }
	bool outputs_enabled() const { return !m_enable_n && m_reset_n && m_sleep_n; }

	double current_limit() const;
	double coil_current(int coil) const;

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_validity_check(validity_checker &valid) const override;

private:
	static inline constexpr u8 MICROSTEPS_PER_FULL_STEP = 16;
	static inline constexpr u8 PHASE_STATES = 4 * MICROSTEPS_PER_FULL_STEP;
	static inline constexpr u8 HOME_PHASE = 8;

	static const u8 s_dac_level[MICROSTEPS_PER_FULL_STEP + 1];

	u8 decode_microsteps() const;
	static double dac_fraction(u8 phase);

	devcb_write64 m_step_cb;

	double m_rsense;
	u8 m_finest_microstep;

	bool m_step;
	bool m_dir;
	bool m_enable_n;
	bool m_ms1;
	bool m_ms2;
	bool m_reset_n;
	bool m_sleep_n;
	double m_vref;

	s64 m_position;
	u8 m_phase;
	u8 m_microsteps;
	attotime m_last_step_edge;
};

DECLARE_DEVICE_TYPE(A4982, a4982_device)

#endif // MAME_MACHINE_A4982_H
