// license:GPL2+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Technics KN5000 DSP1 Device (IC311 - DS3613GF-3BA)

    Parallel bus digital signal processor used for audio effects.
    Register-indirect interface: address latch + data port.

    The firmware initializes 4 channels (0-3) with 0x20-byte spacing:
      Channel N register base = N * 0x20 + 0x10
      Each channel has 8 registers (0x10-0x17, 0x30-0x37, etc.)

    Boot sequence writes test pattern 0x5A5A5A5A to all channels,
    then configures initial values with 0x20 channel spacing.

    DSP2 (IC310, MN19413) uses GPIO serial and is handled by
    Sub CPU port callbacks in kn5000.cpp, not memory-mapped here.

***************************************************************************/

#include "emu.h"
#include "kn5000_dsp.h"

#define LOG_REG  (1U << 1)  // Register read/write

#define VERBOSE (0)
#include "logmacro.h"

DEFINE_DEVICE_TYPE(KN5000_DSP1, kn5000_dsp1_device, "kn5000_dsp1", "KN5000 DSP1 (DS3613GF-3BA)")


//**************************************************************************
//  DSP1 DEVICE
//**************************************************************************

kn5000_dsp1_device::kn5000_dsp1_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, KN5000_DSP1, tag, owner, clock)
	, m_addr_latch(0)
{
}

void kn5000_dsp1_device::device_start()
{
	save_item(NAME(m_addr_latch));
	save_item(NAME(m_regs));
}

void kn5000_dsp1_device::device_reset()
{
	m_addr_latch = 0;
	std::fill(std::begin(m_regs), std::end(m_regs), 0);
}

void kn5000_dsp1_device::addr_w(uint16_t data)
{
	m_addr_latch = data & 0xFF;
	LOGMASKED(LOG_REG, "DSP1 addr latch: 0x%02X\n", m_addr_latch);
}

void kn5000_dsp1_device::data_w(uint16_t data)
{
	uint8_t val = data & 0xFF;
	LOGMASKED(LOG_REG, "DSP1 reg[0x%02X] <- 0x%02X\n", m_addr_latch, val);

	if (m_addr_latch < std::size(m_regs))
		m_regs[m_addr_latch] = val;
}

uint16_t kn5000_dsp1_device::data_r()
{
	uint16_t val = 0;
	if (m_addr_latch < std::size(m_regs))
		val = m_regs[m_addr_latch];

	LOGMASKED(LOG_REG, "DSP1 reg[0x%02X] -> 0x%02X\n", m_addr_latch, val);
	return val;
}
