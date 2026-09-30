// license:GPL2+
// copyright-holders:Felipe Sanches

// KN7000 tone generators: IC201 (main) and IC205 (sub), 64 voices each.

#include "emu.h"
#include "kn7000_tonegen.h"

DEFINE_DEVICE_TYPE(KN7000_TONEGEN, kn7000_tonegen_device, "kn7000_tonegen", "KN7000 tone generator")

kn7000_tonegen_device::kn7000_tonegen_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: kn_tonegen_base_device(mconfig, KN7000_TONEGEN, tag, owner, clock, 2)
{
}
