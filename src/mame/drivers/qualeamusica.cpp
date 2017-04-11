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
	DECLARE_WRITE8_MEMBER(keyboard_row_w);
	DECLARE_READ8_MEMBER(p1_r);
	DECLARE_WRITE8_MEMBER(p1_w);
	DECLARE_WRITE8_MEMBER(p3_w);

protected:
	uint8_t row_select;
	uint8_t p1_data;
};


WRITE8_MEMBER(qualeamusica_state::keyboard_row_w)
{
	row_select = data;
}

WRITE8_MEMBER(qualeamusica_state::p1_w)
{
	p1_data = data;
}

READ8_MEMBER(qualeamusica_state::p1_r)
{

	if (!BIT(row_select, 0)) return ioport("ROW0")->read();
	if (!BIT(row_select, 1)) return ioport("ROW1")->read();
	if (!BIT(row_select, 2)) return ioport("ROW2")->read();
	if (!BIT(row_select, 3)) return ioport("ROW3")->read();
	if (!BIT(row_select, 4)) return ioport("ROW4")->read();
	if (!BIT(row_select, 5)) return ioport("ROW5")->read();
	if (!BIT(row_select, 6)) return ioport("ROW6")->read();
	if (!BIT(row_select, 7)) return ioport("ROW7")->read();
	return 0xFF;
}

/*
rotina 2238 lê teclado:

          01  10  40
P1 bits:   0   4   6
FE>       37h (?) 23h     7 (?) #
FD>       61h (?) 40h     a (?) @
FB>       41h (?) 21h     A (?) !
F7>       60h (?) 53h     ` (?) S
EF>       58h 37h 5Ah     X  7  Z
DF>       20h 55h 58h    ' ' U  X
BF>       50h 4Ah 30h     P  J  0
7F>       0Dh 4Dh 39h    CR  M  9

*/

WRITE8_MEMBER(qualeamusica_state::p3_w)
{
	//p3_data = data;
}

/*************************
* Memory map information *
*************************/

static ADDRESS_MAP_START( prog_map, AS_PROGRAM, 8, qualeamusica_state )
	AM_RANGE(0x0000, 0xffff) AM_ROM
ADDRESS_MAP_END

static ADDRESS_MAP_START( io_map, AS_IO, 8, qualeamusica_state )
	AM_RANGE(0x0000, 0x7fff) AM_RAM
        AM_RANGE(0xff0d, 0xff0d) AM_WRITE(keyboard_row_w)
        //AM_RANGE(0xff0e, 0xff0e) AM_DEVREADWRITE("tms9128", tms9128_device, vram_read, vram_write)
        AM_RANGE(0xff0e, 0xff0e) AM_DEVWRITE("tms9128", tms9128_device, vram_write)
        //AM_RANGE(0xff0f, 0xff0f) AM_DEVREADWRITE("tms9128", tms9128_device, register_read, register_write)
        AM_RANGE(0xff0f, 0xff0f) AM_DEVWRITE("tms9128", tms9128_device, register_write)

	/* Ports start here */
//	AM_RANGE(MCS51_PORT_P0, MCS51_PORT_P0) AM_NOP
	AM_RANGE(MCS51_PORT_P1, MCS51_PORT_P1) AM_READWRITE(p1_r, p1_w)
//	AM_RANGE(MCS51_PORT_P2, MCS51_PORT_P2) AM_NOP
//	AM_RANGE(MCS51_PORT_P3, MCS51_PORT_P3) AM_WRITE(p3_w)
ADDRESS_MAP_END


/*************************
*      Input ports       *
*************************/

static INPUT_PORTS_START( qualeamusica )
        PORT_START("ROW0")
        PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("I") PORT_CODE(KEYCODE_I)
        PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("K") PORT_CODE(KEYCODE_K)
        PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("L") PORT_CODE(KEYCODE_L)
        PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)

        PORT_START("ROW1")
        PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("U") PORT_CODE(KEYCODE_U)
        PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("J") PORT_CODE(KEYCODE_J)
        PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("M") PORT_CODE(KEYCODE_M)
        PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)

        PORT_START("ROW2")
        PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Y") PORT_CODE(KEYCODE_Y)
        PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("H") PORT_CODE(KEYCODE_H)
        PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("N") PORT_CODE(KEYCODE_N)
        PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)

        PORT_START("ROW3")
        PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("T") PORT_CODE(KEYCODE_T)
        PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("G") PORT_CODE(KEYCODE_G)
        PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("B") PORT_CODE(KEYCODE_B)
        PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)

        PORT_START("ROW4")
        PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("R") PORT_CODE(KEYCODE_R)
        PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F") PORT_CODE(KEYCODE_F)
        PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("V") PORT_CODE(KEYCODE_V)
        PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)

        PORT_START("ROW5")
        PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("E") PORT_CODE(KEYCODE_E)
        PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("D") PORT_CODE(KEYCODE_D)
        PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("C") PORT_CODE(KEYCODE_C)
        PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)

        PORT_START("ROW6")
        PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("W") PORT_CODE(KEYCODE_W)
        PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("S") PORT_CODE(KEYCODE_S)
        PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("X") PORT_CODE(KEYCODE_X)
        PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)

        PORT_START("ROW7")
        PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Q") PORT_CODE(KEYCODE_Q)
        PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("A") PORT_CODE(KEYCODE_A)
        PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNUSED)
        PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Z") PORT_CODE(KEYCODE_Z)
        PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)
INPUT_PORTS_END


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

COMP(199?, qmusica, 0, 0, qualeamusica, qualeamusica, driver_device, 0, "Kortas/Bit9", "Qual e a Musica", MACHINE_NOT_WORKING|MACHINE_NO_SOUND)
