// license:GPL2+
// copyright-holders:Felipe Sanches

// KN6000/KN6500 tone generator (IC213).

#ifndef MAME_MATSUSHITA_KN6000_TONEGEN_H
#define MAME_MATSUSHITA_KN6000_TONEGEN_H

#pragma once

#include "kn_tonegen.h"

class kn6000_tonegen_device : public kn_tonegen_base_device
{
public:
	kn6000_tonegen_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
};

DECLARE_DEVICE_TYPE(KN6000_TONEGEN, kn6000_tonegen_device)

#endif // MAME_MATSUSHITA_KN6000_TONEGEN_H
