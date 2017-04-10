// license:BSD-3-Clause
// copyright-holder:FelipeSanches

#include "emu.h"
#include "cpu/mcs51/mcs51.h"
#include "video/tms9928a.h"
#define XTAL_10_726835MHz 10726835

class qualeamusica_state : public driver_device
{
public:
	qualeamusica_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
	{
	}
};


/*************************
* Memory map information *
*************************/

static ADDRESS_MAP_START( prog_map, AS_PROGRAM, 8, qualeamusica_state )
	AM_RANGE(0x0000, 0xffff) AM_ROM
ADDRESS_MAP_END

static ADDRESS_MAP_START( io_map, AS_IO, 8, qualeamusica_state )
        AM_RANGE(0x98, 0x98) AM_DEVREADWRITE("tms9128", tms9128_device, vram_read, vram_write)
        AM_RANGE(0x99, 0x99) AM_DEVREADWRITE("tms9128", tms9128_device, register_read, register_write)
	AM_RANGE(0x8000, 0xffff) AM_RAM

	/* Ports start here */
	AM_RANGE(MCS51_PORT_P0, MCS51_PORT_P3) AM_RAM
ADDRESS_MAP_END


/*************************
*      Input ports       *
*************************/

//static INPUT_PORTS_START( qualeamusica )
//INPUT_PORTS_END


/*************************
*     Machine Driver     *
*************************/

static MACHINE_CONFIG_START( qualeamusica, qualeamusica_state )
	// basic machine hardware
	MCFG_CPU_ADD("maincpu", I80C32, XTAL_11_0592MHz) /* Actually the board has a Dallas DS80C320 mcu. */
	MCFG_CPU_PROGRAM_MAP(prog_map)
	MCFG_CPU_IO_MAP(io_map)

        /* Video hardware */
        MCFG_DEVICE_ADD( "tms9128", TMS9128, XTAL_10_726835MHz)
        MCFG_TMS9928A_VRAM_SIZE(0x10000)
//        MCFG_TMS9928A_OUT_INT_LINE_CB(WRITELINE(msx_state,msx_irq_source0))
        MCFG_TMS9928A_SCREEN_ADD_NTSC( "screen" )
        MCFG_SCREEN_UPDATE_DEVICE("tms9128", tms9128_device, screen_update)
MACHINE_CONFIG_END


/*************************
*        Rom Load        *
*************************/

ROM_START( qmusica ) 
	ROM_REGION( 0x10000, "maincpu", 0 )
	ROM_LOAD( "2400.ci4",   0x00000, 0x10000, CRC(4535f6b5) SHA1(e0db7653157a6b92e805f76207a999b7bcbc3d80) )
ROM_END

COMP(199?, qmusica, 0, 0, qualeamusica, 0, driver_device, 0, "Kortas/Bit9", "Qual e a Musica", MACHINE_NOT_WORKING|MACHINE_NO_SOUND)
