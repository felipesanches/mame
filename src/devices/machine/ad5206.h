// license:BSD-3-Clause
// copyright-holders:Felipe Sanches

#ifndef MAME_MACHINE_AD5206_H
#define MAME_MACHINE_AD5206_H

#pragma once


class ad5206_device : public device_t
{
public:
	static constexpr unsigned RDAC_COUNT = 6;

	ad5206_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	ad5206_device &set_resistance(double r_ab) { assert(!configured()); m_r_ab = r_ab; return *this; } // ohms
	ad5206_device &set_wiper_resistance(double r_w) { assert(!configured()); m_r_w = r_w; return *this; } // ohms
	ad5206_device &set_terminal_voltages(double v_a, double v_b) { assert(!configured()); m_v_a = v_a; m_v_b = v_b; return *this; }

	template <unsigned N> auto wiper_cb() { return m_wiper_cb[N].bind(); }

	void cs_w(int state);
	void clk_w(int state);
	void di_w(int state);

	void write(u8 data);

	u8 wiper_position(int channel) const { return (unsigned(channel) < RDAC_COUNT) ? m_rdac[channel] : 0; }
	double wiper_voltage(int channel) const;
	double wiper_resistance_wb(int channel) const;
	double wiper_resistance_wa(int channel) const;

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	void latch();

	devcb_write8::array<RDAC_COUNT> m_wiper_cb;

	double m_r_ab;
	double m_r_w;
	double m_v_a;
	double m_v_b;

	u8 m_rdac[RDAC_COUNT];
	u16 m_shift;
	u8 m_bits;
	u8 m_cs;
	u8 m_clk;
	u8 m_di;
};


DECLARE_DEVICE_TYPE(AD5206, ad5206_device)

#endif // MAME_MACHINE_AD5206_H
