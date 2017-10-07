// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/*
    PenseBem (TecToy) / SmartStart (VTech)
    driver by Felipe Correa da Silva Sanches <fsanches@metamaquina.com.br>

---------------------------------------------------------------------------------------------
    The 2017 edition of TecToy's PenseBem has a 2x4 programming pin-header at position CN4:

    CN4 - ATMEGA
      1 -  4 VCC
      2 - 15 MOSI
      3 - 31 TXD
      4 - 16 MISO
      5 - 30 RXD
      6 - 17 SCK
      7 -  5 GND
      8 - 29 RESET
 
    R34 is the pull-up resistor for the RESET signal
---------------------------------------------------------------------------------------------

    Changelog:

    2017 OCT 07 [Felipe Sanches]:
        * Initial driver skeleton
*/

#include "emu.h"
#include "cpu/avr8/avr8.h"
#include "sound/dac.h"
//#include "rendlay.h"
#include "screen.h"
#include "speaker.h"

#define MASTER_CLOCK    16000000
#define LOG_PORTS 1

class pensebem2017_state : public driver_device
{
public:
	pensebem2017_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_dac(*this, "dac")
	{
	}

	uint8_t m_port_b;
	uint8_t m_port_c;
	uint8_t m_port_d;
	uint8_t m_port_e;

	required_device<avr8_device> m_maincpu;
	required_device<dac_bit_interface> m_dac;

	DECLARE_READ8_MEMBER(port_r);
	DECLARE_WRITE8_MEMBER(port_w);
	DECLARE_DRIVER_INIT(pensebem2017);
	virtual void machine_reset() override;
};

READ8_MEMBER(pensebem2017_state::port_r)
{
	switch( offset )
	{
		case AVR8_IO_PORTA:
		{
#if LOG_PORTS
		printf("[%08X] Port A READ \n", m_maincpu->m_shifted_pc);
#endif
		return 0x00;
		}
		case AVR8_IO_PORTB:
		{
#if LOG_PORTS
		printf("[%08X] Port B READ \n", m_maincpu->m_shifted_pc);
#endif
		return 0x00;
		}
		case AVR8_IO_PORTC:
		{
#if LOG_PORTS
		printf("[%08X] Port C READ \n", m_maincpu->m_shifted_pc);
#endif
		return 0x00;
		}
		case AVR8_IO_PORTD:
		{
#if LOG_PORTS
		printf("[%08X] Port D READ \n", m_maincpu->m_shifted_pc);
#endif
		return 0x00;
		}
		case AVR8_IO_PORTE:
		{
#if LOG_PORTS
		printf("[%08X] Port E READ \n", m_maincpu->m_shifted_pc);
		//return ioport("keypad")->read();
#endif
		return 0x00;
		}
	}
	return 0;
}

WRITE8_MEMBER(pensebem2017_state::port_w)
{
	switch( offset )
	{
		case AVR8_IO_PORTA:
		{
			if (data == m_port_a) break;
#if LOG_PORTS
			uint8_t old_port_a = m_port_a;
			uint8_t changed = data ^ old_port_a;

			printf("[%08X] ", m_maincpu->m_shifted_pc);
//			if(changed & A_AXIS_DIR) printf("[A] A_AXIS_DIR: %s\n", data & A_AXIS_DIR ? "HIGH" : "LOW");
#endif
			m_port_a = data;
			break;
		}
		case AVR8_IO_PORTB:
		{
			if (data == m_port_b) break;
#if LOG_PORTS
			uint8_t old_port_b = m_port_b;
			uint8_t changed = data ^ old_port_b;

			printf("[%08X] ", m_maincpu->m_shifted_pc);
//			if(changed & SD_CS) printf("[B] SD Card Chip Select: %s\n", data & SD_CS ? "HIGH" : "LOW");
#endif
			m_port_b = data;
			break;
		}
		case AVR8_IO_PORTC:
		{
			if (data == m_port_c) break;

			uint8_t old_port_c = m_port_c;
			uint8_t changed = data ^ old_port_c;
#if LOG_PORTS
			printf("[%08X] ", m_maincpu->m_shifted_pc);
//			if(changed & EX2_1280) printf("[C] EX2_1280: %s\n", data & EX2_1280 ? "HIGH" : "LOW");
#endif
			m_port_c = data;

			break;
		}
		case AVR8_IO_PORTD:
		{
			if (data == m_port_d) break;
#if LOG_PORTS
			uint8_t old_port_d = m_port_d;
			uint8_t changed = data ^ old_port_d;

			printf("[%08X] ", m_maincpu->m_shifted_pc);
//			if(changed & PORTD_SCL) printf("[D] PORTD_SCL: %s\n", data & PORTD_SCL ? "HIGH" : "LOW");
#endif
			m_port_d = data;
			break;
		}
		case AVR8_IO_PORTE:
		{
			if (data == m_port_e) break;
#if LOG_PORTS
			uint8_t old_port_e = m_port_e;
			uint8_t changed = data ^ old_port_e;

			printf("[%08X] ", m_maincpu->m_shifted_pc);
//			if(changed & RX_1280) printf("[E] 1280-RX: %s\n", data & RX_1280 ? "HIGH" : "LOW");
#endif
			m_port_e = data;
			break;
		}
	}
}

