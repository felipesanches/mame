// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/****************************************************************************

    Skeleton driver for Sony DFS-500 DME Video Mixer

****************************************************************************/

#include "emu.h"
#include "cpu/nec/nec.h"
#include "machine/gen_latch.h"
#include "machine/pit8253.h"
#include "machine/pic8259.h"
#include "machine/i8251.h"


class dfs500_state : public driver_device
{
public:
	dfs500_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_cpanelcpu(*this, "cpanelcpu")
		, m_maincpu(*this, "maincpu")
		, m_effectcpu(*this, "effectcpu")
		, m_pit(*this, "pit")
		, m_pic(*this, "pic")
		, m_serial1(*this, "serial1")
		, m_serial2(*this, "serial2")
		, m_cpanel_serial(*this, "cpanel_serial")
		, m_cpanel_pit(*this, "cpanel_pit")
		, m_rombank1(*this, "rombank1")
		, m_rombank2(*this, "rombank2")
	{
	}

	void dfs500(machine_config &config);

private:
	virtual void machine_start() override;

	void cpanelcpu_mem_map(address_map &map);
	void cpanelcpu_io_map(address_map &map);
	void maincpu_mem_map(address_map &map);
	void maincpu_io_map(address_map &map);
	void effectcpu_mem_map(address_map &map);
	uint8_t pit_r(offs_t offset);
	void pit_w(offs_t offset, uint8_t data);
	uint8_t pic_r(offs_t offset);
	void pic_w(offs_t offset, uint8_t data);
	void rombank1_entry_w(offs_t offset, uint8_t data);
	void rombank2_entry_w(offs_t offset, uint8_t data);
	void input_select_w(offs_t offset, uint8_t data);
	uint16_t RA0_r(offs_t offset);
	uint16_t RB0_r(offs_t offset);
	void WA0_w(offs_t offset, uint16_t data);
	void WB0_w(offs_t offset, uint16_t data);
	uint16_t RA1_r(offs_t offset);
	uint8_t RB1_r(offs_t offset);
	uint8_t RB2_r(offs_t offset);

	uint8_t m_input_sel_A;
	uint8_t m_input_sel_B;
	uint16_t m_maincpu_latch16;
	uint16_t m_effectcpu_latch16;
	bool m_TOC;
	bool m_TOE;
	// TODO: bool m_BVS;

	required_device<v20_device> m_cpanelcpu;
	required_device<v30_device> m_maincpu;
	required_device<v30_device> m_effectcpu;
	required_device<pit8254_device> m_pit;
	required_device<pic8259_device> m_pic;
	required_device<i8251_device> m_serial1;
	required_device<i8251_device> m_serial2;
	required_device<i8251_device> m_cpanel_serial;
	required_device<pit8254_device> m_cpanel_pit;
	required_memory_bank m_rombank1;
	required_memory_bank m_rombank2;
};

void dfs500_state::machine_start()
{
	m_rombank1->configure_entries(0, 128, memregion("effectdata")->base(), 0x4000);
	m_rombank2->configure_entries(0, 128, memregion("effectdata")->base(), 0x4000);
	m_rombank1->set_entry(0);
	m_rombank2->set_entry(0);
	m_maincpu_latch16 = 0x0000;
	m_effectcpu_latch16 = 0x0000;
	m_TOC = false;
	m_TOE = false;
}

uint8_t dfs500_state::pit_r(offs_t offset)
{
	// CXQ71054P (Programmable Timer / Counter)
	return m_pit->read((offset>>1) & 3); // addressed by CPU's address bits AB2 and AB1
}

void dfs500_state::pit_w(offs_t offset, uint8_t data)
{
	// CXQ71054P (Programmable Timer / Counter)
	m_pit->write((offset>>1) & 3, data); // addressed by CPU's address bits AB2 and AB1
}

uint8_t dfs500_state::pic_r(offs_t offset)
{
	// PD71059C (Programmable Interrupt Controller)
	return m_pic->read((offset>>1) & 1); // addressed by CPU's address bit AB1
}

