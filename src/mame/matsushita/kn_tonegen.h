// license:GPL2+
// copyright-holders:Felipe Sanches

// Register interface shared by the KN6000 and KN7000 tone generators.

#ifndef MAME_MATSUSHITA_KN_TONEGEN_H
#define MAME_MATSUSHITA_KN_TONEGEN_H

#pragma once

class kn_tonegen_base_device : public device_t, public device_sound_interface
{
public:
	static constexpr feature_type unemulated_features() { return feature::SOUND; }

	// The firmware latches a register address, then writes its data; each chip
	// has its own window.
	void tg_write(int chip, uint16_t addr, uint16_t data);

protected:
	kn_tonegen_base_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock, int chips);

	virtual void device_start() override ATTR_COLD;
	virtual void sound_stream_update(sound_stream &stream) override;

private:
	const int m_chips;
	sound_stream *m_stream;
	std::unique_ptr<uint16_t []> m_regs;
};

#endif // MAME_MATSUSHITA_KN_TONEGEN_H
