// license:GPL2+
// copyright-holders:Felipe Sanches
/**************************************************************************
 *
 * fernvale.c - Fernvale board
 *
 * Driver by Felipe Correa da Silva Sanches <juca@members.fsf.org>
 *
 * == More info ==
 *
 * "From Gongkai to Open Source"
 * http://www.bunniestudios.com/blog/?p=4297
 *
 * Operating System development & devtools at GitHub:
 * https://github.com/xobs/fernly
 *
 * Emulation in QEMU:
 * https://github.com/xobs/fernvale-qemu
 *
 **************************************************************************/

#include "emu.h"
#include "cpu/arm7/arm7.h"
#include "cpu/arm7/arm7core.h"

class fernvale_state : public driver_device
{
public:
	fernvale_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu") { }

	virtual void machine_reset();
	required_device<cpu_device> m_maincpu;
};

void fernvale_state::machine_reset()
{
}

/* This current memory map is probably wrong and must be reviewed: */
static ADDRESS_MAP_START( fernvale_map, AS_PROGRAM, 32, fernvale_state )
//	AM_RANGE(0x00000000, 0x007fffff) AM_RAM
//	AM_RANGE(0xfff00000, 0xfff0ffff) AM_ROM

	AM_RANGE(0x00000000, 0x0000ffff) AM_ROM
	AM_RANGE(0xff000000, 0xff7fffff) AM_RAM
ADDRESS_MAP_END

static MACHINE_CONFIG_START( fernvale, fernvale_state )
	MCFG_CPU_ADD("maincpu", ARM9, 3640000) /* AMR7EJ XTAL_26MHz gets somehow divided into 3.64MHz */
	MCFG_CPU_PROGRAM_MAP(fernvale_map)
MACHINE_CONFIG_END

ROM_START( fernvale )
	ROM_REGION( 0x10000, "maincpu", 0 )
	ROM_LOAD( "fernvale_boot.rom", 0x000000, 0x010000, CRC(e6b49ed6) SHA1(861b791a86679e5019dd86cb453cc48fa3380f83))
ROM_END

CONS(2015, fernvale, 0, 0, fernvale, 0, driver_device, 0, "Fernvale Project", "Fernvale", GAME_NOT_WORKING|GAME_NO_SOUND)
