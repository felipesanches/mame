// license:BSD-3-Clause
// copyright-holders:R. Belmont, Felipe Sanches
/***************************************************************************

    acvirus.cpp - Access Virus series

    Skeleton driver by R. Belmont

    Hardware in brief:
        Virus A: SAB 80C535-N    (12 MHz), DSP56303 @ 66 MHz
        Virus B: SAB 80C535-N    (12 MHz), DSP56311 @ ??? MHz (illegible on PCB photo I've seen)
        Virus C: SAF 80C515-L24N (24 MHz), DSP56362 @ 120 MHz

        Virus Rack is same h/w as B, Rack XL is the same h/w as C.
        Virus Classic is supposed to be the same h/w as B but not proven.

    The various 80C5xx chips are i8051-based SoCs with additional I/O ports,
    256 bytes of internal RAM like the 8052, and an analog/digital converter.

    The top 4 bits of port P5 select the bank at 0x8000.

    Hardware Notes:
    The DSP has three SRAM chips, probably 128 kbyte each
    for a total of 128 kwords, mapped to address 0x20000. All three DSP
    buses (P, X, Y) point to the same external memory. There's another 128
    kbyte of battery backed SRAM for the 8051.

    The firmware image fits exactly in an AM29F040-120PC flash chip, and is
    bank switched into the 8051 program address space. The lower 0x8000
    bytes of the address space always points to the first 0x8000 bytes of
    flash (except during firmware upgrade, as I assume the programming
    routine has do run from RAM). The upper 0x8000 bytes of the address
    space can point to any 0x8000 sized bank in flash. A bank switch routine
    is at 0x64B8, and will switch to e.g. bank 2 (offset 0x10000) when A =
    0x20. The low nibble is usually zero, but not always, and I don't know
    how it's interpreted.

    Banks 0-2 contain OS code and data, banks 3-6 contain DSP code and data,
    and banks 8-14 seem to contain factory default settings. There are flash
    programming routines at the beginning of banks 7 and 15, and two at the
    end of bank 6. Not sure why there are so many, and not all are
    identical, so there's probably additional bank switching logic to match.
    All display a charming "DO NOT TOUCH ME" message while programming. :)

    The same bank switching also seems to affect external memory, but I'm
    not sure how the smaller SRAM is mapped. Some external memory locations
    are used for other tasks, like communicating with the DSP.

    The initial DSP program and data upload routine is at 0x1FAA. After
    setting up the bus, it churns out all the 24-bit words in banks 3-6
    (except for headers) as one stream. The DSP will interpret the first
    word as a length, the second as address, and the following "length"
    words will be stored at that address in program memory before execution
    starts there. This is just a very short bootstrap program, which takes
    care of receiving the remaining words in chunks. Each chunks starts with
    three words - a command, an address, and optionally length. Commands 0-2
    store data in P, X, or Y memory respectively. Command 3 splits each
    24-bit word into two 12-bit values and store each of them as a 24-bit
    word in Y memory. Command 4 starts execution at the specified address,
    and doesn't have a length.


    FSanches notes:
	- Initial upload of DSP code ends at address 0x4250
	- P1.4 is read at 0x0CE9 and it will only continue if it is 0
	- At 0x4258 we have our first string printed on the LCD
		- via routine 0FCB for the top line
		- and routine 0FE0 prints the bottom line
		
	- P1.7 (clkout) = 0
	- P1.6 (t2) = 1
	- P1.5 (t2ex) is the lcd clock signal
	- P1.1 (cc1) is DB4
	- P1.2 (cc2) is DB5
	- P1.3 (cc3) is DB6
	- P1.4 (int2) is DB7
	- P1.6 (t2) = 0
	
	routine 0d1e writes to display in 4bit mode

	routine 4ec0 animates logo: "access VIRUS b"



	P3.4 and P3.5 select the 4 rows of knobs

	P4 read: status of selected row of buttons
	P4 write: status of selected row of LEDs

	P5.3 is register clock for LEDs
	P5 bits 2, 1 and 0 select the 8 rows of buttons and also LEDs


A/D converter:
	
	The first instruction manipulating A/D registers is reach during boot
	at address 5C37.

***************************************************************************/

