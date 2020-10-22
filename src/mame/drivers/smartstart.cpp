// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/*
    PenseBem (TecToy) / SmartStart (VTech)
    driver by Felipe Correa da Silva Sanches <juca@members.fsf.org>

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

1c78:
loop infinito ? volta pra 1c94... :-P

Investigar instrução RJMP no endereço F96. Comportamento difere dos parametros do disasm.



*/

#include "emu.h"
#include "cpu/avr8/avr8.h"
#include "sound/dac.h"
#include "rendlay.h"
#include "screen.h"
#include "speaker.h"
#include "pbem2017.lh"

#define MASTER_CLOCK    16000000
#define LOG_PORTS 0

class pensebem2017_state : public driver_device
{
public:
	pensebem2017_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_dac(*this, "dac"),
		m_out_x(*this, "%u.%u", 0U, 0U)
	{
	}

	uint8_t m_port_a;
	uint8_t m_port_b;
	uint8_t m_port_c;
	uint8_t m_port_d;
	uint8_t m_port_e;

	required_device<avr8_device> m_maincpu;
	required_device<dac_bit_interface> m_dac;
	output_finder<8, 8> m_out_x;

	uint8_t port_r(offs_t offset);
	void port_w(offs_t offset, uint8_t value);
	void prg_map(address_map &map);
	void data_map(address_map &map);
	void io_map(address_map &map);
	void pensebem2017(machine_config &config);

protected:
	virtual void machine_start() override;
	virtual void machine_reset() override;
};

void pensebem2017_state::machine_start()
{
	// resolve handlers
	m_out_x.resolve();
}


uint8_t pensebem2017_state::port_r(offs_t offset)
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

void pensebem2017_state::port_w(offs_t offset, uint8_t data)
{
	switch( offset )
	{
		case AVR8_IO_PORTA:
		{
			if (data == m_port_a) break;
#if LOG_PORTS
//			uint8_t old_port_a = m_port_a;
//			uint8_t changed = data ^ old_port_a;

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
//			uint8_t old_port_b = m_port_b;
//			uint8_t changed = data ^ old_port_b;

			printf("[%08X] ", m_maincpu->m_shifted_pc);
//			if(changed & SD_CS) printf("[B] SD Card Chip Select: %s\n", data & SD_CS ? "HIGH" : "LOW");
#endif
			m_port_b = data;
			break;
		}
		case AVR8_IO_PORTC:
		{
			m_port_c = data;
			for (int digit=0; digit <= 5; digit++){
				if (BIT(m_port_c, 5 - digit)){
					for (int seg=0; seg<8; seg++){
						bool value;
						if (seg < 2){
							value = BIT(m_port_e, seg);
						} else {
							value = BIT(m_port_d, seg);
						}
						m_out_x[digit][seg] = value;
					}
				}
			}
			break;
		}
		case AVR8_IO_PORTD:
		{
			if (data == m_port_d) break;
#if LOG_PORTS
//			uint8_t old_port_d = m_port_d;
//			uint8_t changed = data ^ old_port_d;

			printf("[%08X] ", m_maincpu->m_shifted_pc);
//			if(changed & PORTD_SCL) printf("[D] PORTD_SCL: %s\n", data & PORTD_SCL ? "HIGH" : "LOW");
#endif
			m_port_d = data;
			break;
		}
		case AVR8_IO_PORTE:
		{
			m_port_e = data;
			for (int digit=6; digit <= 7; digit++){
				if (BIT(m_port_e, 7 - digit + 2)){
					for (int seg=0; seg<8; seg++){
						bool value;
						if (seg < 2){
							value = BIT(m_port_e, seg);
						} else {
							value = BIT(m_port_d, seg);
						}
						m_out_x[digit][seg] = value;
					}
				}
			}
			break;
		}
	}
}

/****************************************************\
* Address maps                                       *
\****************************************************/

void pensebem2017_state::prg_map(address_map &map)
{
	map(0x0000, 0x3fff).rom().region("maincpu", 0); /* 16 kbytes of Flash ROM */
}

