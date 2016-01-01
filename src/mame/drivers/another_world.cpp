// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/*
    Another World game (Virtual Machine based driver)
*/

#include "emu.h"
#include "includes/anotherworld.h"
#include "cpu/anotherworld/anotherworld.h"

/*
    driver init function
*/
DRIVER_INIT_MEMBER(another_world_state, another_world)
{
}

void another_world_state::machine_start(){
}

static INPUT_PORTS_START( another_world )
      PORT_START("keyboard")
      PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Arrow up") PORT_CODE(KEYCODE_UP)
      PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Arrow down") PORT_CODE(KEYCODE_DOWN)
      PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Arrow left") PORT_CODE(KEYCODE_LEFT)
      PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Arrow right") PORT_CODE(KEYCODE_RIGHT)
      PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Action") PORT_CODE(KEYCODE_LCONTROL)
INPUT_PORTS_END

READ16_MEMBER(another_world_state::up_down_r)
{
    int value = ioport("keyboard")->read();
    return (value & 0x01) ? 0xFFFF : (value & 0x02) ? 1 : 0;
}

READ16_MEMBER(another_world_state::left_right_r)
{
    int value = ioport("keyboard")->read();
    return (value & 0x04) ? 0xFFFF : (value & 0x08) ? 1 : 0;
}

READ16_MEMBER(another_world_state::action_r)
{
    int value = ioport("keyboard")->read();
    return (value & 0x10) ? 1 : 0;
}


/* Graphics Layouts */

static const gfx_layout charlayout =
{
    8,8,    /* 8*8 characters */
    96,     /* 96 characters */
    4,      /* 4 bits per pixel */
    { 0 },
    { 0, 1, 2, 3, 4, 5, 6, 7},
    { 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
    8*8    /* every char takes 8 consecutive bytes */
};

/* Graphics Decode Info */

static GFXDECODE_START( anotherw )
    GFXDECODE_ENTRY( "chargen", 0, charlayout,            0, 16)
GFXDECODE_END

TILE_GET_INFO_MEMBER(another_world_state::get_char_tile_info)
{
//    int attr = m_colorram[tile_index];
    int code = m_videoram[tile_index];// + ((attr & 0xe0) << 2);
//    int color = attr & 0x1f;
    int color = 15;

    tileinfo.group = color;

    SET_TILE_INFO_MEMBER(0, code, color, 0);
}

static ADDRESS_MAP_START( aw_prog_map, AS_PROGRAM, 8, another_world_state )
    AM_RANGE(0x0000, 0xfbff) AM_ROM
    AM_RANGE(0xfc00, 0xffff) AM_RAM AM_SHARE("videoram")
ADDRESS_MAP_END

static ADDRESS_MAP_START( aw_data_map, AS_DATA, 16, another_world_state )
    AM_RANGE(0x01F4, 0x01f5) AM_READ(action_r)
    AM_RANGE(0x01F6, 0x01f7) AM_READ(up_down_r)
    AM_RANGE(0x01F8, 0x01f9) AM_READ(left_right_r)
    AM_RANGE(0x0000, 0x01ff) AM_RAM //VM Variables
ADDRESS_MAP_END

static MACHINE_CONFIG_START( another_world, another_world_state )
    /* basic machine hardware */
    MCFG_CPU_ADD("maincpu", ANOTHER_WORLD, 5000) /* FIX-ME: This clock frequency is arbitrary */
    MCFG_CPU_PROGRAM_MAP(aw_prog_map)
    MCFG_CPU_DATA_MAP(aw_data_map)

    /* video hardware */
    MCFG_SCREEN_ADD("screen", RASTER)
    MCFG_SCREEN_REFRESH_RATE(60)
    MCFG_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(0))
    MCFG_SCREEN_SIZE(40*8, 25*8)
    MCFG_SCREEN_VISIBLE_AREA(0*8, 40*8-1, 0*8, 25*8-1)
    MCFG_SCREEN_UPDATE_DRIVER(another_world_state, screen_update_aw)

    MCFG_SCREEN_PALETTE("palette")
    MCFG_GFXDECODE_ADD("gfxdecode", "palette", anotherw)

    MCFG_PALETTE_ADD("palette", 16)
    MCFG_PALETTE_INDIRECT_ENTRIES(16) /*I am not sure yet what does it mean...*/
MACHINE_CONFIG_END

ROM_START( anotherw )
    ROM_REGION( 0xfc00, "maincpu", ROMREGION_ERASEFF ) /* MS-DOS: Bytecode */
    ROM_LOAD( "resource-0x15.bin", 0x0000, 0x10e1, CRC(f26172f6) SHA1(d0bc831a0683bb1416900c45be677a51fb9bc0fa) ) /* Protection screens */

    ROM_REGION( 0x0800, "colors", 0 ) /* MS-DOS: Palette */
    ROM_LOAD( "resource-0x14.bin", 0x0000, 0x0800, CRC(d72808cf) SHA1(b078c4a11628a384ab7c3128dfe93eaeb2745c07) ) /* Protection screens */

    ROM_REGION( 0x2000, "video1", ROMREGION_ERASEFF ) /* MS-DOS: Cinematic */
    ROM_LOAD( "resource-0x16.bin", 0x0000, 0x1404, CRC(114b0df5) SHA1(41d191da457779b0ce140035889ad2c73bf171b8) ) /* Protection screens */
    //ROM_LOAD( "resource-0x1b.bin", 0x0000, 0x514a, CRC(82ccacd6) SHA1(f093b219e10d3bd9d9fc93d36cb232f13da4881e) ) /* Intro */

    //ROM_REGION( 0x????, "video2", ROMREGION_ERASEFF ) /* MS-DOS: Video2 */
    //ROM_LOAD( "resource-0x00.bin", 0x0000, 0x????, CRC(?) SHA1(?) ) /* Protection screens */

    ROM_REGION( 0x0300, "chargen", 0)
    ROM_LOAD( "anotherworld_chargen.rom", 0x0000, 0x0300, CRC(e2df8c47) SHA1(b79b41835aa2d5747932f8080bb6fb2cf32837d7) )
ROM_END

/*    YEAR  NAME      PARENT    COMPAT  MACHINE        INPUT          INIT                                COMPANY              FULLNAME */
COMP( 199?, anotherw, 0,        0,      another_world, another_world, another_world_state, another_world, "Delphine Software", "Another World - MSDOS (VM)" , MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
