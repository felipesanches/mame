// license:BSD-3-Clause
// copyright-holders:Felipe Sanches
/***************************************************************************

    Analog Devices AD5206

    6-channel, 256-position digital potentiometer with a 3-wire
    SPI-compatible serial interface.

    Six independent resistor ladders (RDAC 1 to RDAC 6), each with its own
    A terminal, B terminal and wiper.  End-to-end A-to-B resistance is 10k,
    50k or 100k depending on the part number suffix, tapped at 256 positions
    by an 8-bit latch.

    No SDO (so no daisy chaining), no PR preset pin and no SHDN pin: those
    are AD5204-only (Rev. D Table 3 vs. Table 5).

                AD5206 (24-lead SOIC/TSSOP/PDIP)
                  ___ ___
          A6   1 |*  u  | 24  B4
          W6   2 |      | 23  W4
          B6   3 |      | 22  A4
         GND   4 |      | 21  B2
         /CS   5 |      | 20  W2
         VDD   6 |      | 19  A2
         SDI   7 |      | 18  A1
         CLK   8 |      | 17  W1
         VSS   9 |      | 16  B1
          B5  10 |      | 15  A3
          W5  11 |      | 14  W3
          A5  12 |______| 13  B3

    Wire format - 11 bits, address first, MSB first (Rev. D Table 6):

              B10 B9  B8 | B7  B6  B5  B4  B3  B2  B1  B0
              A2  A1  A0 | D7  D6  D5  D4  D3  D2  D1  D0
              \-address-/ \--------- 8-bit data --------/
              MSB     LSB  MSB                        LSB

    CLK is positive-edge triggered and is only effective while /CS is low.
    The RDAC latch addressed by A2..A0 is loaded on the rising edge of /CS.
    The shift register is free running: only the last 11 bits clocked in
    before /CS rises are decoded, so a host pushing two whole SPI bytes
    works, the leading 5 bits simply fall off the end.

    Reference: Analog Devices AD5204/AD5206 data sheet Rev. D (6/2015),
    document D06884-0-6/15(D).

***************************************************************************/

#ifndef MAME_MACHINE_AD5206_H
#define MAME_MACHINE_AD5206_H

#pragma once


//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

class ad5206_device : public device_t
{
public:
	// channel index is the A2..A0 address code, not the datasheet's 1-based
	// RDAC number: channel 0 is RDAC 1 / W1, channel 5 is RDAC 6 / W6
	static constexpr unsigned RDAC_COUNT = 6;

	// construction/destruction
	ad5206_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	// configuration
	// end-to-end resistance R_AB in ohms (part is sold as 10k, 50k or 100k)
	ad5206_device &set_resistance(double r_ab) { assert(!configured()); m_r_ab = r_ab; return *this; }
	// wiper contact resistance R_W in ohms (45 ohms typical, Rev. D p.13)
	ad5206_device &set_wiper_resistance(double r_w) { assert(!configured()); m_r_w = r_w; return *this; }
	// voltages on the A and B terminals, shared by all six ladders
	ad5206_device &set_terminal_voltages(double v_a, double v_b) { assert(!configured()); m_v_a = v_a; m_v_b = v_b; return *this; }

	// N is the A2..A0 address code; payload is the raw 8-bit wiper position
	template <unsigned N> auto wiper_cb() { return m_wiper_cb[N].bind(); }

	// serial interface pins
	void cs_w(int state);
	void clk_w(int state);
	void di_w(int state);

	// equivalent to eight clk_w() rising edges, MSB first; only accepted
	// while /CS is low
	void write(u8 data);

	// wiper readers
	u8 wiper_position(int channel) const { return (unsigned(channel) < RDAC_COUNT) ? m_rdac[channel] : 0; }
	double wiper_voltage(int channel) const;
	double wiper_resistance_wb(int channel) const;
	double wiper_resistance_wa(int channel) const;

protected:
	// device-level overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	void latch();

	// callbacks
	devcb_write8::array<RDAC_COUNT> m_wiper_cb;

	// configuration
	double m_r_ab;
	double m_r_w;
	double m_v_a;
	double m_v_b;

	// state
	u8 m_rdac[RDAC_COUNT];
	u16 m_shift;
	u8 m_bits;
	u8 m_cs;
	u8 m_clk;
	u8 m_di;
};


// device type definition
DECLARE_DEVICE_TYPE(AD5206, ad5206_device)

#endif // MAME_MACHINE_AD5206_H
