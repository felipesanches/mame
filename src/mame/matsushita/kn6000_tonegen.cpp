// license:GPL-2.0+
// copyright-holders:Felipe Sanches

// KN6000/KN6500 tone generator (IC213): one chip, 64 voices.

#include "emu.h"
#include "kn6000_tonegen.h"

DEFINE_DEVICE_TYPE(KN6000_TONEGEN, kn6000_tonegen_device, "kn6000_tonegen", "KN6000 tone generator")

kn6000_tonegen_device::kn6000_tonegen_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: kn_tonegen_base_device(mconfig, KN6000_TONEGEN, tag, owner, clock, 1)
{
}
