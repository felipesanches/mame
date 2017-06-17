// license:GPL-2.0+
// copyright-holders:Felipe Sanches, Gregory Montoir, Fabien Sanglard
/*
    Another World game (JAMMA board driver)

    Author: Felipe Sanches <juca@members.fsf.org>

    Heavily based on Fabien Sanglard's Another World Bytecode Interpreter:
    https://github.com/fabiensanglard/Another-World-Bytecode-Interpreter
*/

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200

//Try this for high-res polygon rendering:
//#define SCREEN_WIDTH (320*4)
//#define SCREEN_HEIGHT (200*4)

//#define SCREEN_WIDTH 256
//#define SCREEN_HEIGHT 224
//
// This other resolution (256x224) was used in some ports (I guess SEGA Genesis used it)
// It is here for the purpose of testing a generic renderer code that would work
// in any given resolution. But the initial port of the graphics code was based on
// Fabien Sanglard's free-software VM re-implementation, which used
// a screen resolution of 320x200 pixels.

#include "emu.h"
#include "includes/anotherworld.h"
#include "cpu/z80/z80.h"
#include "screen.h"
#include "speaker.h"

/*
    driver init function
*/
DRIVER_INIT_MEMBER(another_world_state, another_world)
{
    uint8_t *RAM = memregion("maincpu")->base();
    membank("active_videopage")->configure_entries(0, 256, &RAM[0x9000], 0x200);
    membank("work_videopage")->configure_entries(0, 4*256, &RAM[0x9200], 0x200);
}

void another_world_state::machine_start(){
}

static INPUT_PORTS_START( another_world )
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

static ADDRESS_MAP_START( maincpu_prog_map, AS_PROGRAM, 8, another_world_state )
    AM_RANGE(0x0000, 0x7fff) AM_ROM
    AM_RANGE(0x8000, 0x8001) AM_WRITE(set_instruction_pointer)
    AM_RANGE(0x8002, 0x8002) AM_READ(fetch_byte)
    AM_RANGE(0x8003, 0x8003) AM_READ_PORT("buttons")
    AM_RANGE(0x8004, 0x8004) AM_READ_PORT("keyboard")
    AM_RANGE(0x8005, 0x8005) AM_WRITE(switch_level_bank)
    AM_RANGE(0x8006, 0x8006) AM_DEVWRITE("soundlatch", generic_latch_8_device, write)
    AM_RANGE(0x8007, 0x8007) AM_DEVWRITE("videolatch", generic_latch_8_device, write)
    AM_RANGE(0x8008, 0x8008) AM_DEVREAD("musicmarklatch", generic_latch_8_device, read) /* shouldn't it be a 16 bit value ?! */
    AM_RANGE(0x8009, 0x8009) AM_WRITE(changePalette)
    AM_RANGE(0x800a, 0x800a) AM_DEVWRITE("param1latch", generic_latch_8_device, write)
    AM_RANGE(0x800b, 0x800b) AM_DEVWRITE("param2latch", generic_latch_8_device, write)
    AM_RANGE(0x800c, 0x800c) AM_DEVWRITE("param3latch", generic_latch_8_device, write)
    AM_RANGE(0x800d, 0x800d) AM_DEVWRITE("param4latch", generic_latch_8_device, write)
    AM_RANGE(0x9000, 0x91ff) AM_RAM /* VM Variables */
    AM_RANGE(0xc000, 0xffff) AM_RAM /* 16kb SRAM */
ADDRESS_MAP_END

static ADDRESS_MAP_START( soundcpu_prog_map, AS_PROGRAM, 8, another_world_state )
    AM_RANGE(0x0000, 0x7fff) AM_ROM
    AM_RANGE(0x8000, 0x8000) AM_DEVREAD("soundlatch", generic_latch_8_device, read)
    AM_RANGE(0x8001, 0x8001) AM_DEVREAD("param1latch", generic_latch_8_device, read)
    AM_RANGE(0x8002, 0x8002) AM_DEVREAD("param2latch", generic_latch_8_device, read)
    AM_RANGE(0x8003, 0x8003) AM_DEVREAD("param3latch", generic_latch_8_device, read)
    AM_RANGE(0x8004, 0x8004) AM_DEVREAD("param4latch", generic_latch_8_device, read)
    AM_RANGE(0x8005, 0x8005) AM_DEVWRITE("musicmarklatch", generic_latch_8_device, write) /* shouldn't it be a 16 bit value ?! */
    AM_RANGE(0xc000, 0xffff) AM_RAM /* 16kb SRAM */
ADDRESS_MAP_END

