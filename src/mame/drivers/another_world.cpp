// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/*
    Another World game (Virtual Machine based driver)
*/

#include "emu.h"
#include "cpu/anotherworld/anotherworld.h"

class another_world_state : public driver_device
{
public:
    another_world_state(const machine_config &mconfig, device_type type, const char *tag)
        : driver_device(mconfig, type, tag)
    { }

    DECLARE_DRIVER_INIT(another_world);
    virtual void machine_start() override;
};

/*
    driver init function
*/
DRIVER_INIT_MEMBER(another_world_state, another_world)
{
}

void another_world_state::machine_start(){
}

static INPUT_PORTS_START( another_world )
/*
      PORT_START("FOO")
      PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("bit 0") PORT_CODE(KEYCODE_EQUALS) PORT_TOGGLE
      PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("bit 1") PORT_CODE(KEYCODE_MINUS) PORT_TOGGLE
      PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("bit 2") PORT_CODE(KEYCODE_0) PORT_TOGGLE
      PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("bit 3") PORT_CODE(KEYCODE_9) PORT_TOGGLE
      PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("bit 4") PORT_CODE(KEYCODE_8) PORT_TOGGLE
      PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("bit 5") PORT_CODE(KEYCODE_7) PORT_TOGGLE
      PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("bit 6") PORT_CODE(KEYCODE_6) PORT_TOGGLE
      PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("bit 7") PORT_CODE(KEYCODE_5) PORT_TOGGLE
*/
INPUT_PORTS_END

static ADDRESS_MAP_START( another_world_map, AS_PROGRAM, 8, another_world_state)
    ADDRESS_MAP_UNMAP_HIGH
    AM_RANGE(0x0000, 0xffff) AM_ROM
ADDRESS_MAP_END

static MACHINE_CONFIG_START( another_world, another_world_state )
    /* basic machine hardware */
    MCFG_CPU_ADD("maincpu", ANOTHER_WORLD, 500000)
    MCFG_CPU_PROGRAM_MAP(another_world_map)
MACHINE_CONFIG_END

ROM_START( anotherw )
    ROM_REGION( 0x10000, "maincpu", ROMREGION_ERASEFF )
    /* anotherworld_msdos_resource_0x1b.bin */
    ROM_LOAD( "resource-0x1b.bin", 0x0000, 0x514a, CRC(82ccacd6) SHA1(f093b219e10d3bd9d9fc93d36cb232f13da4881e) )
ROM_END

/*    YEAR  NAME      PARENT    COMPAT  MACHINE        INPUT          INIT                                COMPANY              FULLNAME */
COMP( 199?, anotherw, 0,        0,      another_world, another_world, another_world_state, another_world, "Delphine Software", "Another World - MSDOS (VM)" , MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
