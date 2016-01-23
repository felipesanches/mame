// license:GPL2+
// copyright-holders:Felipe Sanches

#include "emu.h"
#include "cpu/mcs51/mcs51.h"

class djboy_state : public driver_device
{
public:
        djboy_state(const machine_config &mconfig, device_type type, const char *tag)
                : driver_device(mconfig, type, tag),
                m_cpu(*this, "mcu")
                { }

        /* devices */
        required_device<cpu_device> m_cpu;

        DECLARE_READ8_MEMBER(lexmark_p0_r);
        DECLARE_WRITE8_MEMBER(lexmark_p0_w);
        DECLARE_READ8_MEMBER(lexmark_p1_r);
        DECLARE_WRITE8_MEMBER(lexmark_p1_w);
        DECLARE_READ8_MEMBER(lexmark_p2_r);
        DECLARE_WRITE8_MEMBER(lexmark_p2_w);
        DECLARE_READ8_MEMBER(lexmark_p3_r);
        DECLARE_WRITE8_MEMBER(lexmark_p3_w);
        DECLARE_DRIVER_INIT(lexmark);
        virtual void machine_start() override;
        virtual void machine_reset() override;
};

static ADDRESS_MAP_START( prog_map, AS_PROGRAM, 8, lexmark_state )
	AM_RANGE(0x0000, 0x7fff) AM_ROM
	AM_RANGE(0x8000, 0xffff) AM_RAM
ADDRESS_MAP_END

READ8_MEMBER(lexmark_state:p0_r)
{
    return 0; // ?
}

WRITE8_MEMBER(lexmark_state::p0_w)
{
}

READ8_MEMBER(lexmark_state::p1_r)
{
    return 0; // ?
}

WRITE8_MEMBER(lexmark_state::p1_w)
{
}

READ8_MEMBER(lexmark_state::p2_r)
{
    return 0; // ?
}

WRITE8_MEMBER(lexmark_state::p2_w)
{
}

READ8_MEMBER(lexmark_state::p3_r)
{
	return 0; // ?
}

WRITE8_MEMBER(lexmark_state::p3_w)
{
}

static ADDRESS_MAP_START( io_map, AS_IO, 8, lexmark_state )
	AM_RANGE(MCS51_PORT_P0, MCS51_PORT_P0) AM_READWRITE(p0_r, p0_w)
	AM_RANGE(MCS51_PORT_P1, MCS51_PORT_P1) AM_READWRITE(p1_r, p1_w)
	AM_RANGE(MCS51_PORT_P2, MCS51_PORT_P2) AM_READWRITE(p2_r, p2_w)
	AM_RANGE(MCS51_PORT_P3, MCS51_PORT_P3) AM_READWRITE(p3_r, p3_w)
ADDRESS_MAP_END

/******************************************************************************/

static INPUT_PORTS_START( djboy )
INPUT_PORTS_END

static MACHINE_CONFIG_START( djboy, djboy_state )
	MCFG_CPU_ADD("mcu", I80C51, 6000000) // actual cpu is an AT89C55
	MCFG_CPU_PROGRAM_MAP(lexmark_prog_map)
        MCFG_CPU_IO_MAP(lexmark_io_map)
MACHINE_CONFIG_END

ROM_START( lexmark )
	ROM_REGION( 0x1000, "mcu", 0 ) /* microcontroller */
	ROM_LOAD( "43h2911_v4.10_toc_lexmark_rk_1996.plcc44", 0x00000, 0x1000, CRC(944d256a) SHA1(b9703569bc48c3f2829940f52ef1a51cd758aa11) )
ROM_END

/*    YEAR, NAME,    PARENT,  MACHINE, INPUT,   INIT,                   MNTR, COMPANY, FULLNAME, FLAGS */
GAME( 1996, 43h2911, lexmark, lexmark, lexmark, lexmark_state, lexmark, ROT0, "Lexmark International inc.", "43H2911", MACHINE_NOT_WORKING )