static ADDRESS_MAP_START( videocpu_prog_map, AS_PROGRAM, 8, another_world_state )
    AM_RANGE(0x0000, 0x7fff) AM_ROM
    AM_RANGE(0x8000, 0x8000) AM_DEVREAD("videolatch", generic_latch_8_device, read)
    AM_RANGE(0x8001, 0x8001) AM_DEVREAD("param1latch", generic_latch_8_device, read)
    AM_RANGE(0x8002, 0x8002) AM_DEVREAD("param2latch", generic_latch_8_device, read)
    AM_RANGE(0x8003, 0x8003) AM_DEVREAD("param3latch", generic_latch_8_device, read)
    AM_RANGE(0x8004, 0x8004) AM_DEVREAD("param4latch", generic_latch_8_device, read)
    AM_RANGE(0x8005, 0x8005) AM_WRITE(switch_work_videopage_bank)
    AM_RANGE(0x8006, 0x8006) AM_WRITE(select_active_videopage_y)
    AM_RANGE(0x8007, 0x8007) AM_WRITE(select_work_videopage_y)
    AM_RANGE(0x9000, 0x91ff) AM_RAMBANK("active_videopage")
    AM_RANGE(0x9200, 0x93ff) AM_RAMBANK("work_videopage")
    AM_RANGE(0xc000, 0xffff) AM_RAM /* 16kb SRAM */
ADDRESS_MAP_END

READ8_MEMBER(another_world_state::fetch_byte)
{
    printf("bank: %04X ip: %04X\n", level_bank, instruction_pointer);
    return memregion("bytecode")->base()[((level_bank & 0x0F) << 16) | instruction_pointer++];
}

WRITE8_MEMBER(another_world_state::set_instruction_pointer)
{
    if (offset % 2 == 0){
      instruction_pointer = (instruction_pointer & 0xFF00) | data;
    } else {
      instruction_pointer = (instruction_pointer & 0x00FF) | (data << 8);
    }
}

WRITE8_MEMBER(another_world_state::switch_level_bank)
{
    level_bank = data;
}

WRITE8_MEMBER(another_world_state::select_active_videopage_y)
{
    membank("active_videopage")->set_entry(data);
}

WRITE8_MEMBER(another_world_state::select_work_videopage_y)
{
    work_videopage_y = data;
    membank("work_videopage")->set_entry(256 * work_videopage_bank + work_videopage_y);
}

WRITE8_MEMBER(another_world_state::switch_work_videopage_bank)
{
    work_videopage_bank = data;
    membank("work_videopage")->set_entry(256 * work_videopage_bank + work_videopage_y);
}

WRITE8_MEMBER(another_world_state::changePalette)
{
    set_palette(data);
}

uint32_t another_world_state::screen_update_aw(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
    copybitmap(bitmap, m_screen_bitmap, 0, 0, 0, 0, cliprect);
    return 0;
}

static MACHINE_CONFIG_START( another_world )
    /* basic machine hardware */
    MCFG_CPU_ADD("maincpu", Z80, XTAL_16MHz)
    MCFG_CPU_PROGRAM_MAP(maincpu_prog_map)

    MCFG_CPU_ADD("soundcpu", Z80, XTAL_16MHz)
    MCFG_CPU_PROGRAM_MAP(soundcpu_prog_map)

    MCFG_CPU_ADD("videocpu", Z80, XTAL_16MHz)
    MCFG_CPU_PROGRAM_MAP(videocpu_prog_map)

    MCFG_MACHINE_START_OVERRIDE(another_world_state, anotherw)

    MCFG_GENERIC_LATCH_8_ADD("soundlatch")
    MCFG_GENERIC_LATCH_8_ADD("videolatch")
    MCFG_GENERIC_LATCH_8_ADD("param1latch")
    MCFG_GENERIC_LATCH_8_ADD("param2latch")
    MCFG_GENERIC_LATCH_8_ADD("param3latch")
    MCFG_GENERIC_LATCH_8_ADD("param4latch")
    MCFG_GENERIC_LATCH_8_ADD("musicmarklatch")

    /* video hardware */
    MCFG_SCREEN_ADD("screen", RASTER)
    MCFG_SCREEN_REFRESH_RATE(60)
    MCFG_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(0))
    MCFG_SCREEN_SIZE(SCREEN_WIDTH, SCREEN_HEIGHT)
    MCFG_SCREEN_VISIBLE_AREA(0, SCREEN_WIDTH-1, 0, SCREEN_HEIGHT-1)
    MCFG_SCREEN_UPDATE_DRIVER(another_world_state, screen_update_aw)

    MCFG_SCREEN_PALETTE("palette")
    MCFG_PALETTE_ADD("palette", 16)
    MCFG_PALETTE_INDIRECT_ENTRIES(16) /*I am not sure yet what does it mean...*/

    /* sound hardware */
    MCFG_SPEAKER_STANDARD_STEREO("lspeaker", "rspeaker")
    MCFG_SOUND_ADD("paula", PAULA_8364, XTAL_28_37516MHz / 8) //PAL
    MCFG_SOUND_ROUTE(0, "lspeaker", 0.50)
    MCFG_SOUND_ROUTE(1, "rspeaker", 0.50)
    MCFG_SOUND_ROUTE(2, "rspeaker", 0.50)
    MCFG_SOUND_ROUTE(3, "lspeaker", 0.50)

