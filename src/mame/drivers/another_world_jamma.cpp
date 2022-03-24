// license:GPL-2.0+
// copyright-holders:Felipe Sanches, Gregory Montoir, Fabien Sanglard
/*
    Another World game (JAMMA board driver)

    The purpose of this driver is to prototype a board that I may someday
    actually build. The firmware to run on the CPUs is being developed
    experimentall at:
    https://github.com/felipesanches/AnotherWorld_JAMMA/tree/master/firmware

    Author: Felipe Sanches <juca@members.fsf.org>

    Heavily based on Fabien Sanglard's Another World Bytecode Interpreter:
    https://github.com/fabiensanglard/Another-World-Bytecode-Interpreter
*/

#include "emu.h"
#include "includes/anotherworld_jamma.h"
#include "cpu/z80/z80.h"
#include "screen.h"
#include "speaker.h"

void awjamma_state::machine_reset()
{
	use_video_2 = false; //TODO: review this.
}


void awjamma_state::machine_start()
{
	// Allocate 256 VRAM banks (200 of which are active display "line banks")
	m_active_vram = std::make_unique<uint8_t[]>(256 * 0x200);

	// Allocate 4 * 256 VRAM banks (which are used as 4 "work" pages for video-buffering)
	m_work_vram = std::make_unique<uint8_t[]>(4 * 256 * 0x200);

	membank("active_videopage")->configure_entries(0, 256, m_active_vram.get(), 0x200);
	membank("work_videopage")->configure_entries(0, 4 * 256, m_work_vram.get(), 0x200);
	membank("screens_rombank")->configure_entries(0, 16 * 256, memregion("screens")->base(), 0x200);

	bytecode_base = memregion("bytecode")->base();
	chargen_base = memregion("chargen")->base();
	strings_base = memregion("strings")->base();
	polygon_cinematic_base = memregion("cinematic")->base();
	polygon_video2_base = memregion("video2")->base();
}


