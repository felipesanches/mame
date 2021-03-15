// license:GPL2+
// copyright-holders:Felipe Sanches
/******************************************************************************

	Technics SX KN-1500 music keyboard driver

******************************************************************************/

#include "emu.h"
#include "cpu/tlcs900/tmp95c061.h"
#include "imagedev/floppy.h"
#include "machine/upd765.h"
#include "screen.h"
#include "speaker.h"


class kn1500_state : public driver_device
{
public:
	kn1500_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_fdc(*this, "fdc"){}

	void kn1500(machine_config &config);

private:
	required_device<tmp95c061_device> m_maincpu;
	required_device<upd765a_device> m_fdc;

	void  kn1500_tlcs900_porta_w(offs_t offset, uint8_t data);
	void  kn1500_tlcs900_portb_w(offs_t offset, uint8_t data);
	void  kn1500_tlcs900_port5_w(offs_t offset, uint8_t data);
	void  kn1500_tlcs900_port6_w(offs_t offset, uint8_t data);
	uint32_t screen_update_kn1500(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);

	void kn1500_mem(address_map &map);

	bool m_mute;
	bool m_nreset;
};


void kn1500_state::kn1500_mem(address_map &map)
{
	map(0x000080, 0x07ffff).ram(); //~CS3: MSAR3=00 MAMR3=0f
	//map(0x780000, 0x78ffff). ? //~CS0 & A19=1 & A18-16=000: MSAR0=78 MAMR0=3f
	//map(0x790000, 0x79ffff). tone_generator //~CS0 & A19=1 & A18-16=001: MSAR0=78 MAMR0=3f
	//map(0x7a0000, 0x7affff).w(m_fdc, FUNC(upd765a_device::dack_w)); //~CS0 & A19=1 & A18-16=010: MSAR0=78 MAMR0=3f
	map(0x7b0000, 0x7bffff).m(m_fdc, FUNC(upd765a_device::map)); //~CS0 & A19=1 & A18-16=011: MSAR0=78 MAMR0=3f
	map(0xc00000, 0xdfffff).rom().region("rhythm", 0); //~RHYMCS = ~CS1: MSAR1=c0 MAMR1=7f
	map(0xe00000, 0xffffff).rom().region("prog", 0);   //~PROGCS = ~CS2: MSAR2=e0 MAMR2=3f
}

static void kn1500_floppies(device_slot_interface &device)
{
	device.option_add("35dd", FLOPPY_35_DD);
}

static INPUT_PORTS_START(kn1500)
	//PORT_START("...")
	//PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_?) PORT_NAME("...")
	//PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_?) PORT_NAME("...")
	//PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_?) PORT_NAME("...")
	//PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_?) PORT_NAME("...")
	//PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_?) PORT_NAME("...")
	//PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_?) PORT_NAME("...")
	//PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_?) PORT_NAME("...")
	//PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)
INPUT_PORTS_END


void kn1500_state::kn1500_tlcs900_porta_w(offs_t offset, uint8_t data)
{
	//int DSPRDY = BIT(data, 1);
	//int ? = BIT(data, 2);
	//int FDMOT = BIT(data, 3);
}


void kn1500_state::kn1500_tlcs900_portb_w(offs_t offset, uint8_t data)
{
	//int ? = BIT(data, 0);
	//int FDINT = BIT(data, 1);
	//int FDRST = BIT(data, 2);
	//int FDTC = BIT(data, 3);
	//int ? = BIT(data, 4);
	//int FDDRQ = BIT(data, 5);
	//int ? = BIT(data, 6);
	//int ? = BIT(data, 7);
}

void kn1500_state::kn1500_tlcs900_port5_w(offs_t offset, uint8_t data)
{
	m_mute = BIT(data, 3);
}

void kn1500_state::kn1500_tlcs900_port6_w(offs_t offset, uint8_t data)
{
	m_nreset = BIT(data, 5);
}

uint32_t kn1500_state::screen_update_kn1500(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	//TODO: Implement-me!
	return 0;
}


void kn1500_state::kn1500(machine_config &config)
{
	TMP95C061(config, m_maincpu, 24_MHz_XTAL); // TMP95C061AF
	m_maincpu->set_am8_16(0);
	m_maincpu->set_addrmap(AS_PROGRAM, &kn1500_state::kn1500_mem);
	m_maincpu->porta_write().set(FUNC(kn1500_state::kn1500_tlcs900_porta_w));
	m_maincpu->portb_write().set(FUNC(kn1500_state::kn1500_tlcs900_portb_w));
	m_maincpu->port5_write().set(FUNC(kn1500_state::kn1500_tlcs900_port5_w));
	m_maincpu->port6_write().set(FUNC(kn1500_state::kn1500_tlcs900_port6_w));

	UPD765A(config, m_fdc, 24'000'000, true, true); // actual controller is UPD72070GF3BE at IC4
	m_fdc->drq_wr_callback().set([this](int state){ m_maincpu->set_input_line(TLCS900_INT7, state); });
	m_fdc->intrq_wr_callback().set([this](int state){ m_maincpu->set_input_line(TLCS900_INT5, state); });
	FLOPPY_CONNECTOR(config, "fdc:0", kn1500_floppies, "35dd", floppy_image_device::default_mfm_floppy_formats).enable_sound(true);

	//screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
	//screen.set_raw(?_MHz_XTAL, 515, 0, 160 /*480*/, 199, 0, 152);
	//screen.set_screen_update(FUNC(kn1500_state::screen_update_kn1500));

	/* sound hardware */
	//SPEAKER(config, "lspeaker").front_left();
	//SPEAKER(config, "rspeaker").front_right();
}


ROM_START(kn1500)
	ROM_REGION16_LE(0x200000, "prog" , 0)
	ROM_LOAD("technics_qsigt3c16079_5y68-j079_japan_9649eai.ic15", 0x00000, 0x200000, CRC(0f78da9a) 
SHA1(53d5c43d833fb005a7bd377583252b84b646253d))

	ROM_REGION16_LE(0x200000, "rhythm" , 0)
	ROM_LOAD("technics_qsigt3c16068_japan.ic17", 0x00000, 0x200000, NO_DUMP)
ROM_END


//   YEAR  NAME   PARENT  COMPAT  MACHINE INPUT   STATE         INIT        COMPANY      FULLNAME           FLAGS
CONS(1990, kn1500,    0,       0, kn1500, kn1500, kn1500_state, empty_init, "Technics", "SX KN-1500 PCM Keyboard", MACHINE_NOT_WORKING|MACHINE_NO_SOUND)