/****************************************************\
* Address maps                                       *
\****************************************************/

static ADDRESS_MAP_START( pensebem2017_prg_map, AS_PROGRAM, 8, pensebem2017_state )
	AM_RANGE(0x0000, 0x3FFF) AM_ROM /* 16 kbytes of Flash ROM */
ADDRESS_MAP_END

static ADDRESS_MAP_START( pensebem2017_data_map, AS_DATA, 8, pensebem2017_state )
	AM_RANGE(0x0000, 0x03FF) AM_RAM  /* ATMEGA168PB Internal 1024 bytes of SRAM */
ADDRESS_MAP_END

static ADDRESS_MAP_START( pensebem2017_io_map, AS_IO, 8, pensebem2017_state )
	AM_RANGE(AVR8_IO_PORTA, AVR8_IO_PORTE) AM_READWRITE( port_r, port_w )
ADDRESS_MAP_END

/****************************************************\
* Input ports                                        *
\****************************************************/

static INPUT_PORTS_START( smartstart )
	PORT_START("keypad")
	PORT_BIT(0x00000001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("CENTER") PORT_CODE(KEYCODE_M)
	PORT_BIT(0x00000002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("RIGHT") PORT_CODE(KEYCODE_D)
	PORT_BIT(0x00000004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("LEFT") PORT_CODE(KEYCODE_A)
	PORT_BIT(0x00000008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("DOWN") PORT_CODE(KEYCODE_S)
	PORT_BIT(0x00000010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("UP") PORT_CODE(KEYCODE_W)
INPUT_PORTS_END

/****************************************************\
* Machine definition                                 *
\****************************************************/

DRIVER_INIT_MEMBER(pensebem2017_state, pensebem2017)
{
}

void pensebem2017_state::machine_reset()
{
	m_port_a = 0;
	m_port_b = 0;
	m_port_c = 0;
	m_port_d = 0;
	m_port_e = 0;
}


static MACHINE_CONFIG_START( pb2017 )

	MCFG_CPU_ADD("maincpu", ATMEGA88, MASTER_CLOCK) /* Atmel ATMEGA168PB with a 16MHz Crystal */
	MCFG_CPU_PROGRAM_MAP(pensebem2017_prg_map)
	MCFG_CPU_DATA_MAP(pensebem2017_data_map)
	MCFG_CPU_IO_MAP(pensebem2017_io_map)

	MCFG_CPU_AVR8_EEPROM("eeprom")
	MCFG_CPU_AVR8_LFUSE(0xFF)
	MCFG_CPU_AVR8_HFUSE(0xDA)
	MCFG_CPU_AVR8_EFUSE(0xF4)
	MCFG_CPU_AVR8_LOCK(0x0F)

	/* video hardware */
        //TODO

	/* sound hardware */
	/* A piezo is connected to the PORT <?> bit <?> */
	MCFG_SPEAKER_STANDARD_MONO("speaker")
	MCFG_SOUND_ADD("dac", DAC_1BIT, 0) MCFG_SOUND_ROUTE(0, "speaker", 0.5)
	MCFG_DEVICE_ADD("vref", VOLTAGE_REGULATOR, 0) MCFG_VOLTAGE_REGULATOR_OUTPUT(5.0)
	MCFG_SOUND_ROUTE_EX(0, "dac", 1.0, DAC_VREF_POS_INPUT)
MACHINE_CONFIG_END

ROM_START( pb2017 )
	ROM_REGION( 0x20000, "maincpu", 0 )
	ROM_DEFAULT_BIOS("v750")

	/* Version 5.1 release:
	- Initial firmware release
	*/
	ROM_SYSTEM_BIOS( 0, "v51", "V 5.1" )
	ROMX_LOAD("mighty-mb40-v5.1.bin", 0x0000, 0x10b90, CRC(20d65cd1) SHA1(da18c3eb5a29a6bc1eecd92eaae6063fe29d0305), ROM_BIOS(1))

	/*Arduino MEGA bootloader */
	ROM_LOAD( "atmegaboot_168_atmega1280.bin", 0x1f000, 0x0f16, CRC(c041f8db) SHA1(d995ebf360a264cccacec65f6dc0c2257a3a9224) )

	/* on-die 4kbyte eeprom */
	ROM_REGION( 0x1000, "eeprom", ROMREGION_ERASEFF )
ROM_END

/*   YEAR  NAME    PARENT    COMPAT    MACHINE        INPUT       STATE                INIT           COMPANY    FULLNAME */
COMP(2017, pb2017,      0,        0,   pensebem2017,  smartstart, pensebem2017_state,  pensebem2017,  "TecToy",  "PenseBem (2017)", MACHINE_NOT_WORKING)