#include "emu.h"

#include "debugger.h"
#include "cpu/dsp563xx/dsp56303.h"
#include "cpu/dsp563xx/dsp56311.h"
#include "cpu/dsp563xx/dsp56362.h"
#include "cpu/dsp563xx/dsp56364.h"
#include "cpu/mcs51/sab80c535.h"
#include "machine/intelfsh.h"
#include "video/hd44780.h"

#include "emupal.h"
#include "speaker.h"
#include "screen.h"

#include "virusb.lh"


namespace {

class acvirus_state : public driver_device
{
public:
	acvirus_state(const machine_config &mconfig, device_type type, const char *tag) :
		driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_lcdc(*this, "lcdc"),
		m_dsp(*this, "dsp"),
		m_rombank(*this, "rombank"),
		m_row(*this, "ROW%u", 0U),
		m_scan(0xff)
	{ }

	void virusa(machine_config &config) ATTR_COLD;
	void virusb(machine_config &config) ATTR_COLD;
	void virusc(machine_config &config) ATTR_COLD;

	void init_virus() ATTR_COLD;

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

private:
	required_device<sab80c535_device> m_maincpu;
	required_device<hd44780_device> m_lcdc;
	required_device<dsp563xx_device> m_dsp;
	required_memory_bank m_rombank;
	required_ioport_array<4> m_row;

	void prog_map(address_map &map) ATTR_COLD;
	void data_map(address_map &map) ATTR_COLD;

	u8 p1_r();
	u8 p3_r();
	u8 p4_r();
	u8 p5_r();
	void p1_w(u8 data);
	void p5_w(u8 data);
	void p4_w(u8 data);

	u8 p402_r();

	void palette_init(palette_device &palette) ATTR_COLD;
	
