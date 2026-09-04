// license:GPL-2.0+
// copyright-holders:Felipe Sanches

#include "emu.h"

#include "cpu/avr8/avr8.h"

#include "machine/nvram.h"
#include "bus/rs232/rs232.h"


namespace {

#define MASTER_CLOCK    16000000

class rambo_state : public driver_device
{
public:
	rambo_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_rs232(*this, "rs232")
		, m_eeprom(*this, "eeprom")
		, m_nvram(*this, "nvram")
	{
	}

	void rambo(machine_config &config);

private:
	virtual void machine_start() override ATTR_COLD;

	void rambo_prg_map(address_map &map) ATTR_COLD;
	void rambo_data_map(address_map &map) ATTR_COLD;

	required_device<atmega2560_device> m_maincpu;
	required_device<rs232_port_device> m_rs232;
	required_memory_region m_eeprom;
	required_device<nvram_device> m_nvram;
};

void rambo_state::rambo_prg_map(address_map &map)
{
	map(0x00000, 0x3ffff).rom();
}

void rambo_state::rambo_data_map(address_map &map)
{
	map(0x0200, 0x21FF).ram();
}

static DEVICE_INPUT_DEFAULTS_START( host_serial )
	DEVICE_INPUT_DEFAULTS( "RS232_RXBAUD", 0xff, RS232_BAUD_115200 )
	DEVICE_INPUT_DEFAULTS( "RS232_TXBAUD", 0xff, RS232_BAUD_115200 )
	DEVICE_INPUT_DEFAULTS( "RS232_DATABITS", 0xff, RS232_DATABITS_8 )
	DEVICE_INPUT_DEFAULTS( "RS232_PARITY", 0xff, RS232_PARITY_NONE )
	DEVICE_INPUT_DEFAULTS( "RS232_STOPBITS", 0xff, RS232_STOPBITS_1 )
DEVICE_INPUT_DEFAULTS_END

void rambo_state::machine_start()
{
	m_nvram->set_base(m_eeprom->base(), m_eeprom->bytes());
}

void rambo_state::rambo(machine_config &config)
{
	ATMEGA2560(config, m_maincpu, MASTER_CLOCK);
	m_maincpu->set_addrmap(AS_PROGRAM, &rambo_state::rambo_prg_map);
	m_maincpu->set_addrmap(AS_DATA, &rambo_state::rambo_data_map);
	m_maincpu->set_eeprom_tag("eeprom");
	NVRAM(config, m_nvram, nvram_device::DEFAULT_ALL_1);

	m_maincpu->set_low_fuses(0xff);
	m_maincpu->set_high_fuses(0xd9);
	m_maincpu->set_extended_fuses(0xfd);
	m_maincpu->set_lock_bits(0x0f);

	RS232_PORT(config, m_rs232, default_rs232_devices, nullptr);
	m_rs232->set_option_device_input_defaults("terminal", DEVICE_INPUT_DEFAULTS_NAME(host_serial));
	m_rs232->set_option_device_input_defaults("null_modem", DEVICE_INPUT_DEFAULTS_NAME(host_serial));
	m_rs232->set_option_device_input_defaults("pty", DEVICE_INPUT_DEFAULTS_NAME(host_serial));
	m_maincpu->txd<0>().set(m_rs232, FUNC(rs232_port_device::write_txd));
	m_rs232->rxd_handler().set(m_maincpu, FUNC(atmega2560_device::rxd_w<0>));

}

ROM_START( metamaq2 )
	ROM_REGION( 0x40000, "maincpu", 0 )
	ROM_DEFAULT_BIOS("20131015")

	ROM_SYSTEM_BIOS( 0, "20130619", "June 19th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-06-19.bin", 0x0000, 0x1000e, CRC(4279b178) SHA1(e4d3c9d6421287c980639c2df32d07b754adc8fc), ROM_BIOS(0))

	ROM_SYSTEM_BIOS( 1, "20130624", "June 24th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-06-24_mm2rc2_rambo_rev10e.bin", 0x0000, 0xcebc, CRC(82400a3c) SHA1(0781ce29406ce69b63edb93d776b9c081bed841e), ROM_BIOS(1))

	ROM_SYSTEM_BIOS( 2, "20130625", "June 25th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-06-25.bin", 0x0000, 0x10076, CRC(e7e4db38) SHA1(0c307bb0a0ee4e9d38253936e7030d0efb3c1845), ROM_BIOS(2))

	ROM_SYSTEM_BIOS( 3, "20130709", "July 9th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-07-09.bin", 0x0000, 0x10078, CRC(9a45509f) SHA1(3a2e6516b45cc0ea1aef039335b02208847aaebf), ROM_BIOS(3))

	ROM_SYSTEM_BIOS( 4, "20130712", "July 12th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-07-12.bin", 0x0000, 0x10184, CRC(9aeac87c) SHA1(c1441096553c214c12a34da87fa42cc3f0eaf74d), ROM_BIOS(4))

	ROM_SYSTEM_BIOS( 5, "20130717", "July 17th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-07-17.bin", 0x0000, 0x10180, CRC(7c053ed0) SHA1(7abeabcbfdb411b6e681e2d0c9398c40b142f76b), ROM_BIOS(5))

	ROM_SYSTEM_BIOS( 6, "20130806", "August 6th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-08-06.bin", 0x0000, 0x1017e, CRC(6aaf5a14) SHA1(93cebee8ab9eda9d81e70504b407268a198577f0), ROM_BIOS(6))

	ROM_SYSTEM_BIOS( 7, "20130809", "August 9th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-08-09.bin", 0x0000, 0x1018a, CRC(ee53a011) SHA1(666d09fe69220a172528fe8d1c358e3ddaaa743a), ROM_BIOS(7))

	ROM_SYSTEM_BIOS( 8, "20130822", "August 22nd, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-08-22.bin", 0x0000, 0x1018a, CRC(70a5a3c9) SHA1(20e52ea7bf40e71020b815b9fb6385d880677927), ROM_BIOS(8))

	ROM_SYSTEM_BIOS( 9, "20130913", "September 13th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-09-13-devel.bin", 0x0000, 0x101bc, CRC(5e7c7933) SHA1(5b9bfe919daf705ad7a9a2de3cf4c51e3338ec47), ROM_BIOS(9))

	ROM_SYSTEM_BIOS( 10, "20130920", "September 20th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-09-20.bin", 0x0000, 0x10384, CRC(48378e58) SHA1(513f0a0c65219875cc467420cc091e3489b58919), ROM_BIOS(10))

	ROM_SYSTEM_BIOS( 11, "20131015", "October 15th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-10-15.bin", 0x0000, 0x102c8, CRC(520134bd) SHA1(dfe2251aad06972f237eb4920ce14ccb32da5af0), ROM_BIOS(11))

	ROM_REGION( 0x1000, "eeprom", ROMREGION_ERASEFF )
ROM_END

} // anonymous namespace


//   YEAR  NAME      PARENT  COMPAT  MACHINE  INPUT  CLASS        INIT        COMPANY        FULLNAME                            FLAGS
COMP(2012, metamaq2, 0,      0,      rambo,   0,     rambo_state, empty_init, "Metamaquina", "Metamaquina 2 desktop 3d printer", MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
