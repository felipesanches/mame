// license:GPL2+
// copyright-holders:Felipe Sanches
/***************************************************************************
  KOPP Brazilian bowling hardware
****************************************************************************/

#include "emu.h"
#include "cpu/z80/z80.h"
#include "machine/i8255.h"
#include "bus/isa/isa.h"
#include "bus/isa/isa_cards.h"

class kopp_state : public driver_device
{
public:
	kopp_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag) ,
		m_maincpu(*this, "maincpu") { }

	virtual void machine_reset() override;
	required_device<cpu_device> m_maincpu;
};

static ADDRESS_MAP_START( kopp_mem, AS_PROGRAM, 8, kopp_state )
	ADDRESS_MAP_UNMAP_HIGH
	AM_RANGE(0x0000, 0x7fff) AM_ROM
	AM_RANGE(0x8000, 0xffff) AM_RAM
ADDRESS_MAP_END

static ADDRESS_MAP_START( kopp_io, AS_IO, 8, kopp_state )
	ADDRESS_MAP_UNMAP_HIGH
	ADDRESS_MAP_GLOBAL_MASK(0xff)
        //AM_RANGE(0x????, 0x????) AM_DEVREADWRITE("ppi8255", i8255_device, read, write)
ADDRESS_MAP_END

/* Input ports */
static INPUT_PORTS_START( kopp )
INPUT_PORTS_END


void kopp_state::machine_reset()
{
}

static MACHINE_CONFIG_START( kopp, kopp_state )
	/* basic machine hardware */
	MCFG_CPU_ADD("maincpu",Z80, XTAL_32MHz / 8) /* TODO: check on PCB */
	MCFG_CPU_PROGRAM_MAP(kopp_mem)
	MCFG_CPU_IO_MAP(kopp_io)

        MCFG_DEVICE_ADD("isa", ISA8, 0)
        MCFG_ISA8_CPU(":maincpu")
        MCFG_ISA8_SLOT_ADD("isa", "isa1", pc_isa8_cards, "cga", false)

        MCFG_DEVICE_ADD("ppi8255", I8255A, 0) /* TODO: check PCB  */
        //MCFG_I8255_OUT_PORTA_CB(WRITE8(kopp_state, kopp_8255_porta_w))
        //MCFG_I8255_IN_PORTB_CB(READ8(kopp_state, kopp_8255_portb_r))
        //MCFG_I8255_IN_PORTC_CB(READ8(kopp_state, kopp_8255_portc_r))
MACHINE_CONFIG_END

ROM_START( kopp )
	ROM_REGION( 0x10000, "maincpu", ROMREGION_ERASEFF )
	ROM_LOAD( "kopp_boliche.rom", 0x0000, 0x8000, CRC(9dbc5e97) SHA1(aa24b6f35b05d984bccdf328d7217ef2dd9bbc3f))
ROM_END

/*    YEAR  NAME    PARENT  COMPAT   MACHINE    INPUT    INIT                COMPANY                    FULLNAME        FLAGS */
COMP(    ?, kopp,   0,      0,       kopp,      kopp,    driver_device,   0, "Eliseu Kopp & Cia Ltda",  "Boliche KOPP", MACHINE_IS_SKELETON | MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
