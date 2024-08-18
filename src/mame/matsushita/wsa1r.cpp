// license:GPL2+
// copyright-holders:Felipe Sanches
/******************************************************************************

	Technics sx-WSA1R synth driver

*******************************************************************************

	Tips:
	Technics SX-WSA1 RESET  	Turn the power off. 	Press and hold the "Real time Creator" [1], [2] and [3] buttons while turning on the power.  	Result: All 'System Type' memory banks will be restored to the factory settings.
	 	Note: The "Sound" and "Combination" user data will not be affected.  
	To restore the factory "Sound" and "Combination" banks you need to load the 
	SOUND/COMBI data from the accessory disk.

******************************************************************************/

#include "emu.h"
#include "cpu/tlcs900/tmp95c061.h"
#include "imagedev/floppy.h"
#include "machine/gen_latch.h"
#include "machine/upd765.h"
#include "video/sed1330.h"
#include "emupal.h"
#include "screen.h"
#include "wsa1r.lh"

namespace {

class wsa1r_state : public driver_device
{
public:
	wsa1r_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_subcpu(*this, "subcpu")
		, m_maincpu_latch(*this, "maincpu_latch")
		, m_subcpu_latch(*this, "subcpu_latch")
		, m_fdc(*this, "fdc")
		, m_lcdc(*this, "lcdc")
		, m_SEG(*this, "SEG%u", 0U)
		, m_checking_device_led(*this, "checking_device_led")
		, m_mstat(0)
		, m_sstat(0)
	{ }

	void wsa1r(machine_config &config);

private:
	required_device<tmp95c061_device> m_maincpu;
	required_device<tmp95c061_device> m_subcpu;
	required_device<generic_latch_8_device> m_maincpu_latch;
	required_device<generic_latch_8_device> m_subcpu_latch;
	required_device<upd72067_device> m_fdc;
	required_device<sed1330_device> m_lcdc;

	required_ioport_array<11> m_SEG; // buttons on Control Panel PCB
	output_finder<> m_checking_device_led;

	uint8_t m_mstat;
	uint8_t m_sstat;

	virtual void machine_start() override;
	virtual void machine_reset() override;

	//TODO: uint8_t cpanel_buttons_r(offs_t offset);

