// license:GPL2+
// copyright-holders:Felipe Sanches
/******************************************************************************

    Technics SX-KN5000 music keyboard driver

******************************************************************************/

#include "emu.h"
#include <queue>
#include "bus/technics/kn5000/hdae5000.h"
#include "bus/midi/midi.h"
#include "cpu/tlcs900/tmp94c241.h"
#include "imagedev/floppy.h"
#include "machine/gen_latch.h"
#include "machine/nvram.h"
#include "machine/upd765.h"
#include "video/pc_vga.h"
#include "screen.h"
#include "kn5000.lh"
#include "kn5000_cpanel.h"

class mn89304_vga_device : public svga_device
{
public:
	// construction/destruction
	mn89304_vga_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

protected:
	virtual void device_reset() override ATTR_COLD;

	virtual void palette_update() override;
	virtual void recompute_params() override;
	virtual uint16_t offset() override;
};

DEFINE_DEVICE_TYPE(MN89304_VGA, mn89304_vga_device, "mn89304_vga", "MN89304 VGA")

// TODO: nothing is known about this, configured out of usage in here for now.
mn89304_vga_device::mn89304_vga_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: svga_device(mconfig, MN89304_VGA, tag, owner, clock)
{
	// ...
}

void mn89304_vga_device::device_reset()
{
	svga_device::device_reset();
	svga.rgb8_en = 1;
}

// sets up mode 0, by default it will throw 155 Hz, assume divided by 3
void mn89304_vga_device::recompute_params()
{
	u8 xtal_select = (vga.miscellaneous_output & 0x0c) >> 2;
	int xtal;

	switch(xtal_select & 3)
	{
		case 0: xtal = XTAL(25'174'800).value() / 3; break;
		case 1: xtal = XTAL(28'636'363).value() / 3; break;
		case 2:
		default:
			throw emu_fatalerror("MN89304: setup ext. clock select");
	}

	recompute_params_clock(1, xtal);
}


void mn89304_vga_device::palette_update()
{
	// 4bpp RAMDAC
	for (int i = 0; i < 256; i++)
	{
		set_pen_color(
			i,
			pal4bit(vga.dac.color[3*(i & vga.dac.mask) + 0]),
			pal4bit(vga.dac.color[3*(i & vga.dac.mask) + 1]),
			pal4bit(vga.dac.color[3*(i & vga.dac.mask) + 2])
		);
	}
}

uint16_t mn89304_vga_device::offset()
{
	return svga_device::offset() << 3;
}


namespace {

// Logging macros for inter-CPU communication debugging
#define LOG_LATCH    (1U << 1)  // Latch read/write (command bytes only)
#define LOG_LATCH_DATA (1U << 2) // Latch read/write (all data bytes - very verbose)
#define LOG_HANDSHAKE (1U << 3) // MSTAT/SSTAT handshake changes
#define LOG_RESET    (1U << 4)  // Sub CPU reset control
#define LOG_KEYBED   (1U << 5)  // Tone generator keybed HLE events
#define LOG_ALL_LATCH (LOG_LATCH | LOG_LATCH_DATA)

#define VERBOSE (LOG_LATCH | LOG_RESET)
#include "logmacro.h"

class kn5000_state : public driver_device
{
public:
	kn5000_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_cpanel(*this, "cpanel")
		, m_maincpu(*this, "maincpu")
		, m_subcpu(*this, "subcpu")
		, m_maincpu_latch(*this, "maincpu_latch")
		, m_subcpu_latch(*this, "subcpu_latch")
		, m_fdc(*this, "fdc")
		, m_com_select(*this, "COM_SELECT")
		, m_extension(*this, "extension")
		, m_CPL_SEG(*this, "CPL_SEG%u", 0U)
		, m_CPR_SEG(*this, "CPR_SEG%u", 0U)
		, m_keybed(*this, "KEY%u", 0U)
		, m_checking_device_led_cn11(*this, "checking_device_led_cn11")
		, m_checking_device_led_cn12(*this, "checking_device_led_cn12")
		, m_mstat(0)
		, m_sstat(0)
		, m_cpanel_inta(0)
		, m_subcpu_latch_write_count(0)
		, m_maincpu_latch_write_count(0)
	{ }

	void kn5000(machine_config &config);

private:
	required_device<kn5000_cpanel_device> m_cpanel;
	required_device<tmp94c241_device> m_maincpu;
	required_device<tmp94c241_device> m_subcpu;
	required_device<generic_latch_8_device> m_maincpu_latch;
	required_device<generic_latch_8_device> m_subcpu_latch;
	required_device<upd72067_device> m_fdc;
	required_ioport m_com_select;
	required_device<kn5000_extension_connector> m_extension;

	required_ioport_array<11> m_CPL_SEG; // buttons on "Control Panel Left" PCB
	required_ioport_array<11> m_CPR_SEG; // buttons on "Control Panel Right" PCB
	required_ioport_array<6> m_keybed;   // 61-key keyboard (6 ports x 12 bits, last port 1 key)
	output_finder<> m_checking_device_led_cn11;
	output_finder<> m_checking_device_led_cn12;
	uint8_t m_mstat;
	uint8_t m_sstat;
	uint8_t m_cpanel_inta;
	uint32_t m_subcpu_latch_write_count;
	uint32_t m_maincpu_latch_write_count;
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

	// Latch logging wrappers
	void subcpu_latch_w(uint8_t data);
	void maincpu_latch_w(uint8_t data);

	// Tone generator keybed HLE
	uint16_t tonegen_status_r();
	uint16_t tonegen_data_r();
	struct keybed_event { uint16_t data; };
	std::queue<keybed_event> m_keybed_queue;
	uint8_t m_keybed_prev[61];
	emu_timer *m_keybed_timer;
	TIMER_CALLBACK_MEMBER(keybed_scan);
	static constexpr uint8_t KEYBED_VELOCITY = 100; // fixed velocity for PC keyboard

