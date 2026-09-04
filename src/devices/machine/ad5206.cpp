// license:BSD-3-Clause
// copyright-holders:Felipe Sanches

#include "emu.h"
#include "ad5206.h"

#define LOG_XFER  (1U << 1)
#define LOG_BITS  (1U << 2)
#define LOG_WIPER (1U << 3)

#define VERBOSE (0)
#include "logmacro.h"

#define LOGXFER(...)  LOGMASKED(LOG_XFER,  __VA_ARGS__)
#define LOGBITS(...)  LOGMASKED(LOG_BITS,  __VA_ARGS__)
#define LOGWIPER(...) LOGMASKED(LOG_WIPER, __VA_ARGS__)


DEFINE_DEVICE_TYPE(AD5206, ad5206_device, "ad5206", "AD5206 6-channel digital potentiometer")


ad5206_device::ad5206_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock) :
	device_t(mconfig, AD5206, tag, owner, clock),
	m_wiper_cb(*this),
	m_r_ab(10000.0),
	m_r_w(45.0),
	m_v_a(5.0),
	m_v_b(0.0),
	m_shift(0),
	m_bits(0),
	m_cs(1),
	m_clk(0),
	m_di(0)
{
}


void ad5206_device::device_start()
{
	for (unsigned i = 0; i < RDAC_COUNT; i++)
		m_rdac[i] = 0x80;

	m_shift = 0;
	m_bits = 0;
	m_cs = 1;
	m_clk = 0;
	m_di = 0;

	save_item(NAME(m_rdac));
	save_item(NAME(m_shift));
	save_item(NAME(m_bits));
	save_item(NAME(m_cs));
	save_item(NAME(m_clk));
	save_item(NAME(m_di));
}

void ad5206_device::device_reset()
{
	// no reset pin: the RDAC latches survive a host reset
	m_shift = 0;
	m_bits = 0;
	m_cs = 1;
	m_clk = 0;
	m_di = 0;

	for (unsigned i = 0; i < RDAC_COUNT; i++)
		m_wiper_cb[i](m_rdac[i]);
}


void ad5206_device::cs_w(int state)
{
	state = state ? 1 : 0;
	if (state == m_cs)
		return;
	m_cs = state;

	if (!m_cs)
	{
		m_bits = 0;
	}
	else
	{
		latch();
	}
}

void ad5206_device::clk_w(int state)
{
	state = state ? 1 : 0;
	if (state == m_clk)
		return;
	m_clk = state;

	if (!m_clk || m_cs)
		return;

	m_shift = u16((m_shift << 1) | m_di);
	if (m_bits < 0xff)
		m_bits++;

	LOGBITS("bit %u in = %u, shift register %04x\n", m_bits, m_di, m_shift);
}

void ad5206_device::di_w(int state)
{
	m_di = state ? 1 : 0;
}

void ad5206_device::write(u8 data)
{
	if (m_cs)
	{
		LOGXFER("byte %02x ignored, /CS is high\n", data);
		return;
	}

	for (int i = 7; i >= 0; i--)
	{
		m_shift = u16((m_shift << 1) | BIT(data, i));
		if (m_bits < 0xff)
			m_bits++;
	}

	LOGBITS("byte %02x in, shift register %04x\n", data, m_shift);
}

void ad5206_device::latch()
{
	const unsigned address = (m_shift >> 8) & 0x07;
	const u8 data = m_shift & 0xff;

	if (m_bits == 0)
	{
		LOGXFER("/CS pulsed with no clocks, re-latching %04x\n", m_shift);
	}
	else if (m_bits < 11)
	{
		logerror("truncated frame: only %u bits clocked in before /CS rose, a word is 11 bits\n", m_bits);
	}
	else
	{
		LOGXFER("/CS rose after %u bits: address %u (RDAC %u), data %02x\n", m_bits, address, address + 1, data);
	}

	if (address >= RDAC_COUNT)
	{
		logerror("address %u decodes to no RDAC latch, data %02x discarded\n", address, data);
		return;
	}

	if (m_rdac[address] == data)
		return;

	m_rdac[address] = data;

	LOGWIPER("W%u = %u (%.4f V, R_WB %.1f ohms)\n", address + 1, data, wiper_voltage(int(address)), wiper_resistance_wb(int(address)));

	m_wiper_cb[address](data);
}


double ad5206_device::wiper_voltage(int channel) const
{
	if (unsigned(channel) >= RDAC_COUNT)
		return m_v_b;

	return m_v_b + (double(m_rdac[channel]) / 256.0) * (m_v_a - m_v_b);
}

double ad5206_device::wiper_resistance_wb(int channel) const
{
	if (unsigned(channel) >= RDAC_COUNT)
		return m_r_w;

	return (double(m_rdac[channel]) / 256.0) * m_r_ab + m_r_w;
}

double ad5206_device::wiper_resistance_wa(int channel) const
{
	if (unsigned(channel) >= RDAC_COUNT)
		return m_r_ab + m_r_w;

	return (double(256 - m_rdac[channel]) / 256.0) * m_r_ab + m_r_w;
}