	void lcdc_map(address_map &map);
	void maincpu_mem(address_map &map);
	void subcpu_mem(address_map &map);
//	uint16_t tone_generator_r(offs_t offset);
//	void tone_generator_w(offs_t offset, uint16_t data);
};

/*
==== MAIN CPU ====
CS0:   HDCS, MIF, FDCS/FDDAK, LCDCS
    B0CS=0x14     B0E = B0CS master bit  B0BUS = 8 bit  2 wait
          bits:  23 20   16    12   9 8    4    0
    MSAR0=0x78    0111 1000 x[xxx xxx]x xxxx xxxx   base-addr: 0x780000
    MAMR0=0x3f       0 0111 1[111 111]1                  mask: 0x07ffff
                        001                 LCDCS  0x790000
                        010                 FDDAK  0x7A0000
                        011                  FDCS  0x7B0000
                        100                   MIF  0x7C0000
                        110                  HDCS  0x7E0000

CS1:  STATIC RAM
	B1CS=0x17     B1E = B1CS master bit  B1BUS = 8 bit  0 wait
          bits:  23 20   16    12   9 8    4    0
	MSAR1=0x00    0000 0000 [xxxx xxx]x xxxx xxxx   base-addr: 0x000000
	MAMR1=0x7f      00 1111 [1111 111]1                  mask: 0x0fffff
                     0 0                 RAMCS  0x000000

CS2:  
	B2CS=0x1b     B2E = B2CS master bit  B2M = "Set MREG"  B2BUS = 16 bit  0 wait
	MSAR2=0xe0
	MAMR2=0x3f

DRAM:
	B3CS=0x19     B3E = B3CS master bit  B3CAS = "/CAS output"  B3BUS = 16 bit  1 wait
	MSAR3=0x60
	MAMR3=0x0f
*/
void wsa1r_state::maincpu_mem(address_map &map)
{
	//TODO: map(0x?, 0x?).r(FUNC(wsa1r_state::cpanel_buttons_r));
	
	map(0x000000, 0x007fff).ram(); // 256Kbit SRAM @ IC14 
	map(0x600000, 0x67ffff).ram(); // 4Mbit DRAM @ IC15
	map(0x790000, 0x790000).rw(m_lcdc, FUNC(sed1330_device::status_r), FUNC(sed1330_device::data_w));
	map(0x790001, 0x790001).rw(m_lcdc, FUNC(sed1330_device::data_r), FUNC(sed1330_device::command_w));
	map(0x7a0000, 0x7a0000).rw(m_fdc, FUNC(upd72067_device::dma_r), FUNC(upd72067_device::dma_w)); // FIXME: Is this correct?
	map(0x7b0000, 0x7b0003).m(m_fdc, FUNC(upd72067_device::map)); // Floppy Controller
	map(0x7c0000, 0x7c0000).r(m_maincpu_latch, FUNC(generic_latch_8_device::read));
	map(0x7c0000, 0x7c0000).w(m_subcpu_latch, FUNC(generic_latch_8_device::write));
	//map(0x7e0000, 0x7e000?).m(...); // TODO: Something Optional?
	map(0x7f0000, 0x7f0003).nopw(); // FIXME: rom-code writes here, but I have no idea what this is.
	map(0xf00000, 0xf7ffff).rom().region("promb", 0);
	map(0xf80000, 0xffffff).rom().region("proma", 0);
}

/*
==== SUB CPU ====
CS0:   SMIF, WFICS, KSCS, SGCS
    B0CS=0x10     B0E = B0CS master bit  B0BUS = 8 bit  2 wait
          bits:  23 20   16    12   9 8    4    0
    MSAR0=0x10    0001 0000 x[xxx xxx]x xxxx xxxx   base-addr: 0x100000
    MAMR0=0x07       0 0000 1[111 111]1                  mask: 0x00ffff
                            0 0                  SMIF  0x100000 (inter-cpu comm latches)
                            0 1                 WFICS  0x104000 (Modeling LSI)
                            1 0                  KSCS  0x108000 (Tone Generator LSI - guess:"key scan")
                            1 1                  SGCS  0x10c000 (Tone Generator LSI - guess:"sound gen")

CS1:  ?
	B1CS=0x14     B1E = B1CS master bit  B1BUS = 8 bit  0 wait
          bits:  23 20   16    12   9 8    4    0
	MSAR1=0xc0    1100 0000 [xxxx xxx]x xxxx xxxx   base-addr: 0xc00000
	MAMR1=0x7f      00 1111 [1111 111]1                  mask: 0x0fffff

CS2:  ROMs & FLASH
	B2CS=0x1b     B2E = B2CS master bit  B2M = "Set MREG"  B2BUS = 16 bit  0 wait
	MSAR2=0xe0
	MAMR2=0x3f

DRAM:
	B3CS=0x1b     B3E = B3CS master bit  B3CAS = "/CAS output"  B3BUS = 16 bit  1 wait
	MSAR3=0x00
	MAMR3=0x03
*/
void wsa1r_state::subcpu_mem(address_map &map)
{
	map(0x000000, 0x01ffff).ram(); // 1Mbit DRAM @ IC23
	map(0x100000, 0x100000).r(m_subcpu_latch, FUNC(generic_latch_8_device::read));
	map(0x100000, 0x100000).w(m_maincpu_latch, FUNC(generic_latch_8_device::write));
	map(0x104000, 0x104003).noprw(); // WFICS: Modeling LSI
	//map(0x108000, 0x108003).noprw(); // KSCS: Tone Generator LSI - guess:"key scan"
	map(0x10c000, 0x10c003).noprw(); // SGCS: Tone Generator LSI - guess:"sound gen"
	//-----map(0xc00000, 0xc00000).nopr(); //.lr8(NAME([this] { return 0xFF; })); // (/CS1) Is this what distinguishes WSA1 frmo WSA1R?
	map(0xe00000, 0xe00003).ram(); // <-- No idea what this is.
	map(0xe80000, 0xefffff).ram(); // FLASH
	map(0xf00000, 0xf7ffff).rom().region("promd", 0);
	map(0xf80000, 0xffffff).rom().region("promc", 0);
}

void wsa1r_state::lcdc_map(address_map &map)
{
	map.global_mask(0x7fff);
	map(0x0000, 0x7fff).ram();
}

static void wsa1r_floppies(device_slot_interface &device)
{
	device.option_add("35dd", FLOPPY_35_DD);
}

static INPUT_PORTS_START(wsa1r)
	PORT_START("CN4")
	PORT_DIPNAME(0x10, 0x00, "Checking Device")
	PORT_DIPSETTING(   0x00, DEF_STR(On))
	PORT_DIPSETTING(   0x10, DEF_STR(Off))