	void nvram2_init(nvram_device &device, void *data, size_t size);
	void maincpu_mem(address_map &map) ATTR_COLD;
	void subcpu_mem(address_map &map) ATTR_COLD;
};

void kn5000_state::subcpu_latch_w(uint8_t data)
{
	m_subcpu_latch_write_count++;
	// Log command bytes (E1, E2, E3) and first few data bytes
	if (data == 0xe1 || data == 0xe2 || data == 0xe3)
		LOGMASKED(LOG_LATCH, "MainCPU -> SubCPU latch: cmd 0x%02X (write #%u) PC=%06X\n",
			data, m_subcpu_latch_write_count, m_maincpu->pc());
	else
		LOGMASKED(LOG_LATCH_DATA, "MainCPU -> SubCPU latch: 0x%02X (write #%u)\n",
			data, m_subcpu_latch_write_count);

	// Force tight CPU interleaving so subcpu HDMA can process each byte
	// before the next one is written. On real hardware, HDMA steals cycles
	// between main CPU instructions.
	machine().scheduler().perfect_quantum(attotime::from_usec(100));

	m_subcpu_latch->write(data);
}

void kn5000_state::maincpu_latch_w(uint8_t data)
{
	m_maincpu_latch_write_count++;
	if (data == 0xe1 || data == 0xe2 || data == 0xe3)
		LOGMASKED(LOG_LATCH, "SubCPU -> MainCPU latch: cmd 0x%02X (write #%u) PC=%06X\n",
			data, m_maincpu_latch_write_count, m_subcpu->pc());
	else
		LOGMASKED(LOG_LATCH_DATA, "SubCPU -> MainCPU latch: 0x%02X (write #%u)\n",
			data, m_maincpu_latch_write_count);
	m_maincpu_latch->write(data);
}

// Tone generator keybed HLE: status register at 0x110002
// Bit 0 = data ready (queue non-empty), Bit 1 = 0 (note-on context)
uint16_t kn5000_state::tonegen_status_r()
{
	return m_keybed_queue.empty() ? 0x0000 : 0x0001;
}

// Tone generator keybed HLE: data register at 0x110000
// Returns 16-bit word: low byte = raw note (bit 7 = has velocity), high byte = velocity
uint16_t kn5000_state::tonegen_data_r()
{
	if (m_keybed_queue.empty())
		return 0x0000;

	keybed_event ev = m_keybed_queue.front();
	m_keybed_queue.pop();
	return ev.data;
}

// Scan PC keyboard input ports and generate note-on/note-off events
// Called every 1ms by timer, matching real IC303 hardware scan rate
TIMER_CALLBACK_MEMBER(kn5000_state::keybed_scan)
{
	for (int port = 0; port < 6; port++)
	{
		uint16_t keys = m_keybed[port]->read();
		int num_keys = (port < 5) ? 12 : 1; // last port has only 1 key (C7)

		for (int bit = 0; bit < num_keys; bit++)
		{
			int raw_note = port * 12 + bit;
			uint8_t pressed = (keys >> bit) & 1;
			uint8_t prev = m_keybed_prev[raw_note];

			if (pressed && !prev)
			{
				// Key pressed: data = (velocity << 8) | (raw_note | 0x80)
				uint16_t data = (uint16_t(KEYBED_VELOCITY) << 8) | (raw_note | 0x80);
				m_keybed_queue.push({data});
				LOGMASKED(LOG_KEYBED, "Keybed: note ON raw=%d MIDI=%d vel=%d data=0x%04X\n",
					raw_note, raw_note + 0x24, KEYBED_VELOCITY, data);
			}
			else if (!pressed && prev)
			{
				// Key released: data = (0xFF << 8) | raw_note
				uint16_t data = (0xFF00) | raw_note;
				m_keybed_queue.push({data});
				LOGMASKED(LOG_KEYBED, "Keybed: note OFF raw=%d MIDI=%d data=0x%04X\n",
					raw_note, raw_note + 0x24, data);
			}
			m_keybed_prev[raw_note] = pressed;
		}
	}
}

void kn5000_state::maincpu_mem(address_map &map)
{
	map(0x000000, 0x0fffff).ram().share("nvram1"); // 1Mbyte = 2 * 4Mbit DRAMs @ IC9, IC10 (CS3)
	// Button states and LED control are now handled via serial protocol to cpanel HLE device
	//FIXME: map(0x110000, 0x11ffff).m(m_fdc, FUNC(upd765a_device::map)); // Floppy Controller @ IC208
	//FIXME: map(0x120000, 0x12ffff).w(m_fdc, FUNC(upd765a_device::dack_w)); // Floppy DMA Acknowledge
	map(0x140000, 0x14ffff).r(m_maincpu_latch, FUNC(generic_latch_8_device::read)); // @ IC23
	map(0x140000, 0x14ffff).w(FUNC(kn5000_state::subcpu_latch_w)); // @ IC22 (logged wrapper)
	map(0x1703b0, 0x1703df).m("vga", FUNC(mn89304_vga_device::io_map)); // LCD controller @ IC206
	map(0x1a0000, 0x1dffff).rw("vga", FUNC(mn89304_vga_device::mem_linear_r), FUNC(mn89304_vga_device::mem_linear_w));
	map(0x1e0000, 0x1fffff).ram().share("nvram2"); // 1Mbit SRAM @ IC21 (CS0)  Note: I think this is the message "ERROR in back-up SRAM"
	map(0x300000, 0x3fffff).rom().region("custom_data", 0); // 8MBit FLASH ROM @ IC19 (CS5)
	map(0x400000, 0x7fffff).rom().region("rhythm_data", 0); // 32MBit ROM @ IC14 (A22=1 and CS5)
	// The subcpu payload is stored compressed in IC19 flash at 0x3E0000, which is part of the "custom_data" region above.
	map(0x800000, 0x9fffff).mirror(0x200000).rom().region("table_data", 0); //2 * 8MBit ROMs @ IC1, IC3 (CS2)
	map(0xe00000, 0xffffff).mask(0x1fffff).rom().region("program", 0); //2 * 8MBit FLASH ROMs @ IC4, IC6
}

void kn5000_state::subcpu_mem(address_map &map)
{
	map(0x000000, 0x0fffff).ram(); // 1Mbyte = 2 * 4Mbit DRAMs @ IC28, IC29
	map(0x100000, 0x100003).noprw(); // Tone gen register config (write-only; writes harmlessly discarded)
	map(0x110000, 0x110001).r(FUNC(kn5000_state::tonegen_data_r));   // Tone gen keybed data (HLE)
	map(0x110002, 0x110003).r(FUNC(kn5000_state::tonegen_status_r)); // Tone gen keybed status (HLE)
	map(0x120000, 0x12ffff).r(m_subcpu_latch, FUNC(generic_latch_8_device::read)); // @ IC22
	map(0x120000, 0x12ffff).w(FUNC(kn5000_state::maincpu_latch_w)); // @ IC23 (logged wrapper)
	map(0x130000, 0x130003).noprw(); // DSP1 @ IC311 (stub - not yet emulated)
	map(0x1e0000, 0x1effff).noprw(); // Waveform/sample RAM (stub - not yet emulated)
	map(0xfe0000, 0xffffff).rom().region("subcpu", 0); // 1Mbit MASK ROM @ IC30

	// DSP2 @ IC302 uses serial #0 pins (bitbanging)
}

static void kn5000_floppies(device_slot_interface &device)
{
	device.option_add("35dd", FLOPPY_35_DD);
}

static INPUT_PORTS_START(kn5000)
	PORT_START("CN11")
	PORT_DIPNAME(0x01, 0x01, "Main CPU Checking Device")
	PORT_DIPSETTING(   0x00, DEF_STR(On))
	PORT_DIPSETTING(   0x01, DEF_STR(Off))

