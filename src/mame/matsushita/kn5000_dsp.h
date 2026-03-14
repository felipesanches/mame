// license:GPL2+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Technics KN5000 DSP Devices

    IC311 (DS3613GF-3BA) - "DSP1" - Parallel bus interface at 0x130000
    IC310 (MN19413)      - "DSP2" - GPIO serial interface (bit-bang)

    Both chips are digital signal processors used for audio effects
    (reverb, chorus, delay, etc.). They are controlled by a shared
    bytecode interpreter running on the Sub CPU.

    DSP1 uses a register-indirect memory-mapped interface:
      0x130000: register address byte
      0x130002: register data byte

    DSP2 uses GPIO bit-bang serial on Sub CPU ports:
      PF.0 = SDA (data), PF.2 = SCLK (clock), PE.6 = CS2 (chip select)

    This file provides stub device classes that accept register writes
    without crashing the firmware. Full DSP emulation is a long-term goal.

***************************************************************************/

#ifndef MAME_MATSUSHITA_KN5000_DSP_H
#define MAME_MATSUSHITA_KN5000_DSP_H

#pragma once

//**************************************************************************
//  DSP1 - IC311 (DS3613GF-3BA) - Parallel bus interface
//**************************************************************************

class kn5000_dsp1_device : public device_t
{
public:
	kn5000_dsp1_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	// Memory-mapped register-indirect interface (SubCPU at 0x130000)
	void addr_w(uint16_t data);    // 0x130000: register address latch
	void data_w(uint16_t data);    // 0x130002: register data write
	uint16_t data_r();             // 0x130002: register data read

	// Number of channels and registers per channel
	static constexpr int NUM_CHANNELS = 4;
	static constexpr int REGS_PER_CHANNEL = 8;
	static constexpr int CHANNEL_SPACING = 0x20;

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	uint8_t  m_addr_latch;                                    // Current register address
	uint8_t  m_regs[NUM_CHANNELS * CHANNEL_SPACING];          // Register file (flat)
};

DECLARE_DEVICE_TYPE(KN5000_DSP1, kn5000_dsp1_device)

#endif // MAME_MATSUSHITA_KN5000_DSP_H