	PORT_START("SEG0")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SOUND") // PLAY MODE
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("COMBI") // PLAY MODE
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SOUND") // EDIT MODE
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("COMBI") // EDIT MODE
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("BANK 1") // BANK
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("BANK 2") // BANK
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("BANK 3") // BANK
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RE-MAP")

	PORT_START("SEG1")
	// SOUND COMBINATION GROUPS 1-8 (SCG)
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SCG 1")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SCG 2")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SCG 3")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SCG 4")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SCG 5")
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SCG 6")
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SCG 7")
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SCG 8")

	PORT_START("SEG2")
	// SOUND COMBINATION GROUPS 9-16 (SCG)
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SCG 9")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SCG 10")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SCG 11")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SCG 12")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SCG 13")
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SCG 14")
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SCG 15")
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SCG 16")

	PORT_START("SEG3")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN 5")  // SW25
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP-UP 5") // SW26
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN 6")  // SW27
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP-UP 6") // SW28
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN 7")  // SW29
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP-UP 7") // SW30
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN 8")  // SW31
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP-UP 8") // SW32

	PORT_START("SEG4")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN-DOWN 5") // SW33
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP 5")        // SW34
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN-DOWN 6") // SW35
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP 6")        // SW36
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN-DOWN 7") // SW37
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP 7")        // SW38
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN-DOWN 8") // SW39
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP 8")        // SW40

	PORT_START("SEG5")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN 1")  // SW41
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP-UP 1") // SW42
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN 2")  // SW43
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP-UP 2") // SW44
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN 3")  // SW45
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP-UP 3") // SW46
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN 4")  // SW47
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP-UP 4") // SW48

	PORT_START("SEG6")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN-DOWN 1") // SW49
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP 1")        // SW50
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN-DOWN 2") // SW51
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP 2")        // SW52
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN-DOWN 3") // SW53
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP 3")        // SW54
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN-DOWN 4") // SW55
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP 4")        // SW56

	PORT_START("SEG7")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("PART")   // MENU
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SYSTEM") // MENU
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MIDI")   // MENU
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DISK")   // MENU
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("ON/OFF")     // SEQUENCER
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MENU")       // SEQUENCER
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RESET")      // SEQUENCER
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("START/STOP") // SEQUENCER

	PORT_START("SEG8")	// REAL TIME CREATER 1-6 (RTC)
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RTC 1")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RTC 2")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RTC 3")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RTC 4")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RTC 5")
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RTC 6")
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RESET")
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("SEG9")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("LEFT 1")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("LEFT 2")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("LEFT 3")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("LEFT 4")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("LEFT 5")
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("COMPARE")
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("PAGE DOWN")
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("PAGE UP")

	PORT_START("SEG10")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RIGHT 1")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RIGHT 2")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RIGHT 3")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RIGHT 4")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RIGHT 5")
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("-1")
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("+1")
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("EXIT")
INPUT_PORTS_END

//TODO:
//uint8_t wsa1r_state::cpanel_buttons_r(offs_t offset)
//{
//	return m_SEG[offset]->read();
//}

void wsa1r_state::machine_start()
{
	save_item(NAME(m_mstat));
	save_item(NAME(m_sstat));

	m_checking_device_led.resolve();
}

void wsa1r_state::machine_reset()
{
	m_checking_device_led = 0;
}