	PORT_START("CN12")
	PORT_DIPNAME(0x01, 0x01, "Sub CPU Checking Device")
	PORT_DIPSETTING(   0x00, DEF_STR(On))
	PORT_DIPSETTING(   0x01, DEF_STR(Off))

	PORT_START("COM_SELECT")
	PORT_DIPNAME(0xf0, 0xe0, "Computer Interface Selection")
	PORT_DIPSETTING(   0xe0, "MIDI")
	PORT_DIPSETTING(   0xd0, "PC1")
	PORT_DIPSETTING(   0xb0, "PC2")
	PORT_DIPSETTING(   0x70, "Mac")

	PORT_START("AREA")
	PORT_DIPNAME(0x06, 0x06, "Area Selection")
	PORT_DIPSETTING(   0x02, "Thailand, Indonesia, Iran, U.A.E., Panama, Argentina, Peru, Brazil")
	PORT_DIPSETTING(   0x04, "USA, Mexico")
	PORT_DIPSETTING(   0x06, "Other")

/*
    Actual full list of regions (but it is unclear if there's any
    other hardware difference among them):

    PORT_DIPSETTING(   0x04, "(M): U.S.A.")
    PORT_DIPSETTING(   0x06, "(MC): Canada")
    PORT_DIPSETTING(   0x04, "(XM): Mexico")
    PORT_DIPSETTING(   0x06, "(EN): Norway, Sweden, Denmark, Finland")
    PORT_DIPSETTING(   0x06, "(EH): Holland, Belgium")
    PORT_DIPSETTING(   0x06, "(EF): France, Italy")
    PORT_DIPSETTING(   0x06, "(EZ): Germany")
    PORT_DIPSETTING(   0x06, "(EW): Switzerland")
    PORT_DIPSETTING(   0x06, "(EA): Austria")
    PORT_DIPSETTING(   0x06, "(EP): Spain, Portugal, Greece, South Africa")
    PORT_DIPSETTING(   0x06, "(EK): United Kingdom")
    PORT_DIPSETTING(   0x06, "(XL): New Zealand")
    PORT_DIPSETTING(   0x06, "(XR): Australia")
    PORT_DIPSETTING(   0x06, "(XS): Malaysia")
    PORT_DIPSETTING(   0x06, "(MD): Saudi Arabia, Hong Kong, Kuwait")
    PORT_DIPSETTING(   0x06, "(XT): Taiwan")
    PORT_DIPSETTING(   0x02, "(X): Thailand, Indonesia, Iran, U.A.E., Panama, Argentina, Peru, Brazil")
    PORT_DIPSETTING(   0x06, "(XP): Philippines")
    PORT_DIPSETTING(   0x06, "(XW): Singapore")
*/

