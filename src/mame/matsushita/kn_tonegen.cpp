// license:GPL-2.0+
// copyright-holders:Felipe Sanches

// Register interface shared by the KN6000 and KN7000 tone generators. The
// synthesis datapath is not emulated, so the stream is silent.

#include "emu.h"
#include "kn_tonegen.h"

kn_tonegen_base_device::kn_tonegen_base_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock, int chips)
	: device_t(mconfig, type, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, m_chips(chips)
	, m_stream(nullptr)
{
}

void kn_tonegen_base_device::device_start()
{
	m_stream = stream_alloc(0, 2, 44100);
	m_regs = std::make_unique<uint16_t []>(m_chips * 0x10000);
	save_pointer(NAME(m_regs), m_chips * 0x10000);
}

void kn_tonegen_base_device::tg_write(int chip, uint16_t addr, uint16_t data)
{
	m_stream->update();
	m_regs[(chip % m_chips) * 0x10000 + addr] = data;
}

void kn_tonegen_base_device::sound_stream_update(sound_stream &stream)
{
	stream.fill(0, 0.0);
	stream.fill(1, 0.0);
}