void dfs500_state::pic_w(offs_t offset, uint8_t data)
{
	// PD71059C (Programmable Interrupt Controller)
	m_pic->write((offset>>1) & 1, data); // addressed by CPU's address bit AB1
}

void dfs500_state::rombank1_entry_w(offs_t offset, uint8_t data)
{
	m_rombank1->set_entry(((data >> 1) & 0x40) | (data & 0x3f));
}

void dfs500_state::rombank2_entry_w(offs_t offset, uint8_t data)
{
	m_rombank2->set_entry(((data >> 1) & 0x40) | (data & 0x3f));
}

void dfs500_state::input_select_w(offs_t offset, uint8_t data)
{
	// Selects sources of video input on the AD-76 board.
	m_input_sel_A = (data >> 3) & 0x7;
	m_input_sel_B = data & 0x7;
}

void dfs500_state::WA0_w(offs_t offset, uint16_t data)
{
	m_effectcpu_latch16 = data;
	m_TOC = true;
}

uint16_t dfs500_state::RA0_r(offs_t offset)
{
	return m_maincpu_latch16;
	m_TOE = false;
}

uint16_t dfs500_state::RA1_r(offs_t offset)
{
	// "TEST, 1, OPT2, OPT1, RFLD, VD, TOC, TOE"
	uint8_t value = 0x40;
	// FIXME! Add other signals.
	if (m_TOC) value |= 0x02;
	if (m_TOE) value |= 0x01;
	value |= ((ioport("DSW_S3")->read() & 0x0f) << 8); // (Unknown)
	return value;
}

void dfs500_state::WB0_w(offs_t offset, uint16_t data)
{
	m_maincpu_latch16 = data;
	m_TOE = true;
}

uint16_t dfs500_state::RB0_r(offs_t offset)
{
	return m_effectcpu_latch16;
	m_TOC = false;
}

uint8_t dfs500_state::RB1_r(offs_t offset)
{
	//"TEST, OPT2, OPT1, VD, T2, T1, TOE, TOC"
	uint8_t value = 0;
	// FIXME! Add other signals.
	if (m_TOE) value |= 0x02;
	if (m_TOC) value |= 0x01;
	return value;
}

uint8_t dfs500_state::RB2_r(offs_t offset)
{
	uint8_t value = 0;
	value |= ((ioport("DSW_S1")->read() & 0x0f) << 4); // (Editing Control Unit Select)
	value |= (ioport("DSW_S2")->read() & 0x0f);        // (Freeze Timing)
	// TODO:
	// if (m_BVS) value |= 0x10;
	return value;
}

void dfs500_state::cpanelcpu_mem_map(address_map &map)
{
	map(0x00000, 0x07fff).mirror(0xe8000).ram();                        // 32kb SRAM chip at IC15 
	map(0x10000, 0x17fff).mirror(0xe8000).rom().region("cpanelcpu", 0); // 32kb EPROM at IC14
}

void dfs500_state::cpanelcpu_io_map(address_map &map)
{
	//FIXME! map(0x0000, 0x0007).mirror(0x8ff8).rw(FUNC(dfs500_state::pit_r), FUNC(dfs500_state::pit_w)); //"IO1" IC51
}

void dfs500_state::maincpu_mem_map(address_map &map)
{
	//FIXME: The RAM mirror should be 0xe0000 according to IC49 on board SY-172 (as it is wired on the service manual schematics)
	//       but I saw unmapped accesses to the A15 mirror of this range on the MAME debugger, which suggests the 0xe8000 value used below:
	map(0x00000, 0x07fff).mirror(0xe8000).ram();                      // 4x 8kb SRAM chips at IC59/IC60/IC61/IC62 
	map(0x10000, 0x1ffff).mirror(0xe0000).rom().region("maincpu", 0); // 2x 32kb EPROMs at IC1/IC2
}

