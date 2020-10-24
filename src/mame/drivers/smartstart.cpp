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

    2020 OCT 23 [Felipe Sanches]:
        * keyboard inputs + buzzer

    2017 OCT 07 [Felipe Sanches]:
        * Initial driver skeleton

*/

#include "emu.h"
#include "cpu/avr8/avr8.h"
#include "sound/dac.h"
#include "rendlay.h"
#include "screen.h"
#include "speaker.h"
#include "pbem2017.lh"

#define MASTER_CLOCK    16000000

class pensebem2017_state : public driver_device
{
public:
	pensebem2017_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_dac(*this, "dac"),
		m_out_x(*this, "%u.%u", 0U, 0U),
		m_keyb_rows(*this, "ROW%u", 0U)
	{
	}
	uint8_t m_port_b;
	uint8_t m_port_c;
	uint8_t m_port_d;
	uint8_t m_port_e;

	required_device<avr8_device> m_maincpu;
	required_device<dac_bit_interface> m_dac;
	output_finder<8, 8> m_out_x;
	required_ioport_array<4> m_keyb_rows;

	uint8_t port_r(offs_t offset);
	void port_w(offs_t offset, uint8_t value);
	void prg_map(address_map &map);
	void data_map(address_map &map);
	void io_map(address_map &map);
	void pensebem2017(machine_config &config);
	void update_display();
	uint8_t read_selected_keyb_row();

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
		case AVR8_IO_PORTD: return read_selected_keyb_row() & 0xfc;
		case AVR8_IO_PORTE: return read_selected_keyb_row() & 0x03;
		default:
			return 0xff;
	}
}

void pensebem2017_state::port_w(offs_t offset, uint8_t data)
{
	switch( offset )
	{
		case AVR8_IO_PORTB: // buzzer + keyboard_select_rows
			m_port_b = data;
			m_dac->write(BIT(m_port_b, 1));
			break;
		case AVR8_IO_PORTC: // display
			m_port_c = data;
			update_display();
			break;
		case AVR8_IO_PORTD: // display
			m_port_d = data;
			update_display();
			break;
		case AVR8_IO_PORTE: // display
			m_port_e = data;
			update_display();
			break;
		default:
			break;
	}
}

/*
select_rows:
1   - PB5
2   - PB3 
12  - PB2
13  - PB4

read_cols:
3   - PE0
4   - PD2
5   - PE1
6   - PD7
7   - PD6
8   - PD5
9   - PD4
10  - PD3
*/
uint8_t pensebem2017_state::read_selected_keyb_row()
{
	if (BIT(m_port_b, 5)) return m_keyb_rows[0]->read();
	if (BIT(m_port_b, 3)) return m_keyb_rows[1]->read();
	if (BIT(m_port_b, 2)) return m_keyb_rows[2]->read();
	if (BIT(m_port_b, 4)) return m_keyb_rows[3]->read();
	return 0x00;
}

void pensebem2017_state::update_display()
{
	for (int digit=0; digit <= 5; digit++){
		if (!BIT(m_port_c, 5 - digit)){
			for (int seg=0; seg<8; seg++){
				if (seg < 2){
					m_out_x[7-digit][seg] = !BIT(m_port_e, seg);
				} else {
					m_out_x[7-digit][seg] = !BIT(m_port_d, seg);
				}
			}
		}
	}
	for (int digit=6; digit <= 7; digit++){
		if (!BIT(m_port_e, 7 - digit + 2)){
			for (int seg=0; seg<8; seg++){
				if (seg < 2){
					m_out_x[7-digit][seg] = !BIT(m_port_e, seg);
				} else {
					m_out_x[7-digit][seg] = !BIT(m_port_d, seg);
				}
			}
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

B: buzzer + keyboard_select_rows
C: display
D: display + keyboard_read_cols
E: display + keyboard_read_cols

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

buzzer: PB1

select_rows:
1   - PB5
2   - PB3 
12  - PB2
13  - PB4

read_cols:
3   - PE0
4   - PD2
5   - PE1
6   - PD7
7   - PD6
8   - PD5
9   - PD4
10  - PD3

*/

void pensebem2017_state::io_map(address_map &map)
{
	map(AVR8_IO_PORTA, AVR8_IO_PORTE).rw(FUNC(pensebem2017_state::port_r), FUNC(pensebem2017_state::port_w));
}

/****************************************************\
* Input ports                                        *
\****************************************************/

static INPUT_PORTS_START( smartstart )
	PORT_START("ROW0") // A B C D ENTER LIVRO DESLIGA
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("A") PORT_CODE(KEYCODE_A)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("C") PORT_CODE(KEYCODE_C)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("B") PORT_CODE(KEYCODE_B)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Desliga") PORT_CODE(KEYCODE_MINUS)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Livro") PORT_CODE(KEYCODE_L)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Enter") PORT_CODE(KEYCODE_ENTER)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_UNUSED)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("D") PORT_CODE(KEYCODE_D)

	PORT_START("ROW1")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("1") PORT_CODE(KEYCODE_1)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Adicao") PORT_CODE(KEYCODE_Q)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Subtracao") PORT_CODE(KEYCODE_W)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("/") PORT_CODE(KEYCODE_SLASH_PAD)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("*") PORT_CODE(KEYCODE_ASTERISK)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("-") PORT_CODE(KEYCODE_MINUS_PAD)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("+") PORT_CODE(KEYCODE_PLUS_PAD)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("0") PORT_CODE(KEYCODE_0)

	PORT_START("ROW2")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Multiplicacao") PORT_CODE(KEYCODE_E)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Aritmetica") PORT_CODE(KEYCODE_T)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Divisao") PORT_CODE(KEYCODE_R)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Adivinhe o Número") PORT_CODE(KEYCODE_P)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Número do Meio") PORT_CODE(KEYCODE_O)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Memória Tons") PORT_CODE(KEYCODE_I)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Siga-me") PORT_CODE(KEYCODE_U)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Operacao") PORT_CODE(KEYCODE_Y)

	PORT_START("ROW3")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("2") PORT_CODE(KEYCODE_2)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("4") PORT_CODE(KEYCODE_4)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("3") PORT_CODE(KEYCODE_3)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("9") PORT_CODE(KEYCODE_9)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("8") PORT_CODE(KEYCODE_8)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("7") PORT_CODE(KEYCODE_7)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("6") PORT_CODE(KEYCODE_6)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("5") PORT_CODE(KEYCODE_5)
INPUT_PORTS_END


/****************************************************\
* Machine definition                                 *
\****************************************************/

void pensebem2017_state::machine_reset()
{
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
	/* A piezo is connected to the PORT B bit 1 */
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