static INPUT_PORTS_START( awjamma )
	PORT_START("buttons")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Arrow down") PORT_CODE(KEYCODE_DOWN)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Arrow up") PORT_CODE(KEYCODE_UP)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Arrow right") PORT_CODE(KEYCODE_RIGHT)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Arrow left") PORT_CODE(KEYCODE_LEFT)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Code") PORT_CODE(KEYCODE_C)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Action") PORT_CODE(KEYCODE_LCONTROL)

	PORT_START("keyboard")
	PORT_BIT(0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("B") PORT_CODE(KEYCODE_B)
	PORT_BIT(0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("C") PORT_CODE(KEYCODE_C)
	PORT_BIT(0x0004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("D") PORT_CODE(KEYCODE_D)
	PORT_BIT(0x0008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F") PORT_CODE(KEYCODE_F)
	PORT_BIT(0x0010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("G") PORT_CODE(KEYCODE_G)
	PORT_BIT(0x0020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("H") PORT_CODE(KEYCODE_H)
	PORT_BIT(0x0040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("J") PORT_CODE(KEYCODE_J)
	PORT_BIT(0x0080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("K") PORT_CODE(KEYCODE_K)
	PORT_BIT(0x0100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("L") PORT_CODE(KEYCODE_L)
	PORT_BIT(0x0200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R") PORT_CODE(KEYCODE_R)
	PORT_BIT(0x0400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("T") PORT_CODE(KEYCODE_T)
	PORT_BIT(0x0800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("X") PORT_CODE(KEYCODE_X)
	PORT_BIT(0x1000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("ENTER") PORT_CODE(KEYCODE_ENTER)
INPUT_PORTS_END


void awjamma_state::maincpu_prog_map(address_map &map)
{
	map(0x0000, 0x7fff).rom();
	map(0x8000, 0x8001).w(FUNC(awjamma_state::set_instruction_pointer));
	map(0x8002, 0x8002).r(FUNC(awjamma_state::fetch_bytecode_byte));
	map(0x8003, 0x8003).portr("buttons");
	map(0x8004, 0x8005).portr("keyboard"); /* TODO: review this! */
	map(0x8006, 0x8006).w(FUNC(awjamma_state::switch_level_bank));
	map(0x8007, 0x8007).w("soundlatch", FUNC(generic_latch_8_device::write));
	map(0x8008, 0x8008).w("videolatch", FUNC(generic_latch_8_device::write));
	map(0x8009, 0x8009).r("musicmarklatch", FUNC(generic_latch_8_device::read)); /* shouldn't it be a 16 bit value ?! */
	map(0x800a, 0x800a).w(FUNC(awjamma_state::changePalette));
	map(0x800b, 0x800b).w("param1latch", FUNC(generic_latch_8_device::write));
	map(0x800c, 0x800c).w("param2latch", FUNC(generic_latch_8_device::write));
	map(0x800d, 0x800d).w("param3latch", FUNC(generic_latch_8_device::write));
	map(0x800e, 0x800e).w("param4latch", FUNC(generic_latch_8_device::write));
	map(0x800f, 0x800f).w("param5latch", FUNC(generic_latch_8_device::write));
	map(0x8010, 0x8010).r("videocpu_status_latch", FUNC(generic_latch_8_device::read));
	map(0x8011, 0x8011).r("soundcpu_status_latch", FUNC(generic_latch_8_device::read));
	map(0x9000, 0x91ff).ram(); /* VM Variables */
	map(0xc000, 0xffff).ram(); /* 16kb SRAM */
}


void awjamma_state::soundcpu_prog_map(address_map &map)
{
	map(0x0000, 0x7fff).rom();
	map(0x8000, 0x8000).r("soundlatch", FUNC(generic_latch_8_device::read));
	map(0x8001, 0x8001).r("param1latch", FUNC(generic_latch_8_device::read));
	map(0x8002, 0x8002).r("param2latch", FUNC(generic_latch_8_device::read));
	map(0x8003, 0x8003).r("param3latch", FUNC(generic_latch_8_device::read));
	map(0x8004, 0x8004).r("param4latch", FUNC(generic_latch_8_device::read));
	map(0x8005, 0x8005).w("musicmarklatch", FUNC(generic_latch_8_device::write)); /* shouldn't it be a 16 bit value ?! */
	map(0x8006, 0x8006).w("soundcpu_status_latch", FUNC(generic_latch_8_device::write));
	map(0xc000, 0xffff).ram(); /* 16kb SRAM */
}


void awjamma_state::videocpu_prog_map(address_map &map)
{
	map(0x0000, 0x7fff).rom();
	map(0x8000, 0x8000).r("videolatch", FUNC(generic_latch_8_device::read));
	map(0x8001, 0x8001).r("param1latch", FUNC(generic_latch_8_device::read));
	map(0x8002, 0x8002).r("param2latch", FUNC(generic_latch_8_device::read));
	map(0x8003, 0x8003).r("param3latch", FUNC(generic_latch_8_device::read));
	map(0x8004, 0x8004).r("param4latch", FUNC(generic_latch_8_device::read));
	map(0x8005, 0x8005).r("param5latch", FUNC(generic_latch_8_device::read));
	map(0x8006, 0x8006).w(FUNC(awjamma_state::switch_work_videopage_bank));
	map(0x8007, 0x8007).w(FUNC(awjamma_state::set_screen_selector));
	map(0x8008, 0x8008).w(FUNC(awjamma_state::select_active_videopage_y));
	map(0x8009, 0x8009).w(FUNC(awjamma_state::select_work_videopage_y));
	map(0x800a, 0x800a).w(FUNC(awjamma_state::select_screens_y));
	map(0x800b, 0x800b).w("videocpu_status_latch", FUNC(generic_latch_8_device::write));
	map(0x800c, 0x800d).w(FUNC(awjamma_state::set_polygon_data_offset));
	map(0x800e, 0x800e).r(FUNC(awjamma_state::fetch_polygon_data));
	map(0x800f, 0x800f).w(FUNC(awjamma_state::select_polygon_data_source));
	map(0x9000, 0x91ff).bankrw("active_videopage");
	map(0x9200, 0x93ff).bankrw("work_videopage");
	map(0x9400, 0x95ff).bankr("screens_rombank");
	map(0xa000, 0xa7ff).r(FUNC(awjamma_state::chargen_r));
	map(0xa800, 0xbfff).r(FUNC(awjamma_state::strings_r));
	map(0xc000, 0xffff).ram(); /* 16kb SRAM */
}


uint8_t awjamma_state::chargen_r(offs_t offset)
{
	return chargen_base[offset];
}


uint8_t awjamma_state::strings_r(offs_t offset)
{
	return strings_base[offset];
}


uint8_t awjamma_state::fetch_polygon_data(offs_t offset)
{
	if (use_video_2)
		return polygon_video2_base[polygon_data_offset++];
	else
		return polygon_cinematic_base[(level_bank & 0x0F) * 0x10000 + polygon_data_offset++];
}


void awjamma_state::set_polygon_data_offset(offs_t offset, uint8_t data)
{
	if (offset % 2 == 0)
		polygon_data_offset = (polygon_data_offset & 0xFF00) | data;
	else
		polygon_data_offset = (polygon_data_offset & 0x00FF) | (data << 8);
}


uint8_t awjamma_state::fetch_bytecode_byte(offs_t offset)
{
	return bytecode_base[(level_bank & 0x0F) * 0x10000 + instruction_pointer++];
}


void awjamma_state::set_instruction_pointer(offs_t offset, uint8_t data)
{
	if (offset % 2 == 0)
		instruction_pointer = (instruction_pointer & 0xFF00) | data;
	else
		instruction_pointer = (instruction_pointer & 0x00FF) | (data << 8);
}


void awjamma_state::select_polygon_data_source(uint8_t data)
{
	//0x00: CINEMATIC
	//0x01: VIDEO2
	use_video_2 = (data == 0x01);
}


void awjamma_state::switch_level_bank(uint8_t data)
{
	level_bank = data & 0x0F;
}


void awjamma_state::select_active_videopage_y(uint8_t data)
{
	active_videopage_y = data;
	membank("active_videopage")->set_entry(active_videopage_y);
}


void awjamma_state::select_work_videopage_y(uint8_t data)
{
	work_videopage_y = data;
	membank("work_videopage")->set_entry(256 * work_videopage_bank + work_videopage_y);
}


void awjamma_state::select_screens_y(uint8_t data)
{
	screens_y = data;
	membank("screens_rombank")->set_entry(256 * screens_bank + screens_y);
}


void awjamma_state::switch_work_videopage_bank(uint8_t data)
{
	work_videopage_bank = data & 0x03;
	membank("work_videopage")->set_entry(256 * work_videopage_bank + work_videopage_y);
}


void awjamma_state::set_screen_selector(uint8_t data)
{
	screens_bank = data & 0x0F;
	membank("screens_rombank")->set_entry(256 * screens_bank + screens_y);
}


void awjamma_state::changePalette(uint8_t data)
{
	set_palette(data & 0x3F);
}


uint32_t awjamma_state::screen_update_aw(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	for (uint8_t y=0; y<200; y++)
	{
		for (uint16_t x=0; x<320; x++)
		{
			bitmap.pix(y, x) = m_work_vram[512*y + x];
		}
	}
	return 0;
}


void awjamma_state::awjamma(machine_config &config)
{
	/* basic machine hardware */
	Z80(config, m_maincpu, 16000000);
	m_maincpu->set_addrmap(AS_PROGRAM, &awjamma_state::maincpu_prog_map);

	Z80(config, m_soundcpu, 16000000);
	m_soundcpu->set_addrmap(AS_PROGRAM, &awjamma_state::soundcpu_prog_map);

	Z80(config, m_videocpu, 16000000);
	m_videocpu->set_addrmap(AS_PROGRAM, &awjamma_state::videocpu_prog_map);

	GENERIC_LATCH_8(config, "soundlatch");
	GENERIC_LATCH_8(config, "videolatch");
	GENERIC_LATCH_8(config, "param1latch");
	GENERIC_LATCH_8(config, "param2latch");
	GENERIC_LATCH_8(config, "param3latch");
	GENERIC_LATCH_8(config, "param4latch");
	GENERIC_LATCH_8(config, "param5latch");
	GENERIC_LATCH_8(config, "musicmarklatch");
	GENERIC_LATCH_8(config, "videocpu_status_latch");
	GENERIC_LATCH_8(config, "soundcpu_status_latch");

	/* video hardware */
    	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_RASTER));
	screen.set_screen_update(FUNC(awjamma_state::screen_update_aw));
	screen.set_refresh_hz(60);
	screen.set_vblank_time(ATTOSECONDS_IN_USEC(0));
	screen.set_size(320, 200);
	screen.set_visarea(0, 319, 0, 199);
	screen.set_palette(m_palette);
	PALETTE(config, m_palette, FUNC(awjamma_state::init_palette), 16);


	/* sound hardware */
	SPEAKER(config, "lspeaker").front_left();
	SPEAKER(config, "rspeaker").front_right();
	PAULA_8364(config, m_paula, 28375160 / 8); //PAL
	m_paula->add_route(0, "lspeaker", 0.50);
	m_paula->add_route(1, "rspeaker", 0.50);
	m_paula->add_route(2, "rspeaker", 0.50);
	m_paula->add_route(3, "lspeaker", 0.50);
	//m_paula->mem_read_cb().set(FUNC(awjamma_state::chip_ram_r));
	//m_paula->int_cb().set(FUNC(awjamma_state::paula_int_w));
}


void awjamma_state::video_start()
{
}


void awjamma_state::init_palette(palette_device &palette)
{
	//FIXME!
	//changePalette(0);
}


#define NUM_COLORS 16
void awjamma_state::set_palette(uint8_t paletteId){
	const uint8_t *colors = (const uint8_t *) (memregion("palettes")->base() + level_bank * 0x10000);
	uint8_t r, g, b;

	for (int i = 0; i < NUM_COLORS; ++i)
	{
		uint8_t c1 = colors[paletteId * 2*NUM_COLORS + 2*i];
		uint8_t c2 = colors[paletteId * 2*NUM_COLORS + 2*i + 1];
		r = ((c1 & 0x0F) << 2) | ((c1 & 0x0F) >> 2);
		g = ((c2 & 0xF0) >> 2) | ((c2 & 0xF0) >> 6);
		b = ((c2 & 0x0F) >> 2) | ((c2 & 0x0F) << 2);
		m_palette->set_pen_color(i, pal6bit(r), pal6bit(g), pal6bit(b));
	}
}


#define WORK_IN_PROGRESS (BAD_DUMP CRC(e2df8c47) SHA1(b79b41835aa2d5747932f8080bb6fb2cf32837d7))
ROM_START( awjamma )
	ROM_REGION( 0x8000, "maincpu", ROMREGION_ERASEFF )
	ROM_LOAD( "anotherworld_maincpu.rom", 0x0000, 0x8000, WORK_IN_PROGRESS )

	ROM_REGION( 0x8000, "soundcpu", ROMREGION_ERASEFF )
	ROM_LOAD( "anotherworld_soundcpu.rom", 0x0000, 0x8000, NO_DUMP )

	ROM_REGION( 0x8000, "videocpu", ROMREGION_ERASEFF )
	ROM_LOAD( "anotherworld_videocpu.rom", 0x0000, 0x8000, WORK_IN_PROGRESS )

	ROM_REGION( 0x100000, "bytecode", ROMREGION_ERASEFF ) /* MS-DOS: Bytecode */
	ROM_LOAD( "bytecode.rom", 0x00000, 0x90000, CRC(1fe0f8b5) SHA1(8a4650f742b4462806adaa31f358b0377a819135) )

	ROM_REGION( 0x8000, "palettes", ROMREGION_ERASEFF ) /* MS-DOS: Palette */
	ROM_LOAD( "palettes.rom", 0x0000, 0x4800, CRC(87e879b8) SHA1(dc40fb30a1a982365887059bc0768c27c55f1418) )

	ROM_REGION( 0x100000, "cinematic", ROMREGION_ERASEFF ) /* MS-DOS: Cinematic */
	ROM_LOAD( "cinematic.rom", 0x00000, 0x90000, CRC(598363a6) SHA1(c5b004e2a213f7fb476671f17ef2d121da75ad5b) )

	ROM_REGION( 0x8000, "video2", ROMREGION_ERASEFF ) /* MS-DOS: Video2 */
	ROM_LOAD( "video2.rom", 0x0000, 0x8000, CRC(cf791ac9) SHA1(d3bb38a60d7a13454eec1cb0a1e399c9ba1f2e13) )

	ROM_REGION( 0x200000, "screens", ROMREGION_ERASEFF ) /* MS-DOS: Screens */
	ROM_LOAD( "screens.rom", 0x00000, 0x160000, CRC(af8aefe2) SHA1(dfa75c96c4e165baf3f718577854cc24fef0ad9d) )

	ROM_REGION( 0x0800, "chargen", ROMREGION_ERASEFF)
	ROM_LOAD( "anotherworld_chargen.rom", 0x0000, 0x0300, CRC(e2df8c47) SHA1(b79b41835aa2d5747932f8080bb6fb2cf32837d7) )

	ROM_REGION( 0x1800, "strings", 0)
	ROM_LOAD( "str_data.rom",  0x0000, 0x1000, CRC(9fd4058d) SHA1(7c6ccef96de2cf2bc89db71bc9fba598f74fb782) )
	ROM_LOAD( "str_index.rom", 0x1000, 0x0800, CRC(8035eeed) SHA1(12fa40e963e6f2e04c9789fe0d67fa3495dcf113) )

	ROM_REGION( 0x640000, "samples", ROMREGION_ERASEFF ) /* MS-DOS: Sound samples and music data */
	ROM_LOAD( "samples.rom", 0x000000, 0x640000, NO_DUMP )
ROM_END

/*    YEAR  NAME     PARENT    COMPAT  MACHINE  INPUT    INIT                       COMPANY     FULLNAME */
COMP( 2017, awjamma, 0,        0,      awjamma, awjamma, awjamma_state, empty_init, "FSanches", "Another World JAMMA" , MACHINE_NOT_WORKING)