MACHINE_CONFIG_END

void another_world_state::video_start()
{
//    for (int i=0; i<4; i++){
//        m_screen->register_screen_bitmap(m_page_bitmaps[i]);
//    }
    m_screen->register_screen_bitmap(m_screen_bitmap);

//    m_curPagePtr1 = &m_page_bitmaps[2];
//    m_curPagePtr2 = &m_page_bitmaps[2];
//    m_curPagePtr3 = &m_page_bitmaps[1];
}

#define NUM_COLORS 16
void another_world_state::set_palette(uint8_t paletteId){
    const uint8_t *colors = (const uint8_t *) (memregion("palettes")->base() + level_bank * 0x10000);
    uint8_t r, g, b;

    for (int i = 0; i < NUM_COLORS; ++i)
    {
        uint8_t c1 = *(colors + paletteId * 2*NUM_COLORS + 2*i);
        uint8_t c2 = *(colors + paletteId * 2*NUM_COLORS + 2*i + 1);
        r = ((c1 & 0x0F) << 2) | ((c1 & 0x0F) >> 2);
        g = ((c2 & 0xF0) >> 2) | ((c2 & 0xF0) >> 6);
        b = ((c2 & 0x0F) >> 2) | ((c2 & 0x0F) << 2);
        m_palette->set_pen_color(i, pal6bit(r), pal6bit(g), pal6bit(b));
    }
}

MACHINE_START_MEMBER(another_world_state, anotherw)
{
//    membank("video1_bank")->configure_entries(0, 10, memregion("video1")->base(), 0x10000);
}

#define WORK_IN_PROGRESS (BAD_DUMP CRC(e2df8c47) SHA1(b79b41835aa2d5747932f8080bb6fb2cf32837d7))
ROM_START( anotherw )
    ROM_REGION( 0x8000, "maincpu", ROMREGION_ERASEFF )
    ROM_LOAD( "anotherworld_maincpu.rom", 0x0000, 0x8000, WORK_IN_PROGRESS )

    ROM_REGION( 0x8000, "soundcpu", ROMREGION_ERASEFF )
    ROM_LOAD( "anotherworld_soundcpu.rom", 0x0000, 0x8000, NO_DUMP )

    ROM_REGION( 0x8000, "videocpu", ROMREGION_ERASEFF )
    ROM_LOAD( "anotherworld_videocpu.rom", 0x0000, 0x8000, WORK_IN_PROGRESS )

    ROM_REGION( 0x90000, "bytecode", ROMREGION_ERASEFF ) /* MS-DOS: Bytecode */
    ROM_LOAD( "bytecode.rom", 0x00000, 0x90000, CRC(1fe0f8b5) SHA1(8a4650f742b4462806adaa31f358b0377a819135) )

    ROM_REGION( 0x4800, "palettes", 0 ) /* MS-DOS: Palette */
    ROM_LOAD( "palettes.rom", 0x0000, 0x4800, NO_DUMP )

    ROM_REGION( 0x90000, "video1", ROMREGION_ERASEFF ) /* MS-DOS: Cinematic */
    ROM_LOAD( "video1.rom", 0x00000, 0x90000, NO_DUMP )

    ROM_REGION( 0x8000, "video2", ROMREGION_ERASEFF ) /* MS-DOS: Video2 */
    ROM_LOAD( "video2.rom", 0x0000, 0x8000, NO_DUMP )

    ROM_REGION( 0x60000, "screens", ROMREGION_ERASEFF ) /* MS-DOS: Screens */
    ROM_LOAD( "screens.rom", 0x00000, 0x60000, NO_DUMP )

    ROM_REGION( 0x0300, "chargen", 0)
    ROM_LOAD( "anotherworld_chargen.rom", 0x0000, 0x0300, CRC(e2df8c47) SHA1(b79b41835aa2d5747932f8080bb6fb2cf32837d7) )

    ROM_REGION( 0x1800, "strings", 0)
    ROM_LOAD( "str_data.rom",  0x0000, 0x1000, CRC(9fd4058d) SHA1(7c6ccef96de2cf2bc89db71bc9fba598f74fb782) )
    ROM_LOAD( "str_index.rom", 0x1000, 0x0800, CRC(8035eeed) SHA1(12fa40e963e6f2e04c9789fe0d67fa3495dcf113) )

    ROM_REGION( 0x640000, "samples", ROMREGION_ERASEFF ) /* MS-DOS: Sound samples and music data */
    ROM_LOAD( "samples.rom", 0x000000, 0x640000, NO_DUMP )
ROM_END

/*    YEAR  NAME      PARENT    COMPAT  MACHINE        INPUT          INIT                                COMPANY              FULLNAME */
COMP( 2017, anotherw, 0,        0,      another_world, another_world, another_world_state, another_world, "FSanches", "Another World JAMMA" , MACHINE_NOT_WORKING)
