// license:BSD-3-Clause
// copyright-holders:Felipe Sanches

#include "emu.h"
#include "max6675.h"

#include <cmath>


DEFINE_DEVICE_TYPE(MAX6675, max6675_device, "max6675", "Maxim MAX6675 K-Thermocouple to Digital Converter")


max6675_device::max6675_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock) :
	device_t(mconfig, MAX6675, tag, owner, clock),
	m_temperature(25.0),
	m_open(false),
	m_cs_n(true),
	m_sck(false),
	m_clocked(false),
	m_word(0),
	m_bit(0)
{
}


void max6675_device::device_start()
{
	save_item(NAME(m_temperature));
	save_item(NAME(m_open));
	save_item(NAME(m_cs_n));
	save_item(NAME(m_sck));
	save_item(NAME(m_clocked));
	save_item(NAME(m_word));
	save_item(NAME(m_bit));
}


void max6675_device::device_reset()
{
	m_clocked = false;
	m_word = conversion();
	m_bit = 0;
}


void max6675_device::set_temperature(double celsius)
{
	m_temperature = celsius;
}


void max6675_device::set_open(bool open)
{
	m_open = open;
}


u16 max6675_device::conversion() const
{
	if (m_open)
		return 1 << 2;

	double const quarters = std::floor(std::clamp(m_temperature, 0.0, 1023.75) * 4.0 + 0.5);
	return u16(quarters) << 3;
}


void max6675_device::cs_w(int state)
{
	bool const level = bool(state);
	if (m_cs_n == level)
		return;
	m_cs_n = level;

	if (!level)
	{
		m_word = conversion();
		m_bit = 0;
		m_clocked = false;
	}
}


void max6675_device::sck_w(int state)
{
	bool const level = bool(state);
	if (m_sck == level)
		return;
	m_sck = level;

	if (m_cs_n)
		return;

	if (level)
	{
		m_clocked = true;
	}
	else if (m_clocked)
	{
		m_clocked = false;
		if (m_bit < 16)
			m_bit++;
	}
}


int max6675_device::so_r()
{
	if (m_cs_n || (m_bit >= 16))
		return 0;

	return BIT(m_word, 15 - m_bit);
}