void wsa1r_state::wsa1r(machine_config &config)
{
	TMP95C061(config, m_maincpu, 28_MHz_XTAL);
	//TODO: AM16 = GND (meaning ...?)
	m_maincpu->set_addrmap(AS_PROGRAM, &wsa1r_state::maincpu_mem);
	//TODO: /NMI:        SNS
	//      Interrupt 0: SIFINT "sound interface interrupt" (receiving data from subcpu)
	//TODO: Interrupt 4: HDINT
	//      Interrupt 5: FDINT
	//TODO: Interrupt 6: ?
	//      Interrupt 7: FDDIRQ
	//TODO: TXD0:        MIDI1OUT
	//TODO: RXD0:        MIDI1IN
	//TODO: TXD1:        control panel
	//TODO: RXD1:        control panel
	//TODO: AN0:         STCKX   "stick x"
	//TODO: AN1:         STCKY   "stick y"
	//TODO: AN2:         AFTTCH  "after touch"
	//TODO: AN3:         FPDL    "foot pedal"

	// MAINCPU PORT 5:
	//   bit 4 (input) = "check terminal" switch
	//   bit 3 (output) = "check terminal" LED
	m_maincpu->port5_read().set([this] { return ioport("CN4")->read(); });
	m_maincpu->port5_write().set([this] (u8 data) { m_checking_device_led = (BIT(data, 3) == 0); });

	// P65:         "reset?!"
	
	// MAINCPU PORT 7:
	//   bit 0 = (output) MSTAT0
	//   bit 1 = (output) MSTAT1
	//   bit 2 = (input) SSTAT0
	//   bit 3 = (input) SSTAT1
	//TODO:   bit 4 = (output) LCDOFF
	//TODO:   bit 5 = (output?) MUTE
	//TODO:   bit 6 = (input) "foot switch #1"
	//TODO:   bit 7 = (input) "foot switch #2"
	m_maincpu->port7_read().set([this] {
		return m_sstat << 2;
	});
	m_maincpu->port7_write().set([this] (u8 data) {
		m_mstat = data & 3;
	});

	// MAINCPU PORT 8:
	//TODO:   bit 2:  MIDI1SNS

	// MAINCPU PORT A:
	//TODO: PA1:         FS1 (optional "SY" board)
	//TODO: PA2:         FS2 (optional "SY" board)
	//TODO: PA3:         FDMON

	// MAINCPU PORT B:
	//    bit 2:         FDRST
	//TODO: PB3:         FDTC
	//TODO: PB6:         HDIORDY
	m_maincpu->portb_write().set([this] (u8 data) { m_fdc->reset_w(BIT(data, 2)); });


	TMP95C061(config, m_subcpu, 28_MHz_XTAL);
	//TODO: AM16 = GND (meaning ...?)
	m_subcpu->set_addrmap(AS_PROGRAM, &wsa1r_state::subcpu_mem);
	//TODO: /NMI:        ?
	//      Interrupt 0: IFINT "interface interrupt" (receiving data from maincpu)
	//TODO: Interrupt 4: KBEVT
	//TODO: TXD0:        MIDI2OUT
	//TODO: RXD0:        MIDI2IN
	//TODO: AN0:         +5D
	//TODO: AN1:         JOYX
	//TODO: AN2:         JOYY

	// SUBCPU PORT 2:
	//TODO:    bit 6 = (?) EXRW
	//TODO:    bit 7 = (output) DSP2CS

	// SUBCPU PORT 5:
	//TODO:    bit 3 = (?) DSPCD
	//TODO:    bit 4 = (output) DSP0CS
	//TODO:    bit 5 = (output) DSP1CS

	// SUBCPU PORT 6:
	//TODO:    bit 5 = (output) EEPCS

	// SUBCPU PORT 7:
	//TODO:    bits 0-7 = (bidi?) DSPD0-7

	// SUBCPU PORT 8:
	//TODO:    bit 2 = (?) MIDI2SNS
	//TODO:    bit 3 = (?) EEPSK
	//TODO:    bit 4 = (?) EEPDI
	//TODO:    bit 5 = (?) EEPDO

	// SUBCPU PORT 9:
	//TODO:    bit 3 = (input) DSPRDY

	// SUBCPU PORT A:
	//   bit 0 = (output) SSTAT0
	//   bit 1 = (output) SSTAT1
	//   bit 2 = (input) MSTAT0
	//   bit 3 = (input) MSTAT1
	m_subcpu->porta_read().set([this] {
		return m_mstat << 2;
	});
	m_subcpu->porta_write().set([this] (u8 data) {
		m_sstat = data & 3;
	});

	// SUBCPU PORT B:
	//   bit 0 = unused.
	//TODO:   bit 1 = (output) DSP2RST2
	//TODO:   bit 2 = (output) DSPRST
	//TODO:   bit 3 = (output) DSP0RST2
	//TODO:   bit 4 = (output) DSP1RST2
	//TODO:   bit 5 = (output) DSPWR
	//TODO:   bit 6 = (output) DSPRD
	//   bit 7 = unused. 


	GENERIC_LATCH_8(config, m_maincpu_latch);
	m_maincpu_latch->data_pending_callback().set_inputline(m_maincpu, TLCS900_INT0);

	GENERIC_LATCH_8(config, m_subcpu_latch);
	m_subcpu_latch->data_pending_callback().set_inputline(m_subcpu, TLCS900_INT0);

	UPD72067(config, m_fdc, 24'000'000); // actual controller is UPD72070GF-3BE
	m_fdc->intrq_wr_callback().set_inputline(m_maincpu, TLCS900_INT5); // FIXME: this may be wrong
	m_fdc->drq_wr_callback().set_inputline(m_maincpu, TLCS900_INT7); // FIXME: this may be wrong
	//review: m_fdc->hdl_wr_callback().set_inputline(m_maincpu, TLCS900_INT?);
	//review: m_fdc->??_wr_callback().set_inputline(m_maincpu, TLCS900_INT?);

	FLOPPY_CONNECTOR(config, "fdc:0", wsa1r_floppies, "35dd", floppy_image_device::default_mfm_floppy_formats).enable_sound(true);

	/* sound hardware */
	//SPEAKER(config, "lspeaker").front_left();
	//SPEAKER(config, "rspeaker").front_right();

	/* video hardware */
	auto &palette = PALETTE(config, "palette", palette_device::MONOCHROME_INVERTED);

	auto &screen = SCREEN(config, "screen", SCREEN_TYPE_LCD);
	screen.set_refresh_hz(60);
	screen.set_screen_update(m_lcdc, FUNC(sed1330_device::screen_update));
	screen.set_size(320, 240);
	screen.set_visarea(0, 320-1, 0, 240-1);
	screen.set_palette(palette);

	SED1330(config, m_lcdc, 8_MHz_XTAL);
	m_lcdc->set_screen("screen");
	m_lcdc->set_addrmap(0, &wsa1r_state::lcdc_map);

	config.set_default_layout(layout_wsa1r);
}

