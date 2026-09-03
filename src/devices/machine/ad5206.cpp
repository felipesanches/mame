// license:BSD-3-Clause
// copyright-holders:Felipe Sanches
/*

Analog Devices AD5206 - 6-channel, 256-position digital potentiometer

Six independent resistor ladders sharing one 3-wire SPI-compatible serial port.
Each ladder is tapped at 256 positions by its own 8-bit RDAC latch.  Write only:
no SDO, no PR preset pin, no SHDN pin.

Serial protocol (Rev. D Table 6, Table 9, Table 10, p.15):
- 11 bits per word: A2 A1 A0 then D7..D0, MSB first
- CLK is positive edge triggered and only shifts while /CS is low
- on the rising edge of /CS the last 11 bits held in the shift register are
  decoded, loading D7..D0 into the addressed RDAC latch.  Words longer than
  11 bits are legal: only the last 11 bits matter
- address decode: 000..101 -> RDAC1..RDAC6, 110 and 111 select no latch

Analog Devices AD5204/AD5206 data sheet Rev. D (6/2015), D06884-0-6/15(D).

*/

#include "emu.h"
#include "ad5206.h"

#define LOG_XFER  (1U << 1) // one line per completed /CS frame
#define LOG_BITS  (1U << 2) // one line per bit or byte shifted in
#define LOG_WIPER (1U << 3) // one line per wiper that actually moves

#define VERBOSE (0)
//#define LOG_OUTPUT_FUNC osd_printf_info
#include "logmacro.h"

#define LOGXFER(...)  LOGMASKED(LOG_XFER,  __VA_ARGS__)
#define LOGBITS(...)  LOGMASKED(LOG_BITS,  __VA_ARGS__)
#define LOGWIPER(...) LOGMASKED(LOG_WIPER, __VA_ARGS__)


DEFINE_DEVICE_TYPE(AD5206, ad5206_device, "ad5206", "AD5206 6-channel digital potentiometer")

//-------------------------------------------------
//  ad5206_device - constructor
//-------------------------------------------------

ad5206_device::ad5206_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock) :
	device_t(mconfig, AD5206, tag, owner, clock),
	m_wiper_cb(*this),
	m_r_ab(10000.0), // 10k version of the part (AD5206BN10 / AD5206BRU10)
	m_r_w(45.0),     // typical wiper contact resistance, Rev. D p.13
	m_v_a(5.0),      // A to 5 V, B to ground
	m_v_b(0.0),
	m_shift(0),
	m_bits(0),
	m_cs(1),
	m_clk(0),
	m_di(0)
{
}


//-------------------------------------------------
//  initialization
//-------------------------------------------------

void ad5206_device::device_start()
{
	// internal power-on preset puts every wiper at midscale, Rev. D p.12
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
	// no reset pin: the RDAC latches survive a host reset, only the serial
	// interface returns to idle
	m_shift = 0;
	m_bits = 0;
	m_cs = 1;
	m_clk = 0;
	m_di = 0;

	// re-announce the wiper positions for downstream devices that also reset
	for (unsigned i = 0; i < RDAC_COUNT; i++)
		m_wiper_cb[i](m_rdac[i]);
}


//-------------------------------------------------
//  serial interface
//-------------------------------------------------

void ad5206_device::cs_w(int state)
{
	state = state ? 1 : 0;
	if (state == m_cs)
		return;
	m_cs = state;

	if (!m_cs)
	{
		// the part has no way to clear the shift register, only the bit count
		m_bits = 0;
	}
	else
	{
		// the last 11 bits held in the register are decoded when /CS returns
		// high, Rev. D p.15
		latch();
	}
}

void ad5206_device::clk_w(int state)
{
	state = state ? 1 : 0;
	if (state == m_clk)
		return;
	m_clk = state;

	// data is loaded on each positive clock edge while /CS is low, Rev. D p.15
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
	// byte-level path for a host with a hardware SPI engine: eight rising CLK
	// edges, MSB first
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
	// only the last 11 bits shifted in are decoded
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

	// Table 10: 000 to 101 select RDAC1 to RDAC6, the other two codes no latch
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


//-------------------------------------------------
//  wiper readers
//-------------------------------------------------

double ad5206_device::wiper_voltage(int channel) const
{
	// V_W(D) = D/256 * V_AB + V_B - Rev. D eq. 3, p.14
	if (unsigned(channel) >= RDAC_COUNT)
		return m_v_b;

	return m_v_b + (double(m_rdac[channel]) / 256.0) * (m_v_a - m_v_b);
}

double ad5206_device::wiper_resistance_wb(int channel) const
{
	// R_WB(D) = D/256 * R_AB + R_W - Rev. D eq. 1, p.13
	if (unsigned(channel) >= RDAC_COUNT)
		return m_r_w;

	return (double(m_rdac[channel]) / 256.0) * m_r_ab + m_r_w;
}

double ad5206_device::wiper_resistance_wa(int channel) const
{
	// R_WA(D) = (256 - D)/256 * R_AB + R_W - Rev. D eq. 2, p.13
	if (unsigned(channel) >= RDAC_COUNT)
		return m_r_ab + m_r_w;

	return (double(256 - m_rdac[channel]) / 256.0) * m_r_ab + m_r_w;
}