	PORT_START("CPR_SEG0")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("TRANSPOSE -")
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("TRANSPOSE +")
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("CPR_SEG1")  // SOUND GROUP
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("ORGAN & ACCORDION")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("ORCHESTRAL PAD")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SYNTH")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("BASS")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DIGITAL DRAWBAR")
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("ACCORDION REGISTER")
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("GM SPECIAL")
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DRUM KITS")

	PORT_START("CPR_SEG2")  // SOUND GROUP
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("PIANO") PORT_CODE(KEYCODE_L)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("GUITAR")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("STRINGS & VOCAL")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("BRASS")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("FLUTE")
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SAX & REED")
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MALLET & ORCH PERC")
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("WORLD PERC")

	PORT_START("CPR_SEG3")  // EFFECT
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SUSTAIN")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DIGITAL EFFECT")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DSP EFFECT")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DIGITAL REVERB")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("ACOUSTIC ILLUSION")
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("CPR_SEG4")  // PART SELECT
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("LEFT")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RIGHT 2")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RIGHT 1")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("ENTERTAINER")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("CONDUCTOR: LEFT")
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("CONDUCTOR: RIGHT 2")
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("CONDUCTOR: RIGHT 1")
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("TECHNI CHORD")

	PORT_START("CPR_SEG5")  // SEQUENCER
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SEQUENCER: PLAY")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SEQUENCER: EASY REC")
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SEQUENCER: MENU")
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("CPR_SEG6")  // PANEL MEMORY
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("PM 1")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("PM 2")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("PM 3")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("PM 4")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("PM 5")
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("PM 6")
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("PM 7")
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("PM 8")

	PORT_START("CPR_SEG7")  // PANEL MEMORY
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("PANEL MEMORY: SET")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("PANEL MEMORY: NEXT BANK")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("PANEL MEMORY: BANK VIEW")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("CPR_SEG8")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("R1/R2 OCTAVE -")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("R1/R2 OCTAVE +")
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("START/STOP") PORT_CODE(KEYCODE_SPACE)
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SYNCHRO & BREAK")
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("TAP TEMPO")

	PORT_START("CPR_SEG9")  // SOUND GROUP
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MEMORY A")
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MEMORY B")

	PORT_START("CPR_SEG10")  // MENU
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MENU: SOUND") PORT_CODE(KEYCODE_B)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MENU: CONTROL") PORT_CODE(KEYCODE_N)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MENU: MIDI") PORT_CODE(KEYCODE_M)
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MENU: DISK") PORT_CODE(KEYCODE_COMMA)
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("CPL_SEG0")  // RHYTHM GROUP
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("STANDARD ROCK")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("R & ROLL & BLUES")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("POP & BALLAD")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("FUNK & FUSION")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SOUL & MODERN DANCE")
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("BIG BAND & SWING")
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("JAZZ COMBO")
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("CPL_SEG1")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MEMORY") // Composer
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MENU") // Composer
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SET") // Sound Arranger
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("ON/OFF") // Sound Arranger
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MUSIC STYLIST")
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("FADE IN")
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("FADE OUT")
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("CPL_SEG2")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("FILL IN 1")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("FILL IN 2")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("INTRO & ENDING 1")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("INTRO & ENDING 2")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("PAGE DOWN") PORT_CODE(KEYCODE_PGDN)
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("PAGE UP") PORT_CODE(KEYCODE_PGUP)

	PORT_START("CPL_SEG3")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DEMO") PORT_CODE(KEYCODE_ENTER)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MSP BANK")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MSP MENU")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MSP STOP/RECORD")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("CPL_SEG4")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("VARIATION 1") // VARIATION & MSA
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("VARIATION 2") // VARIATION & MSA
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("VARIATION 3") // VARIATION & MSA
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("VARIATION 4") // VARIATION & MSA
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MUSIC STYLE ARRANGER")
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SPLIT POINT")
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("AUTO PLAY CHORD")
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("CPL_SEG5")  // MANUAL SEQUENCE PADS
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MSP 1")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MSP 2")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MSP 3")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MSP 4")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MSP 5")
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MSP 6")
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("CPL_SEG6")  // RHYTHM GROUP
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("U.S. TRAD")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("COUNTRY")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("LATIN")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("MARCH & WALTZ")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("PARTY TIME")
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("SHOWTIME & TRAD DANCE")
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("WORLD")
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("CUSTOM")

	PORT_START("CPL_SEG7")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RIGHT 5") PORT_CODE(KEYCODE_0)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RIGHT 4") PORT_CODE(KEYCODE_9)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DISPLAY HOLD") PORT_CODE(KEYCODE_Z)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("EXIT") PORT_CODE(KEYCODE_X)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN 7") PORT_CODE(KEYCODE_J)
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP 7") PORT_CODE(KEYCODE_U)
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN 8") PORT_CODE(KEYCODE_K)
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP 8") PORT_CODE(KEYCODE_I)

	PORT_START("CPL_SEG8")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RIGHT 3") PORT_CODE(KEYCODE_8)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RIGHT 2") PORT_CODE(KEYCODE_7)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RIGHT 1") PORT_CODE(KEYCODE_6)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN 5") PORT_CODE(KEYCODE_G)
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP 5") PORT_CODE(KEYCODE_T)
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN 6") PORT_CODE(KEYCODE_H)
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP 6") PORT_CODE(KEYCODE_Y)

	PORT_START("CPL_SEG9")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("LEFT 5") PORT_CODE(KEYCODE_5)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("LEFT 4") PORT_CODE(KEYCODE_4)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("LEFT 3") PORT_CODE(KEYCODE_3)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN 3") PORT_CODE(KEYCODE_D)
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP 3") PORT_CODE(KEYCODE_E)
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN 4") PORT_CODE(KEYCODE_F)
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP 4") PORT_CODE(KEYCODE_R)

	PORT_START("CPL_SEG10")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("LEFT 2") PORT_CODE(KEYCODE_2)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("LEFT 1") PORT_CODE(KEYCODE_1)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("HELP") PORT_CODE(KEYCODE_SLASH)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("OTHER PARTS/TR") PORT_CODE(KEYCODE_O)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN 1") PORT_CODE(KEYCODE_A)
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP 1") PORT_CODE(KEYCODE_Q)
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("DOWN 2") PORT_CODE(KEYCODE_S)
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("UP 2") PORT_CODE(KEYCODE_W)

	// 61-key keyboard (C2-C7) — directly connected to tone generator IC303
	// IC303 does hardware key scanning; this HLE injects events at 0x110000
	// PC keyboard mapping: Z-row = lower octave, Q-row = upper octave (piano layout)
	// Base octave = C4 (raw notes 24-47 for the two mapped octaves)

	PORT_START("KEY0")  // C2-B2 (raw notes 0-11, MIDI 36-47)
	PORT_BIT( 0x001, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("C2")
	PORT_BIT( 0x002, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("C#2")
	PORT_BIT( 0x004, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("D2")
	PORT_BIT( 0x008, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("D#2")
	PORT_BIT( 0x010, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("E2")
	PORT_BIT( 0x020, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("F2")
	PORT_BIT( 0x040, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("F#2")
	PORT_BIT( 0x080, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("G2")
	PORT_BIT( 0x100, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("G#2")
	PORT_BIT( 0x200, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("A2")
	PORT_BIT( 0x400, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("A#2")
	PORT_BIT( 0x800, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("B2")

	PORT_START("KEY1")  // C3-B3 (raw notes 12-23, MIDI 48-59)
	PORT_BIT( 0x001, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("C3")
	PORT_BIT( 0x002, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("C#3")
	PORT_BIT( 0x004, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("D3")
	PORT_BIT( 0x008, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("D#3")
	PORT_BIT( 0x010, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("E3")
	PORT_BIT( 0x020, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("F3")
	PORT_BIT( 0x040, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("F#3")
	PORT_BIT( 0x080, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("G3")
	PORT_BIT( 0x100, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("G#3")
	PORT_BIT( 0x200, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("A3")
	PORT_BIT( 0x400, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("A#3")
	PORT_BIT( 0x800, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("B3")

	// KEY2 and KEY3 have no default PORT_CODE assignments because all candidate
	// keys (Z/S/X/D/C/V/G/B/H/N/J/M, Q/2/W/3/E/R/5/T/6/Y/7/U) conflict with
	// control panel button mappings above. Use MAME's input configuration UI
	// (Tab menu) to assign keyboard keys to these notes.

	PORT_START("KEY2")  // C4-B4 (raw notes 24-35, MIDI 60-71) — Middle C octave
	PORT_BIT( 0x001, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("C4")
	PORT_BIT( 0x002, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("C#4")
	PORT_BIT( 0x004, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("D4")
	PORT_BIT( 0x008, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("D#4")
	PORT_BIT( 0x010, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("E4")
	PORT_BIT( 0x020, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("F4")
	PORT_BIT( 0x040, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("F#4")
	PORT_BIT( 0x080, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("G4")
	PORT_BIT( 0x100, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("G#4")
	PORT_BIT( 0x200, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("A4")
	PORT_BIT( 0x400, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("A#4")
	PORT_BIT( 0x800, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("B4")

	PORT_START("KEY3")  // C5-B5 (raw notes 36-47, MIDI 72-83)
	PORT_BIT( 0x001, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("C5")
	PORT_BIT( 0x002, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("C#5")
	PORT_BIT( 0x004, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("D5")
	PORT_BIT( 0x008, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("D#5")
	PORT_BIT( 0x010, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("E5")
	PORT_BIT( 0x020, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("F5")
	PORT_BIT( 0x040, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("F#5")
	PORT_BIT( 0x080, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("G5")
	PORT_BIT( 0x100, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("G#5")
	PORT_BIT( 0x200, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("A5")
	PORT_BIT( 0x400, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("A#5")
	PORT_BIT( 0x800, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("B5")

	PORT_START("KEY4")  // C6-B6 (raw notes 48-59, MIDI 84-95)
	PORT_BIT( 0x001, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("C6")
	PORT_BIT( 0x002, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("C#6")
	PORT_BIT( 0x004, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("D6")
	PORT_BIT( 0x008, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("D#6")
	PORT_BIT( 0x010, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("E6")
	PORT_BIT( 0x020, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("F6")
	PORT_BIT( 0x040, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("F#6")
	PORT_BIT( 0x080, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("G6")
	PORT_BIT( 0x100, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("G#6")
	PORT_BIT( 0x200, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("A6")
	PORT_BIT( 0x400, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("A#6")
	PORT_BIT( 0x800, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("B6")

	PORT_START("KEY5")  // C7 (raw note 60, MIDI 96) — highest key
	PORT_BIT( 0x001, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("C7")
	PORT_BIT( 0xffe, IP_ACTIVE_HIGH, IPT_UNUSED )
INPUT_PORTS_END


void kn5000_state::machine_start()
{
	save_item(NAME(m_mstat));
	save_item(NAME(m_sstat));
	save_item(NAME(m_cpanel_inta));
	save_item(NAME(m_subcpu_latch_write_count));
	save_item(NAME(m_maincpu_latch_write_count));
	save_item(NAME(m_keybed_prev));

	m_extension->program_map(m_maincpu->space(AS_PROGRAM));

	m_checking_device_led_cn11.resolve();
	m_checking_device_led_cn12.resolve();

	// Connect button input ports to control panel HLE device
	for (int i = 0; i < 11; i++)
	{
		m_cpanel->set_cpl_port(i, m_CPL_SEG[i].target());
		m_cpanel->set_cpr_port(i, m_CPR_SEG[i].target());
	}

	// Keybed scan timer: poll keyboard input ports every 1ms
	memset(m_keybed_prev, 0, sizeof(m_keybed_prev));
	m_keybed_timer = timer_alloc(FUNC(kn5000_state::keybed_scan), this);
	m_keybed_timer->adjust(attotime::from_msec(1), 0, attotime::from_msec(1));
}

void kn5000_state::machine_reset()
{
	m_checking_device_led_cn11 = 0;
	m_checking_device_led_cn12 = 0;

	// Clear keybed state
	memset(m_keybed_prev, 0, sizeof(m_keybed_prev));
	while (!m_keybed_queue.empty())
		m_keybed_queue.pop();
}

void kn5000_state::nvram2_init(nvram_device &device, void *data, size_t size)
{
	// Initialize NVRAM with factory defaults from program ROM.
	// On real hardware, NVRAM (backup SRAM) is pre-programmed at the factory.
	// Without valid data, the firmware fails header/checksum validation and
	// skips Sub-CPU payload transfer, causing incomplete initialization.
	//
	// Factory defaults location in v10 ROM: offset 0x0A0150 (0x72A6 bytes)
	// Header: "KN5000 SOUND RAM" (16 bytes), followed by settings data.
	// Checksum: one's complement of sum of 0x24B8 LE words from offset 0x10,
	// stored at offset 0x72A8.
	uint8_t *dest = reinterpret_cast<uint8_t *>(data);
	memset(dest, 0, size);

	const uint8_t *rom = memregion("program")->base();
	static constexpr uint32_t FACTORY_DEFAULTS_ROM_OFFSET = 0x0A0150;
	static constexpr uint32_t FACTORY_DEFAULTS_SIZE = 0x72A6;
	static constexpr uint32_t CHECKSUM_WORD_COUNT = 0x24B8;
	static constexpr uint32_t CHECKSUM_DATA_OFFSET = 0x10;
	static constexpr uint32_t CHECKSUM_STORE_OFFSET = 0x72A8;

	memcpy(dest, rom + FACTORY_DEFAULTS_ROM_OFFSET, FACTORY_DEFAULTS_SIZE);

	// Compute checksum matching firmware's validation routine (LABEL_FEF93B):
	// ADD DE, (XWA+) loop over 0x24B8 words, then CPL DE
	uint16_t sum = 0;
	for (uint32_t i = 0; i < CHECKSUM_WORD_COUNT; i++)
	{
		uint32_t offset = CHECKSUM_DATA_OFFSET + i * 2;
		uint16_t word = dest[offset] | (dest[offset + 1] << 8);
		sum += word;
	}
	uint16_t checksum = ~sum;
	dest[CHECKSUM_STORE_OFFSET] = checksum & 0xFF;
	dest[CHECKSUM_STORE_OFFSET + 1] = (checksum >> 8) & 0xFF;
}

void kn5000_state::kn5000(machine_config &config)
{
	// Note: The CPU has an internal clock doubler
	TMP94C241(config, m_maincpu, 2 * 8_MHz_XTAL); // TMP94C241F @ IC5
	// Address bus is set to 32 bits by the pins AM1=+5v and AM0=GND
	m_maincpu->set_addrmap(AS_PROGRAM, &kn5000_state::maincpu_mem);
	// Interrupt 4: FDCINT
	// Interrupt 5: FDCIRQ
	// Interrupt 6: FDC.H/D // NOTE: interrupt handler is empty
	// Interrupt 7: FDC.I/O // NOTE: interrupt handler is empty
	// Interrupt 9: HDDINT
	// Interrupt A <edge>: ~CPSCK "Control Panel Serial Clock"
	// ~NMI: SNS
	// TC0: FDCTC


	// MAINCPU PORT 7:
	//   bit 5 (~BUSRQ pin): RY/~BY pin of maincpu ROMs
	m_maincpu->port7_read().set_constant(1 << 5); // checked at EF3735 (v10 ROM)


	// MAINCPU PORT 8:
	//   bit 6 (~WAIT pin) (input): Something involving VGA.RDY, FDC.DMAACK
	//                              and shift-register @ IC18


	// MAINCPU PORT A:
	//   bit 0 (output) = sub_cpu ~RESET / SRST
	m_maincpu->porta_write().set([this] (u8 data) {
		bool reset_released = BIT(data, 0);
		LOGMASKED(LOG_RESET, "SubCPU reset: %s (PA=0x%02X) PC=%06X\n",
			reset_released ? "RELEASED" : "ASSERTED", data, m_maincpu->pc());
		m_subcpu->set_input_line(INPUT_LINE_RESET, reset_released ? CLEAR_LINE : ASSERT_LINE);
	});

	// MAINCPU PORT C:
	//   bit 0 (input) = "check terminal" switch
	//   bit 1 (output) = "check terminal" LED
	m_maincpu->portc_read().set_ioport("CN11");
	m_maincpu->portc_write().set([this] (u8 data) {
		m_checking_device_led_cn11 = (BIT(data, 1) == 0);
	});


	// MAINCPU PORT D:
	//   bit 0 (output) = FDCRST
	//   bit 6 (input) = FD.I/O
	m_maincpu->portd_write().set(m_fdc, FUNC(upd72067_device::reset_w)).bit(0);
	// TODO: bit 6!


	// MAINCPU PORT E:
	//   bit 0 (input) = +5v
	//   bit 2 (input) = HDDRDY
	//   bit 4 (?) = MICSNS
	//   bit 5 (input) = INTA (control panel interrupt)
	m_maincpu->porte_read().set([this] {
		// Bit 0: +5v (always 1 when no HDD extension)
		// Bit 5: INTA from control panel (active HIGH — firmware checks BIT 5,(PE); JR NZ)
		return 0x01 | (m_cpanel_inta ? 0x20 : 0x00);
	});


	// MAINCPU PORT F:
	//   bit 2 (OUTPUT) = Something related to "RESET CONTROL" circuits?
	m_maincpu->portf_read().set_constant(1 << 6); //checked at FC437A (v10 ROM)


	// MAINCPU PORT G:
	//   bit 2 (input) = FS1  (Foot Switches and Foot Controler ?)
	//   bit 3 (input) = FS2
	//   bit 4 (input) = FC1
	//   bit 5 (input) = FC2
	//   bit 6 (input) = FC3
	//   bit 7 (input) = FC4


	// MAINCPU PORT H:
	m_maincpu->porth_read().set_ioport("AREA"); // checked at EF083E (v10 ROM)


	// MAINCPU PORT Z:
	//   bit 0 = (output) MSTAT0
	//   bit 1 = (output) MSTAT1
	//   bit 2 = (input) SSTAT0
	//   bit 3 = (input) SSTAT1
	//   bit 4 = (input) COM.PC2
	//   bit 5 = (input) COM.PC1
	//   bit 6 = (input) COM.MAC
	//   bit 7 = (input) COM.MIDI
	m_maincpu->portz_read().set([this] {
		return m_com_select->read() | (m_sstat << 2);
	});
	m_maincpu->portz_write().set([this] (u8 data) {
		uint8_t new_mstat = data & 3;
		if (new_mstat != m_mstat)
			LOGMASKED(LOG_HANDSHAKE, "MSTAT: %d -> %d (PZ=0x%02X) PC=%06X\n",
				m_mstat, new_mstat, data, m_maincpu->pc());
		m_mstat = new_mstat;
	});


	// RX0/TX0 = MRXD/MTXD
	auto &mdin(MIDI_PORT(config, "mdin"));
	midiin_slot(mdin);
	mdin.rxd_handler().set(m_maincpu->m_serial[0], FUNC(tmp94c241_serial_device::rxd));

	// TODO: MIDI output
	// midiout_slot(MIDI_PORT(config, "mdout"));

	// RX1/TX1 = CPDATA
	// SCLK1 = CPSCK
	auto &m_cpanel(KN5000_CPANEL(config, "cpanel"));
	m_maincpu->m_serial[1].lookup()->txd().set(m_cpanel, FUNC(kn5000_cpanel_device::rxd));
	m_maincpu->m_serial[1].lookup()->sclk_out().set(m_cpanel, FUNC(kn5000_cpanel_device::sioclk));
	m_maincpu->m_serial[1].lookup()->tx_start().set(m_cpanel, FUNC(kn5000_cpanel_device::tx_start));
	m_cpanel.txd().set(m_maincpu->m_serial[1], FUNC(tmp94c241_serial_device::rxd));
	m_cpanel.sclk_out().set(m_maincpu->m_serial[1], FUNC(tmp94c241_serial_device::sioclk));
	m_cpanel.inta().set([this] (int state) {
		m_cpanel_inta = state;
		// Assert/deassert INTA interrupt on the CPU (active on rising edge)
		m_maincpu->set_input_line(TLCS900_INTA, state ? ASSERT_LINE : CLEAR_LINE);
	});


	// AN0 = EXP (expression pedal?)
	// AN1 = AFT

	// Note: The CPU has an internal clock doubler
	TMP94C241(config, m_subcpu, 2*10_MHz_XTAL); // TMP94C241F @ IC27
	// Address bus is set to 8 bits by the pins AM1=GND and AM0=GND
	m_subcpu->set_addrmap(AS_PROGRAM, &kn5000_state::subcpu_mem);

	// SUBCPU PORT C:
	//   bit 0 (input) = "check terminal" switch
	//   bit 1 (output) = "check terminal" LED
	m_subcpu->portc_read().set_ioport("CN12");
	m_subcpu->portc_write().set([this] (u8 data) {
		m_checking_device_led_cn12 = (BIT(data, 1) == 0);
	});


	// SUBCPU PORT D:
	//   bit 0 = (output) SSTAT0
	//   bit 1 = (output) SSTAT1
	//   bit 2 = (input) MSTAT0
	//   bit 3 (not used)
	//   bit 4 = (input) MSTAT1
	m_subcpu->portd_read().set([this] {
		return (BIT(m_mstat, 0) << 2) | (BIT(m_mstat, 1) << 4);
	});
	m_subcpu->portd_write().set([this] (u8 data) {
		uint8_t new_sstat = data & 3;
		if (new_sstat != m_sstat)
			LOGMASKED(LOG_HANDSHAKE, "SSTAT: %d -> %d (PD=0x%02X) PC=%06X\n",
				m_sstat, new_sstat, data, m_subcpu->pc());
		m_sstat = new_sstat;
	});


	GENERIC_LATCH_8(config, m_maincpu_latch); // @ IC23
	m_maincpu_latch->data_pending_callback().set_inputline(m_maincpu, TLCS900_INT0);

	GENERIC_LATCH_8(config, m_subcpu_latch); //  @ IC22
	m_subcpu_latch->data_pending_callback().set_inputline(m_subcpu, TLCS900_INT0);

	UPD72067(config, m_fdc, 32'000'000); // actual controller is UPD72068GF-3B9 at IC208
	m_fdc->intrq_wr_callback().set_inputline(m_maincpu, TLCS900_INT4);
	m_fdc->drq_wr_callback().set_inputline(m_maincpu, TLCS900_INT5);
	// Review:
	// Interrupt 4: FDCINT
	// Interrupt 5: FDCIRQ


	// NOTE: int6 and int7 handlers are empty routines
	// Interrupt 6: FDC.H/D
	// Interrupt 7: FDC.I/O
	//
	// m_fdc->hdl_wr_callback().set_inputline(m_maincpu, TLCS900_INT6);
	// TODO: tc coming from maincpu TC0 signal
	// m_fdc->??_wr_callback().set_inputline(m_maincpu, TLCS900_INT7);


	FLOPPY_CONNECTOR(config, "fdc:0", kn5000_floppies, "35dd", floppy_image_device::default_mfm_floppy_formats).enable_sound(true);

	/* Extension port */
	KN5000_EXTENSION(config, m_extension, kn5000_extension_intf, nullptr);
	m_extension->irq_callback().set_inputline(m_maincpu, TLCS900_INT9);

	/* video hardware */
	// LCD Controller MN89304 @ IC206 24_MHz_XTAL
	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
	screen.set_raw(XTAL(40'000'000)/6, 424, 0, 320, 262, 0, 240);
	screen.set_screen_update("vga", FUNC(mn89304_vga_device::screen_update));

	mn89304_vga_device &vga(MN89304_VGA(config, "vga", 0));
	vga.set_screen("screen");
	// 4 Mbit, M5M44265CJ6S
	vga.set_vram_size(0x80000);
	// iochrdy tied to refresh pin and SA19, A21 and A20 to GND
	// TODO: VGA.A18 signal, banking? From maincpu thru a T7W139F decoder

	NVRAM(config, "nvram1", nvram_device::DEFAULT_ALL_0);
	NVRAM(config, "nvram2").set_custom_handler(FUNC(kn5000_state::nvram2_init));

	config.set_default_layout(layout_kn5000);
}

ROM_START(kn5000)
	ROM_DEFAULT_BIOS("v10")
	ROM_SYSTEM_BIOS(0, "v10", "Version 10 - August 2nd, 1999")
	ROM_SYSTEM_BIOS(1, "v9", "Version 9 - January 26th, 1999")
	ROM_SYSTEM_BIOS(2, "v8", "Version 8 - November 13th, 1998")
	ROM_SYSTEM_BIOS(3, "v7", "Version 7 - June 26th, 1998")
	ROM_SYSTEM_BIOS(4, "v6", "Version 6 - January 16th, 1998") // sometimes refered to as "update6v0"
	ROM_SYSTEM_BIOS(5, "v5", "Version 5 - November 12th, 1997") // sometimes refered to as "update5v0"
	ROM_SYSTEM_BIOS(6, "v4", "Version 4") // I have a v4 board but haven't dumped it yet
	ROM_SYSTEM_BIOS(7, "v3", "Version 3") // I have a v3 board but haven't dumped it yet

	ROM_REGION16_LE(0x200000, "program" , 0) // main cpu

	// FIXME: These are actually stored in a couple flash rom chips IC6 (even) and IC4 (odd)
	//
	// Note: These ROMs from v5 to v10 were extracted from the system update floppies
	//       which were compressed using LZSS.
	//
	//       System update disks for older versions were not found yet, so dumping
	//       efforts will require other methods.
	//
	//       More info at:
	//       https://github.com/felipesanches/kn5000_homebrew/blob/main/kn5000_extract.py

	ROMX_LOAD("kn5000_v10_program.rom", 0x00000, 0x200000, CRC(00303406) SHA1(1f2abc5b1b7b9e16fdf796f26d939edaceded354), ROM_BIOS(0))
	ROMX_LOAD("kn5000_v9_program.rom",  0x00000, 0x200000, CRC(c791d765) SHA1(d9a3b462b1f9302402e8d37aacd15f069f56abd9), ROM_BIOS(1))
	ROMX_LOAD("kn5000_v8_program.rom",  0x00000, 0x200000, CRC(46b4b242) SHA1(a10a6f5a35175b74c3cfb42cef3bdf571c2858bb), ROM_BIOS(2))
	ROMX_LOAD("kn5000_v7_program.rom",  0x00000, 0x200000, CRC(a5a25eb0) SHA1(4c682cb248034a2de04c688b0a45654b8726bffb), ROM_BIOS(3))
	ROMX_LOAD("kn5000_v6_program.rom",  0x00000, 0x200000, CRC(0205db30) SHA1(51108e2d75b180a034395e90bd40ca2bd2a0adfb), ROM_BIOS(4))
	ROMX_LOAD("kn5000_v5_program.rom",  0x00000, 0x200000, CRC(fbd035e3) SHA1(7b69a8aaa84ee3d337acc0c29c34154c5da2df32), ROM_BIOS(5))
	ROMX_LOAD("kn5000_v4_program.rom",  0x00000, 0x200000, NO_DUMP, ROM_BIOS(6))
	ROMX_LOAD("kn5000_v3_program.rom",  0x00000, 0x200000, NO_DUMP, ROM_BIOS(7))

	// Note: I've never seen boards with versions 1 or 2.

	ROM_REGION16_LE(0x20000, "subcpu", 0)
	ROM_LOAD("kn5000_subcpu_boot.ic30", 0x00000, 0x20000, BAD_DUMP CRC(a45ceb77) SHA1(d29429a9a1ef7a718fa88c1aa38d0f7238ba5d94)) // Ranges fe0800-ff7800 and ff9800-fff000 not dumped yet. Assumed here as being filled with 0xFF.

	ROM_REGION16_LE(0x200000, "table_data", 0)
	ROM_LOAD32_WORD("kn5000_table_data_rom_even.ic3", 0x000000, 0x100000, CRC(b6f0becd) SHA1(1fd2604236b8d12ea7281fad64d72746eb00c525))
	ROM_LOAD32_WORD("kn5000_table_data_rom_odd.ic1",  0x000002, 0x100000, CRC(cd907eac) SHA1(bedf09d606d476f3e6d03e590709715304cf7ea5))

	ROM_REGION16_LE(0x100000, "custom_data", 0)
	ROM_LOAD("kn5000_custom_data_rom.ic19", 0x000000, 0x100000, CRC(5de11a6b) SHA1(4709f815d3d03ce749c51f4af78c62bf4a5e3d94))
	// IC19 is a flash ROM. The contents here were dumped from a system that had it already programmed by the initial data disk.
	// Maybe it could also be declared as NVRAM here?
	//
	// The subcpu payload is stored compressed (LZSS SLIDE4K format) in IC19 flash at address 0x3E0000 (offset 0xE0000).
	// During boot, the maincpu decompresses it and transfers it to the subcpu RAM via the inter-cpu latches.
	// The compressed payloads below were extracted from the system update floppy disk images.
	ROMX_LOAD("kn5000_subprogram_v142_compressed.rom", 0x0e0000, 0x16c13, CRC(f81e598f) SHA1(13718900afd55cb2e5ff0be213ba1f5dd14bc174), ROM_BIOS(0)) // v10
	ROMX_LOAD("kn5000_subprogram_v142_compressed.rom", 0x0e0000, 0x16c13, CRC(f81e598f) SHA1(13718900afd55cb2e5ff0be213ba1f5dd14bc174), ROM_BIOS(1)) // v9
	ROMX_LOAD("kn5000_subprogram_v141_compressed.rom", 0x0e0000, 0x16bfd, CRC(c6d4ad98) SHA1(ac9791441ceb13748a2196a0a6a400431d6aed5e), ROM_BIOS(2)) // v8
	ROMX_LOAD("kn5000_subprogram_v141_compressed.rom", 0x0e0000, 0x16bfd, CRC(c6d4ad98) SHA1(ac9791441ceb13748a2196a0a6a400431d6aed5e), ROM_BIOS(3)) // v7
	ROMX_LOAD("kn5000_subprogram_v140_compressed.rom", 0x0e0000, 0x16bc4, CRC(5b182629) SHA1(13098dd150c5a6083a5d15a63d5d785802d8e8ae), ROM_BIOS(4)) // v6
	ROMX_LOAD("kn5000_subprogram_v140_compressed.rom", 0x0e0000, 0x16bc4, CRC(5b182629) SHA1(13098dd150c5a6083a5d15a63d5d785802d8e8ae), ROM_BIOS(5)) // v5

	ROM_REGION16_LE(0x400000, "rhythm_data", 0)
	ROM_LOAD("kn5000_rhythm_data_rom.ic14", 0x000000, 0x400000, CRC(76d11a5e) SHA1(e4b572d318c9fe7ba00e5b44ea783e89da9c68bd))

	ROM_REGION16_LE(0x1000000, "waveform", 0)
	ROM_LOAD("kn5000_waveform_rom.ic304", 0x000000, 0x400000, NO_DUMP)
	ROM_LOAD("kn5000_waveform_rom.ic305", 0x400000, 0x400000, NO_DUMP)
	ROM_LOAD("kn5000_waveform_rom.ic306", 0x800000, 0x400000, NO_DUMP)
	ROM_LOAD("kn5000_waveform_rom.ic307", 0xc00000, 0x400000, CRC(20ff4629) SHA1(4b511bff6625f4655cabd96a263bf548d2ef4bf7))
ROM_END

} // anonymous namespace

//   YEAR  NAME   PARENT  COMPAT  MACHINE INPUT   STATE         INIT        COMPANY      FULLNAME             FLAGS
CONS(1998, kn5000,    0,       0, kn5000, kn5000, kn5000_state, empty_init, "Technics", "SX-KN5000", MACHINE_NOT_WORKING|MACHINE_NO_SOUND)
