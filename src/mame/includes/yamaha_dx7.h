// license:GPL2+
// copyright-holders:Felipe Sanches
#ifndef MAME_INCLUDES_YAMAHADX7_H
#define MAME_INCLUDES_YAMAHADX7_H

#pragma once
#include "machine/i8255.h"
#include "machine/ram.h"
#include "video/hd44780.h"
#include "emupal.h"
#include "cpu/m6800/m6801.h"
#include "cpu/m6805/m68705.h"


class dx7s_state : public driver_device {
public:
	dx7s_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
                , m_ram(*this, RAM_TAG)
		, m_maincpu(*this, "maincpu")
		, m_subcpu(*this, "subcpu")
                , m_ppi(*this, "ppi")
		, m_lcd(*this, "hd44780")
		, m_rambank(*this, "bankedram")
	{ }

	void init_dx7s();
	void dx7s(machine_config &config);
	void lcd_palette(palette_device &palette) const;

protected:
	virtual void machine_start() override;
	void maincpu_mem_map(address_map &map);

	DECLARE_READ8_MEMBER(maincpu_p5_r);
	DECLARE_WRITE8_MEMBER(maincpu_p5_w);
	DECLARE_WRITE8_MEMBER(i8255_porta_w);
	DECLARE_WRITE8_MEMBER(i8255_portb_w);
	DECLARE_WRITE8_MEMBER(i8255_portc_w);
	DECLARE_WRITE8_MEMBER(subcpu_portc_w);

	required_device<ram_device> m_ram;
	required_device<cpu_device> m_maincpu;
	required_device<m6805_hmos_device> m_subcpu;
	required_device<i8255_device> m_ppi;
	required_device<hd44780_device> m_lcd;
	required_memory_bank m_rambank;
};

#endif // MAME_INCLUDES_YAMAHADX7_H