void dfs500_state::maincpu_io_map(address_map &map)
{
	map(0x0000, 0x0007).mirror(0x8ff8).rw(FUNC(dfs500_state::pit_r), FUNC(dfs500_state::pit_w));                    // "IO1" IC51
	map(0x1000, 0x1001).mirror(0x8ffc).rw(FUNC(dfs500_state::pic_r), FUNC(dfs500_state::pic_w));                    // "IO2" IC52
	map(0x2000, 0x2000).mirror(0x8ffd).rw(m_serial1, FUNC(i8251_device::data_r), FUNC(i8251_device::data_w));       // "IO3" IC53
	map(0x2002, 0x2002).mirror(0x8ffd).rw(m_serial1, FUNC(i8251_device::status_r), FUNC(i8251_device::control_w));
	map(0x3000, 0x3000).mirror(0x8ffd).rw(m_serial2, FUNC(i8251_device::data_r), FUNC(i8251_device::data_w));       // "IO4" IC54
	map(0x3002, 0x3002).mirror(0x8ffd).rw(m_serial2, FUNC(i8251_device::status_r), FUNC(i8251_device::control_w));
	map(0x4000, 0x4001).mirror(0x8ff0).r(FUNC(dfs500_state::RB0_r));                                                // "RB0" IC32/IC33
	map(0x4002, 0x4003).mirror(0x8ff0).r(FUNC(dfs500_state::RB1_r));                                                // "RB1" IC55
	map(0x4004, 0x4005).mirror(0x8ff0).r(FUNC(dfs500_state::RB2_r));                                                // "RB2" IC56
	map(0x5000, 0x5001).mirror(0x8ff0).w(FUNC(dfs500_state::WB0_w));                                                // "WB0" IC26/IC27
}