void pensebem2017_state::data_map(address_map &map)
{
	map(0x0000, 0x03ff).ram(); /* ATMEGA168PB Internal 1024 bytes of SRAM */
	map(0x0400, 0xffff).ram(); /* Some additional SRAM ? This is likely an exagerated amount ! */
}

/*
1 = output ?

DDRB: ? ?(0 0   0 0)? ?
DDRC: ? ? 1 1   1 1 1 1
DDRD: 1 1 1 1   1 1(0 1)
DDRE: ? ? ? ?   1 1 1 1


digits:

PORTC_5 - digit_0
PORTC_4 - digit_1
PORTC_3 - digit_2
PORTC_2 - digit_3
PORTC_1 - digit_4
PORTC_0 - digit_5
PORTE_3 - digit_6
PORTE_2 - digit_7

segments:

PORTD_7 - seg_7 (R11)
PORTD_6 - seg_6 (R12)
PORTD_5 - seg_5 (R17)
PORTD_4 - seg_4 (R13)
PORTD_3 - seg_3 (R14)
PORTD_2 - seg_2 (R15)
PORTE_1 - seg_1 (R18)
PORTE_0 - seg_0 (R16)

*/

void pensebem2017_state::io_map(address_map &map)
{
	map(AVR8_IO_PORTA, AVR8_IO_PORTE).rw(FUNC(pensebem2017_state::port_r), FUNC(pensebem2017_state::port_w));
}

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

void pensebem2017_state::machine_reset()
{
	m_port_a = 0;
	m_port_b = 0;
	m_port_c = 0;
	m_port_d = 0;
	m_port_e = 0;
}


void pensebem2017_state::pensebem2017(machine_config &config)
{
	/* CPU */
	ATMEGA328(config, m_maincpu, MASTER_CLOCK); /* Atmel ATMEGA168PB with a 16MHz Crystal */
	m_maincpu->set_addrmap(AS_PROGRAM, &pensebem2017_state::prg_map);
	m_maincpu->set_addrmap(AS_DATA, &pensebem2017_state::data_map);
	m_maincpu->set_addrmap(AS_IO, &pensebem2017_state::io_map);

	m_maincpu->set_eeprom_tag("eeprom");
	m_maincpu->set_low_fuses(0xf7);
	m_maincpu->set_high_fuses(0xdd);
	m_maincpu->set_extended_fuses(0xf9);
	m_maincpu->set_lock_bits(0x0f);

	/* video hardware */
	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_SVG));
	screen.set_refresh_hz(50);
	screen.set_size(1490, 1080);
	screen.set_visarea_full();
	config.set_default_layout(layout_pbem2017);

	/* sound hardware */
	/* A piezo is connected to the PORT <?> bit <?> */
	SPEAKER(config, "speaker").front_center();
	DAC_1BIT(config, m_dac, 0).add_route(0, "speaker", 0.5);
}

ROM_START( pbem2017 )
	ROM_REGION( 0x20000, "maincpu", 0 )
	ROM_DEFAULT_BIOS("sept2017")

	/* September 2017 release */
	ROM_SYSTEM_BIOS( 0, "sept2017", "SEPT/2017" )
	ROMX_LOAD("pensebem-2017.bin", 0x0000, 0x35b6, CRC(d394279e) SHA1(5576599394231c1f83817dd55992e3b5838ab003), ROM_BIOS(0))

	/* on-die 4kbyte eeprom */
	ROM_REGION( 0x1000, "eeprom", ROMREGION_ERASEFF )

        ROM_REGION( 335874, "screen", 0)
        ROM_LOAD( "pensebem.svg", 0, 335874, CRC(8d57bfe8) SHA1(d3ab63a7b9c63579d2ec367e84fce95a26be18c0) )
ROM_END

/*   YEAR  NAME    PARENT    COMPAT    MACHINE        INPUT       STATE                INIT         COMPANY    FULLNAME */
COMP(2017, pbem2017,    0,        0,   pensebem2017,  smartstart, pensebem2017_state,  empty_init,  "TecToy",  "PenseBem (2017)", MACHINE_NOT_WORKING)
