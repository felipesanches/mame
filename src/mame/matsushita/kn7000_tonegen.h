// license:GPL-2.0+
// copyright-holders:Felipe Sanches

// KN7000 tone generators (IC201, IC205).

#ifndef MAME_MATSUSHITA_KN7000_TONEGEN_H
#define MAME_MATSUSHITA_KN7000_TONEGEN_H

#pragma once

#include "kn_tonegen.h"

class kn7000_tonegen_device : public kn_tonegen_base_device
{
public:
	kn7000_tonegen_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
};

DECLARE_DEVICE_TYPE(KN7000_TONEGEN, kn7000_tonegen_device)

#endif // MAME_MATSUSHITA_KN7000_TONEGEN_H