void dfs500_state::effectcpu_mem_map(address_map &map)
{
	// Note: As far as I can tell by the schematics in the service manual, the ram mirror should be 0x90000.
	//       But I see unmapped read acesses to 0x4800, which induces me to make the mirror 0x94000, instead.
	//	 FIXME: This should be double-checked!
	map(0x00000, 0x03fff).mirror(0x94000).ram();                        // 2x 8kb SRAM chips at IC23/IC24 
	map(0x20000, 0x20001).mirror(0x9fffe).noprw(); // FIXME: Do something with the MTRX signal generated by this memory access
	map(0x46000, 0x46fff).mirror(0x91000).ram().share("sgram"); // IC80/IC81 at FM-29 (4/6)
	                                                            // Selected by IC201 at FM-29 (3/6)
	map(0x08000, 0x0bfff).mirror(0x90000).bankr("rombank1");            // Effect data on 4x 512kb EPROMs at IC5/IC6/IC7/IC8
	map(0x0c000, 0x0ffff).mirror(0x90000).bankr("rombank2");            // Second banked view of the contents of the same effect data EPROMs
	map(0x60000, 0x7ffff).mirror(0x80000).rom().region("effectcpu", 0); // 2x 64kb EPROMs at IC3/IC4,
	                                                                    // Note: the 1st half of each is entirely made of 0xFF
	map(0x68000, 0x68001).mirror(0x907fe).rw(FUNC(dfs500_state::RA0_r), FUNC(dfs500_state::WA0_w)); // "RA0" IC26/IC27 & "WA0" IC32/IC33
	                                                                                                // 16-bit data latches for communication between CPUs
	map(0x68800, 0x68801).mirror(0x907fe).r(FUNC(dfs500_state::RA1_r));            // "RA1" IC25/IC64
	map(0x69000, 0x69000).mirror(0x907ff).w(FUNC(dfs500_state::rombank2_entry_w)); // "WA2" IC29
	map(0x69800, 0x69800).mirror(0x907ff).w(FUNC(dfs500_state::rombank1_entry_w)); // "WA3" IC30
	map(0x6A000, 0x6A000).mirror(0x907ff).w(FUNC(dfs500_state::input_select_w));   // "WA4" IC31

	// ==== "ORG1" registers ====
	// "controlsignals" (Not sure yet of their actual use)
	// D13: PS2
	// D12: PS1
	// D11: FGS1
	// D10: FGS0
	// D9: BGS1
	// D8: BGS0
	// D7: TKON
	// D6: TKCONT
	// D5: AM2
	// D4: AM1
	// D3: AM0
	// D2: BM2
	// D1: BM1
	// D0: BM0
	map(0x6B078, 0x6B079).mirror(0x90700).w("controlsignals", FUNC(generic_latch_16_device::write));

	// "Reg7_5":
	//  Not sure yet of their actual use...
	map(0x6B07a, 0x6B07b).mirror(0x90700).w("reg7_5", FUNC(generic_latch_16_device::write));

	// XFLT: Feeds into chips IC107 (CKD8070K "Horizontal Variable Filter") and IC108 (CXD8276Q "CMOS Linear Interpolation")
	//        at PCB FM-29 (6/6); Foreground Bus Digital Lowpass Filter
	map(0x6B07c, 0x6B07d).mirror(0x90700).w("xflt", FUNC(generic_latch_16_device::write));

	// YFLT: Feeds into chips IC114 (CKD8263Q "Vertical Variable Filter") and IC115 (CXD8276Q "CMOS Linear Interpolation")
	//        at PCB FM-29 (6/6); Foreground Bus Digital Lowpass Filter
	map(0x6B07e, 0x6B07f).mirror(0x90700).w("yflt", FUNC(generic_latch_16_device::write));

	// ==== "ORG2" registers: ====
	//map(0x6B800, 0x6B800).mirror(0x907ff).w(...);

	// CKD8263Q: "Color Bar Pattern Generator"
//IC73:
// BA15..12 = 0110 MEMPRG 0x06000 


// REGA: AA18=0 AA17=0 AA15=0 AA14=1
// !REGA: AA18=1 AA17=1 AA15=1 AA14=0
// WRA:  /WR
// WAn:  !REGA & !WRA & n=AA13/12/11
// WAn:  ?11? 10nn n??? ???? ????

// ARAM = AA18/17=10
// ARAMW = ?
// MTRX = ?
// OREG1 = REGA=0 (AA18...14=00x01) AA13..11=110 => x00x 0111 0xxx xxxx xxxx => map(0x07000, 0x07001).mirror(0x90000)
// OREG2 = REGA=0 (AA18...14=00x01) AA13..11=111 => x00x 0111 1xxx xxxx xxxx => map(0x07800, 0x07801).mirror(0x90000) 
// OBUS = active-low "ARAM or MTR or OREG1 or OREG2" 
}

static INPUT_PORTS_START(dfs500)
	PORT_START("DSW_S1")
	PORT_DIPNAME( 0x0f, 0x02, "Editing Control Unit Select" )   PORT_DIPLOCATION("S1:4,3,2,1")
	PORT_DIPSETTING(    0x08, "BVE-600" )
	PORT_DIPSETTING(    0x04, "ONE-GPI" )
	PORT_DIPSETTING(    0x02, "BVE-900" )
	PORT_DIPSETTING(    0x01, "BVS-300" )

	PORT_START("DSW_S2")
	PORT_DIPNAME( 0x0f, 0x07, "Freeze Timing" )   PORT_DIPLOCATION("S2:4,3,2,1")
	PORT_DIPSETTING(    0x07, "8" )
	PORT_DIPSETTING(    0x0b, "4" )
	PORT_DIPSETTING(    0x0d, "2" )
	PORT_DIPSETTING(    0x0e, "1" )

	PORT_START("DSW_S3")
	PORT_DIPNAME( 0x08, 0x08, "Field freeze" )   PORT_DIPLOCATION("S3:4")
	PORT_DIPSETTING(    0x00, "Odd Field" )
	PORT_DIPSETTING(    0x08, "Even Freeze" )
	PORT_DIPNAME( 0x04, 0x04, "Color-Matte Compensation" )   PORT_DIPLOCATION("S3:3")
	PORT_DIPSETTING(    0x00, "Illegal compensation" )
	PORT_DIPSETTING(    0x04, "Limit compensation" )
	PORT_DIPNAME( 0x02, 0x02, "Set up" )   PORT_DIPLOCATION("S3:2")
	PORT_DIPSETTING(    0x00, "7.5%" )
	PORT_DIPSETTING(    0x02, "0%" )
	PORT_DIPNAME( 0x01, 0x00, "Freeze (When changing the crosspoint)" )   PORT_DIPLOCATION("S3:1")
	PORT_DIPSETTING(    0x00, "2 Frames" )
	PORT_DIPSETTING(    0x01, "0 Frame" )