	u8 m_scan;
};


void acvirus_state::machine_start()
{
	m_rombank->configure_entries(0, 16, memregion("maincpu")->base(), 0x8000);
	m_rombank->set_entry(3);
}

void acvirus_state::machine_reset()
{
}

u8 acvirus_state::p1_r()
{
	return ~0x10; // m_lcdc ready?
}

void acvirus_state::p1_w(u8 data)
{
	m_lcdc->db_w(((data << 3) & 0xf0) | 0x08);
	m_lcdc->e_w(BIT(data, 5));
	m_lcdc->rw_w(BIT(data, 6));
	m_lcdc->rs_w(BIT(data, 7));
}

u8 acvirus_state::p3_r()
{
	return 0x00; // dsp ready?
}

u8 acvirus_state::p4_r()
{
	return m_row[m_scan & 3]->read();
}

void acvirus_state::p4_w(u8 data)
{
	// m_LED_pattern = data;
}

void acvirus_state::p5_w(u8 data)
{
	// if raising edge p5.3: set_leds(m_LED_pattern);
	m_scan = data & 7;
	m_rombank->set_entry((data >> 4) & 15);
}

void acvirus_state::prog_map(address_map &map)
{
	map(0x0000, 0x7fff).rom().region("maincpu", 0); // fixed 32K of flash image
	map(0x8000, 0xffff).bankr(m_rombank);
}

void acvirus_state::data_map(address_map &map)
{
	map(0x0400, 0x0407).rw(m_dsp, FUNC(dsp563xx_device::hi08_r), FUNC(dsp563xx_device::hi08_w));
	map(0x4000, 0x7fff).ram();
}

void acvirus_state::palette_init(palette_device &palette)
{
	palette.set_pen_color(0, rgb_t(142, 241, 0));
	palette.set_pen_color(1, rgb_t(0, 48, 0));
}

void acvirus_state::virusa(machine_config &config)
{
	SAB80C535(config, m_maincpu, XTAL(12'000'000));
	m_maincpu->set_addrmap(AS_PROGRAM, &acvirus_state::prog_map);
	m_maincpu->set_addrmap(AS_DATA,    &acvirus_state::data_map);
	m_maincpu->port_in_cb<1>().set(FUNC(acvirus_state::p1_r));
	m_maincpu->port_out_cb<1>().set(FUNC(acvirus_state::p1_w));
	m_maincpu->port_out_cb<5>().set(FUNC(acvirus_state::p5_w));

	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
	screen.set_refresh_hz(60);
	screen.set_screen_update("lcdc", FUNC(hd44780_device::screen_update));
	screen.set_size(6*16, 8*2+1);
	screen.set_visarea_full();
	screen.set_palette("palette");

	PALETTE(config, "palette", FUNC(acvirus_state::palette_init), 2);

	/* Actual device is LM16255 */
	HD44780(config, m_lcdc, 270000); // TODO: clock not measured, datasheet typical clock used
	m_lcdc->set_lcd_size(2, 16);

	DSP56303(config, m_dsp, 66_MHz_XTAL);
	m_dsp->set_hard_omr(0xe);

	SPEAKER(config, "speaker", 2).front();
}

void acvirus_state::virusb(machine_config &config)
{
	SAB80C535(config, m_maincpu, XTAL(12'000'000));
	m_maincpu->set_addrmap(AS_PROGRAM, &acvirus_state::prog_map);
	m_maincpu->set_addrmap(AS_DATA,    &acvirus_state::data_map);
	m_maincpu->port_in_cb<1>().set(FUNC(acvirus_state::p1_r));
	m_maincpu->port_out_cb<1>().set(FUNC(acvirus_state::p1_w));
	m_maincpu->port_in_cb<3>().set(FUNC(acvirus_state::p3_r));
	m_maincpu->port_in_cb<4>().set(FUNC(acvirus_state::p4_r));
	m_maincpu->port_out_cb<4>().set(FUNC(acvirus_state::p4_w));
	m_maincpu->port_out_cb<5>().set(FUNC(acvirus_state::p5_w));

	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
	screen.set_refresh_hz(60);
	screen.set_screen_update("lcdc", FUNC(hd44780_device::screen_update));
	screen.set_size(6*16, 8*2+1);
	screen.set_visarea_full();
	screen.set_palette("palette");

	PALETTE(config, "palette", FUNC(acvirus_state::palette_init), 2);

	/* Actual device is LM16255 */
	HD44780(config, m_lcdc, 270000); // TODO: clock not measured, datasheet typical clock used
	m_lcdc->set_lcd_size(2, 16);

	DSP56311(config, m_dsp, 1_MHz_XTAL);
	m_dsp->set_hard_omr(0xe);

	SPEAKER(config, "speaker", 2).front();

	config.set_default_layout(layout_virusb);
}

void acvirus_state::virusc(machine_config &config)
{
	SAB80C535(config, m_maincpu, XTAL(24'000'000)); // 515 really
	m_maincpu->set_addrmap(AS_PROGRAM, &acvirus_state::prog_map);
	m_maincpu->set_addrmap(AS_DATA,    &acvirus_state::data_map);
	m_maincpu->port_in_cb<1>().set(FUNC(acvirus_state::p1_r));
	m_maincpu->port_out_cb<1>().set(FUNC(acvirus_state::p1_w));
	m_maincpu->port_out_cb<5>().set(FUNC(acvirus_state::p5_w));

	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
	screen.set_refresh_hz(60);
	screen.set_screen_update("lcdc", FUNC(hd44780_device::screen_update));
	screen.set_size(6*16, 8*2+1);
	screen.set_visarea_full();
	screen.set_palette("palette");

	PALETTE(config, "palette", FUNC(acvirus_state::palette_init), 2);

	/* Actual device is LM16255 */
	HD44780(config, m_lcdc, 270000); // TODO: clock not measured, datasheet typical clock used
	m_lcdc->set_lcd_size(2, 16);

	DSP56362(config, m_dsp, 136_MHz_XTAL);
	m_dsp->set_hard_omr(0xe);

	SPEAKER(config, "speaker", 2).front();
}

static INPUT_PORTS_START( virus )
	PORT_START("ROW0")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_1)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_2)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_3)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_4)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_5)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_6)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_7)
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_8)

	PORT_START("ROW1")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_Q)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_W)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_E)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_R)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_T)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_Y)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_U)
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_I)

	PORT_START("ROW2")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_A)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_S)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_D)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_F)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_G)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_H)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_J)
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_K)

	PORT_START("ROW3")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_Z)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_X)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_C)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_V)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_B)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_N)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_M)
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_CODE(KEYCODE_COMMA) PORT_CODE(KEYCODE_ENTER)
INPUT_PORTS_END

