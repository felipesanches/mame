// license:BSD-3-Clause
// copyright-holders:Felipe Sanches

#ifndef MAME_MACHINE_MAX6675_H
#define MAME_MACHINE_MAX6675_H

#pragma once

class max6675_device : public device_t
{
public:
	max6675_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	void set_temperature(double celsius);
	void set_open(bool open);

	void cs_w(int state);
	void sck_w(int state);
	int so_r();

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	u16 conversion() const;

	double m_temperature;
	bool m_open;

	bool m_cs_n;
	bool m_sck;
	bool m_clocked;
	u16 m_word;
	u8 m_bit;
};

DECLARE_DEVICE_TYPE(MAX6675, max6675_device)

#endif // MAME_MACHINE_MAX6675_H