INPUT_PORTS_END

void dfs500_state::dfs500(machine_config &config)
{
	/******************* Control Panel ******************************/
	// NEC D70108C-8 at IC10 (a CPU compatible with Intel 8088)
	V20(config, m_cpanelcpu, 8_MHz_XTAL);
	m_cpanelcpu->set_addrmap(AS_PROGRAM, &dfs500_state::cpanelcpu_mem_map);
	m_cpanelcpu->set_addrmap(AS_IO, &dfs500_state::cpanelcpu_io_map);

	// CXQ71054P at IC16 (Programmable Timer / Counter)
	pit8254_device &m_cpanel_pit(PIT8254(config, "cpanel_pit", 0));
	m_cpanel_pit.set_clk<0>(8_MHz_XTAL);
	m_cpanel_pit.set_clk<1>(8_MHz_XTAL/2);
	m_cpanel_pit.out_handler<1>().set(m_cpanel_pit, FUNC(pit8254_device::write_clk2));
	m_cpanel_pit.out_handler<0>().set(m_cpanel_serial, FUNC(i8251_device::write_txc));
	m_cpanel_pit.out_handler<0>().append(m_cpanel_serial, FUNC(i8251_device::write_rxc));

	// CXQ71051P at IC17 (Serial Interface Unit)
	I8251(config, m_cpanel_serial, 8_MHz_XTAL/2);
	m_cpanel_serial->txd_handler().set(m_serial1, FUNC(i8251_device::write_rxd));

	//Buzzer
	//...

	/******************* Effects Processing Unit ********************/
	// CXQ70116P-10 at IC40 (same as V20, but with a 16-bit data bus)
	V30(config, m_maincpu, 8_MHz_XTAL);
	m_maincpu->set_addrmap(AS_PROGRAM, &dfs500_state::maincpu_mem_map);
	m_maincpu->set_irq_acknowledge_callback("pic", FUNC(pic8259_device::inta_cb));

	// CXQ70116P-10 at IC9
	V30(config, m_effectcpu, 8_MHz_XTAL);
	m_effectcpu->set_addrmap(AS_PROGRAM, &dfs500_state::effectcpu_mem_map);

	// CXQ71054P at IC51 (Programmable Timer / Counter)
	pit8254_device &m_pit(PIT8254(config, "pit", 0));
	m_pit.set_clk<0>(8_MHz_XTAL);
	m_pit.set_clk<1>(8_MHz_XTAL);
	m_pit.out_handler<1>().set(m_pit, FUNC(pit8254_device::write_clk2));
	m_pit.out_handler<0>().set(m_serial1, FUNC(i8251_device::write_txc));
	m_pit.out_handler<0>().append(m_serial1, FUNC(i8251_device::write_rxc));
	m_pit.out_handler<0>().append(m_serial2, FUNC(i8251_device::write_txc));
	m_pit.out_handler<0>().append(m_serial2, FUNC(i8251_device::write_rxc));

	// NEC D71059C at IC52 (Programmable Interruption Controller)
	PIC8259(config, m_pic, 0);
	m_pic->out_int_callback().set_inputline(m_maincpu, 0);

	// CXQ71051P at IC53 (Serial Interface Unit)
	I8251(config, m_serial1, 8_MHz_XTAL);
	m_serial1->txd_handler().set(m_cpanel_serial, FUNC(i8251_device::write_rxd));
	m_serial1->txrdy_handler().set(m_pic, FUNC(pic8259_device::ir7_w));

	// CXQ71051P at IC54 (Serial Interface Unit)
	I8251(config, m_serial2, 8_MHz_XTAL);
	m_serial2->txrdy_handler().set(m_pic, FUNC(pic8259_device::ir6_w));
	// FIXME: Declare an interface to hook this up to another emulated device in MAME (such as pve500)
	//
	// This goes to the CN21 EDITOR D-SUB CONNECTOR on board CN-573
	// I think the purpose of this is to connect to an editor such as the Sony PVE-500.
	//
	// m_serial2->txd_handler().set(m_..., FUNC(..._device::write_txd)); "XMIT"
	// m_...->rxd_handler().set(m_serial2, FUNC(i8251_device::write_rxd)); "RCV"

	GENERIC_LATCH_16(config, "controlsignals");
	GENERIC_LATCH_16(config, "reg7_5");
	GENERIC_LATCH_16(config, "xflt");
	GENERIC_LATCH_16(config, "yflt");
}