ROM_START(wsa1r)
	ROM_REGION16_LE( 0x800000, "proma", 0 )
	ROM_LOAD("technics_wsa1_wsa1r_qsigcwsa1ax_a_2.ic12", 0x00000, 0x80000, CRC(5f34af46) SHA1(90a2369f8e4d2fcdf26875272267624b07bc200d))

	ROM_REGION16_LE( 0x800000, "promb", 0 )
	ROM_LOAD("technics_wsa1_wsa1r_qsigcwsa1bx_b_2.ic13", 0x00000, 0x80000, CRC(f3f84441) SHA1(93adec2a04b7d93a2ec2bfb059227ff3959906e0))

	ROM_REGION16_LE( 0x800000, "promc", 0 )
	ROM_LOAD("technics_wsa1_wsa1r_qsigcwsa1cx_c_2.ic28", 0x00000, 0x80000, CRC(855c8ac4) SHA1(9b2911e4b21a08d9744b91844630489f54dde856))

	ROM_REGION16_LE( 0x800000, "promd", 0 )
	ROM_LOAD("technics_wsa1_wsa1r_qsigcwsa1dx_d_2.ic21", 0x00000, 0x80000, CRC(735ae465) SHA1(82df50816c20cd8f2d29551326d2633e7791f306))
ROM_END

} // anonymous namespace

//   YEAR  NAME   PARENT  COMPAT  MACHINE INPUT   STATE         INIT        COMPANY      FULLNAME    FLAGS
CONS(1995, wsa1r,    0,       0,  wsa1r,  wsa1r,  wsa1r_state,  empty_init, "Technics",  "SX-WSA1R", MACHINE_NOT_WORKING|MACHINE_NO_SOUND)
