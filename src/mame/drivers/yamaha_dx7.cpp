// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/*
	Yamaha DX7
*/

#include "emu.h"
#include "includes/yamaha_dx7.h"
#include "screen.h"

/*
    driver init function
*/
void dx7s_state::init_dx7s()
{
}

void dx7s_state::machine_start(){
	m_rambank->set_base(m_ram->pointer());
}

static INPUT_PORTS_START( dx7s )
	PORT_START("FOOTPEDALS")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("FOOT_SWITCH_1")  PORT_CODE(KEYCODE_1)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("FOOT_SWITCH_2")  PORT_CODE(KEYCODE_2)
INPUT_PORTS_END

void dx7s_state::maincpu_mem_map(address_map &map)
{
	map(0x0000, 0x0014).noprw(); // Internal CPU registers
	map(0x0015, 0x0015).rw(FUNC(dx7s_state::maincpu_p5_r), FUNC(dx7s_state::maincpu_p5_w));
	map(0x0016, 0x001f).noprw(); // Internal CPU registers
	map(0x0028, 0x002b).w(m_ppi, FUNC(i8255_device::write));
	map(0x002c, 0x002f).nopw(); // ATT 	40H174 "6bit Volume control"
	map(0x0030, 0x0030).mirror(0x0002).rw(m_lcd, FUNC(hd44780_device::control_r), FUNC(hd44780_device::control_w));
	map(0x0031, 0x0031).mirror(0x0002).rw(m_lcd, FUNC(hd44780_device::data_r), FUNC(hd44780_device::data_w));
	map(0x0034, 0x0035).mirror(0x0002).noprw(); // "YM2604" OPS2
	map(0x0040, 0x013f).ram(); // Internal RAM
	map(0x1000, 0x1fff).mask(0x1f).noprw(); // "YM3609" Envelope Generator
	map(0x2000, 0x3fff).bankrw("bankedram");
	map(0x4000, 0x7fff).mask(0x1fff).noprw(); // ROM CARTRIDGE
	map(0x8000, 0xffff).rom();
}

READ8_MEMBER( dx7s_state::maincpu_p5_r )
{
	return (ioport("FOOTPEDALS")->read() & 0x03) | 0xfc;
}

WRITE8_MEMBER( dx7s_state::maincpu_p5_w )
{
	m_rambank->set_base(m_ram->pointer() + (BIT(data, 6) ? 0 : 0x2000));
}

WRITE8_MEMBER( dx7s_state::i8255_porta_w )
{
	/* 7-SEG LEDs 1 */
}

WRITE8_MEMBER( dx7s_state::i8255_portb_w )
{
	/* 7-SEG LEDs 2 */
}

WRITE8_MEMBER( dx7s_state::subcpu_portc_w )
{
	
}

WRITE8_MEMBER( dx7s_state::i8255_portc_w )
{
	/* PC5 - PERF
           PC4 - BANK
           PC3 - PBM
           PC2 - KSF
           PC1 - VOICE
           PC0 - CRT
	*/
}

void dx7s_state::lcd_palette(palette_device &palette) const
{
	// FIXME: colors
	palette.set_pen_color(0, rgb_t(138, 146, 148)); // background
	palette.set_pen_color(1, rgb_t( 92,  83,  88)); // lcd pixel on
	palette.set_pen_color(2, rgb_t(131, 136, 139)); // lcd pixel off
}

void dx7s_state::dx7s(machine_config &config)
{
	/* basic machine hardware */
	HD6303Y(config, m_maincpu, XTAL(8'000'000)); // Service manual lists HD63B03YP
	HD6805U1(config, m_subcpu, XTAL(8'000'000)/2); // Service manual lists HD6805S1A33P
	m_subcpu->portc_w().set(FUNC(dx7s_state::subcpu_portc_w));

	m_maincpu->set_addrmap(AS_PROGRAM, &dx7s_state::maincpu_mem_map);

	RAM(config, m_ram).set_default_size("16K");

	I8255A(config, m_ppi, 0);
	m_ppi->out_pa_callback().set(FUNC(dx7s_state::i8255_porta_w));
	m_ppi->out_pb_callback().set(FUNC(dx7s_state::i8255_portb_w));
	m_ppi->out_pc_callback().set(FUNC(dx7s_state::i8255_portc_w));

	/* video hardware */
	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
	screen.set_screen_update("hd44780", FUNC(hd44780_device::screen_update));
	screen.set_refresh_hz(60); //arbitrary
	hd44780_device &hd44780(HD44780(config, "hd44780", 0));
	hd44780.set_lcd_size(2, 16);
	screen.set_palette("palette");

	PALETTE(config, "palette", FUNC(dx7s_state::lcd_palette), 3);

}

ROM_START( ymdx7s )
	ROM_REGION( 0x10000, "maincpu", 0 )
	ROM_LOAD( "dx7s-v1-3-27c256.bin", 0x8000, 0x8000, CRC(f1fa77f9) SHA1(cdc5cde5dea03e645b2f6a8d3935d539f30e2423) )
	ROM_REGION( 0x10000, "subcpu", 0 )
ROM_END

//    YEAR  NAME    PARENT  COMPAT  MACHINE INPUT CLASS       INIT       COMPANY   FULLNAME FLAGS
COMP( 1987, ymdx7s, 0,      0,      dx7s,   dx7s, dx7s_state, init_dx7s, "Yamaha", "DX7s" , MACHINE_NO_SOUND_HW | MACHINE_NOT_WORKING )