ROM_START(dfs500)
	// Process Unit System Control:
	ROM_REGION(0x8000, "cpanelcpu", 0)
	ROM_LOAD("27c256b_npky14_v1.03_293-83_5500_ky223_sony94.ic14", 0x0000, 0x8000, CRC(8b9e564a) SHA1(aa8a1f211a7834fb15f7ecbc58570f566c0ef5ab))

	// Process Unit System Control:
	ROM_REGION(0x10000, "maincpu", 0)
	ROM_LOAD16_BYTE("27c256b_npsys1_v1.03_293-84_3eb5_sy172_sony94.ic1", 0x0001, 0x8000, CRC(4604e7c0) SHA1(80f965b69a163a6278d6f54db741f4c5ada1cb59))
	ROM_LOAD16_BYTE("27c256b_npsys2_v1.03_293-85_3ecd_sy172_sony94.ic2", 0x0000, 0x8000, CRC(b80a66e6) SHA1(407ddc5fee61920bfbe90c20faf4482ceef1ad4f))

	// Process Unit Effect Control:
	ROM_REGION(0x20000, "effectcpu", 0)
	ROM_LOAD16_BYTE("27c512_npsys3_v1.04_293-86_b7d0_sy172_sony94.ic3", 0x0001, 0x10000, CRC(69238d02) SHA1(288babc7547858a3ca3f65af0be76f72335392ea))
	ROM_LOAD16_BYTE("27c512_npsys4_v1.04_293-87_b771_sy172_sony94.ic4", 0x0000, 0x10000, CRC(541abd4f) SHA1(e51f5ca6416c17535f2d2a13a7bedfb3b4b4a58b))

	// Process Unit Effect Data:
	ROM_REGION(0x200000, "effectdata", 0)
	ROM_LOAD("27c4001-12f1_sy172_v1.01_d216.ic5", 0x000000,  0x80000, CRC(ae094fcb) SHA1(c29c27b3c80e67caba2078bb60696c1b8692eb8b))
	ROM_LOAD("27c4001-12f1_sy172_v1.01_d225.ic6", 0x080000,  0x80000, CRC(caa6ccb2) SHA1(9b72dc47cf4cc9c2f9915ea4f1bd7b5136e29db5))
	ROM_LOAD("27c4001-12f1_sy172_v1.01_cc13.ic7", 0x100000,  0x80000, CRC(e1fe8606) SHA1(a573c7023daeb84d5a1182db4051b1bccfcfc1f8))
	ROM_LOAD("27c4001-12f1_sy172_v1.01_c42d.ic8", 0x180000,  0x80000, CRC(66e0f20f) SHA1(e82562ae1eeecc5c97b0f40e01102c2ebe0d6276))
ROM_END

//   YEAR  NAME   PARENT/COMPAT MACHINE  INPUT    CLASS          INIT     COMPANY  FULLNAME                     FLAGS
SYST(1994, dfs500,    0, 0,     dfs500, dfs500, dfs500_state, empty_init, "Sony", "DFS-500 DME Video Switcher", MACHINE_IS_SKELETON)