ROM_START( virusa )
	ROM_REGION(0x80000, "maincpu", 0)
	ROM_LOAD( "virus_a_28.bin", 0x000000, 0x080000, CRC(087cd808) SHA1(fe3310a165c208473822455c75ee5b2a6de34bc8) )
ROM_END

ROM_START( virusb )
	ROM_REGION(0x80000, "maincpu", 0)
	ROM_LOAD( "virus_bt_490x049.bin", 0x000000, 0x080000, CRC(4ffc928a) SHA1(ee4b83e2eb1f01c73e37e2ff1d2edd653a0dcf5b) )
ROM_END

ROM_START( virusc )
	ROM_REGION(0x80000, "maincpu", 0)
	ROM_LOAD( "virus_c_650x352.bin", 0x000000, 0x080000, CRC(d44a9468) SHA1(fad9b896b39a43a1d46acb1d780b78b775a609b8) )
ROM_END

ROM_START( virusrck )
	ROM_REGION(0x80000, "maincpu", 0)
	ROM_LOAD( "virus_rt_210x071.bin", 0x000000, 0x080000, CRC(62b2bcc1) SHA1(241467bcb563736472a6e61f6c9c532590664500) )
ROM_END

ROM_START( virusrckxl )
	ROM_REGION(0x80000, "maincpu", 0)
	ROM_LOAD( "virus_xl_650x079.bin", 0x000000, 0x080000, CRC(d0721c46) SHA1(b7c292b66ba3690a4a50592e17321b9c4147621d) )
ROM_END

ROM_START( viruscl )
	ROM_REGION(0x80000, "maincpu", 0)
	ROM_LOAD( "virus_cl_061_release.bin", 0x000000, 0x080000, CRC(a202e443) SHA1(33d5f4ebbacc817ab1e5dd572e8dc755f6c5e253) )
ROM_END

} // anonymous namespace


SYST( 1997, virusa,     0, 0, virusa, virus, acvirus_state, empty_init, "Access", "Virus A", MACHINE_NOT_WORKING|MACHINE_NO_SOUND )
SYST( 1999, virusb,     0, 0, virusb, virus, acvirus_state, empty_init, "Access", "Virus B (Ver. T)", MACHINE_NOT_WORKING|MACHINE_NO_SOUND )
SYST( 2002, virusc,     0, 0, virusc, virus, acvirus_state, empty_init, "Access", "Virus C", MACHINE_NOT_WORKING|MACHINE_NO_SOUND )
SYST( 2001, virusrck,   0, 0, virusb, virus, acvirus_state, empty_init, "Access", "Virus Rack (Ver. T)", MACHINE_NOT_WORKING|MACHINE_NO_SOUND )
SYST( 2002, virusrckxl, 0, 0, virusc, virus, acvirus_state, empty_init, "Access", "Virus Rack XL", MACHINE_NOT_WORKING|MACHINE_NO_SOUND )
SYST( 2004, viruscl,    0, 0, virusb, virus, acvirus_state, empty_init, "Access", "Virus Classic", MACHINE_NOT_WORKING|MACHINE_NO_SOUND )
