// license:GPL2+
// copyright-holders:Felipe Sanches
/***************************************************************************

    DS3613GF-3BA Effect DSP (IC311)

    Memory-mapped DSP used in the Technics SX-KN5000 for digital effects
    (reverb, chorus, delay, etc). Connected to Sub CPU at:
      0x130000 (address register), 0x130002 (data register)

    Register layout: 4 channels x 32 registers = 128 bytes
      Channel 0: 0x00-0x1F   Channel 1: 0x20-0x3F
      Channel 2: 0x40-0x5F   Channel 3: 0x60-0x7F

    Known per-channel registers (from SubCPU firmware DSP_Write_Channel):
      0x10-0x17: Voice parameters (8 bytes)
      0x1F:      Channel config (written as 0x01 during init)

***************************************************************************/

#ifndef MAME_SOUND_DS3613GF3BA_H
#define MAME_SOUND_DS3613GF3BA_H

#pragma once

class ds3613gf3ba_device : public device_t
{
public:
	ds3613gf3ba_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// Memory-mapped register interface (SubCPU 0x130000/0x130002)
	void addr_w(uint16_t data);
	void data_w(uint16_t data);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	uint8_t m_addr;        // Current register address (0x00-0x7F)
	uint8_t m_regs[128];   // 4 channels x 32 registers
};

// Effect type name table (from MainCPU ROM at 0xE32A7A, 128 entries)
extern char const *const DS3613GF3BA_EFFECT_TYPE_NAMES[];
extern const int DS3613GF3BA_EFFECT_TYPE_COUNT;

// Effect parameter name table (from MainCPU ROM at 0xE324D0, 84 entries)
extern char const *const DS3613GF3BA_EFFECT_PARAM_NAMES[];
extern const int DS3613GF3BA_EFFECT_PARAM_COUNT;

DECLARE_DEVICE_TYPE(DS3613GF3BA, ds3613gf3ba_device)

#endif // MAME_SOUND_DS3613GF3BA_H
