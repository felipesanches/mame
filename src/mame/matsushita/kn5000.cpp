// license:GPL2+
// copyright-holders:Felipe Sanches
/******************************************************************************

    Technics SX-KN5000 music keyboard driver

******************************************************************************/

#include "emu.h"
#include "bus/technics/kn5000/hdae5000.h"
#include "bus/midi/midi.h"
#include "cpu/tlcs900/tmp94c241.h"
#include "imagedev/floppy.h"
#include "machine/gen_latch.h"
#include "machine/nvram.h"
#include "machine/upd765.h"
#include "sound/ds3613gf3ba.h"
#include "sound/mn19413.h"
#include "sound/tc183c230002.h"
#include "video/pc_vga.h"
#include "screen.h"
#include "speaker.h"
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
#define LOG_KEYBED   (1U << 5)  // Keybed scan events (driver-side)
#define LOG_HEARTBEAT (1U << 6) // Periodic CPU PC snapshot (1 second interval)
#define LOG_COMIF    (1U << 7)  // Computer interface (SubCPU SC1 serial port)
#define LOG_SEQBUF   (1U << 8)  // Sequencer ring buffer pointer changes
#define LOG_AUDIOMIX (1U << 9)  // Audio mixer/attenuator at 0x150000
#define LOG_BOOT     (1U << 10) // Boot sequence events (NMI guard, payload verify)
#define LOG_SOUND    (1U << 11) // Sound/DSP control signals (mute, DSP status)
#define LOG_DSP2     (1U << 12) // DSP2 (MN19413) GPIO serial framing
#define LOG_ALL_LATCH (LOG_LATCH | LOG_LATCH_DATA)

#define VERBOSE (LOG_LATCH | LOG_RESET | LOG_HANDSHAKE | LOG_KEYBED | LOG_HEARTBEAT | LOG_COMIF | LOG_SEQBUF | LOG_AUDIOMIX | LOG_BOOT | LOG_SOUND)
#include "logmacro.h"

// Timestamped logging: prepend emulated time in seconds to each message
#define TLOGMASKED(mask, fmt, ...) LOGMASKED(mask, "@%10.6f " fmt, machine().time().as_double(), ##__VA_ARGS__)

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
		, m_tonegen(*this, "tonegen")
		, m_dsp1(*this, "dsp1")
		, m_dsp2(*this, "dsp2")
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
		, m_subcpu_p7(0xff)
		, m_subcpu_pz(0)
		, m_subcpu_pe(0xff)
		, m_subcpu_pf(0)
		, m_dsp2_shift(0)
		, m_dsp2_bit_count(0)
		, m_comif_txd_state(1)
		, m_comif_sclk_state(0)
		, m_comif_shift(0)
		, m_comif_bit_count(0)
		, m_comif_receiving(false)
		, m_audiomix_addr(0)
		, m_seq_event_loop_hits(0)
		, m_putc_mrx_bf_hits(0)
		, m_rhythm_rom_hits(0)
		, m_rhythm_buf_writes(0)
		, m_rhythm_buf_reads(0)
	{ }

	void kn5000(machine_config &config);

private:
	required_device<kn5000_cpanel_device> m_cpanel;
	required_device<tmp94c241_device> m_maincpu;
	required_device<tmp94c241_device> m_subcpu;
	required_device<generic_latch_8_device> m_maincpu_latch;
	required_device<generic_latch_8_device> m_subcpu_latch;
	required_device<upd72067_device> m_fdc;
	required_device<tc183c230002_device> m_tonegen;     // Tone Generator IC303 (TC183C230002)
	required_device<ds3613gf3ba_device> m_dsp1;          // DSP1 — IC311 (DS3613GF-3BA), memory-mapped
	required_device<mn19413_device> m_dsp2;              // DSP2 — IC310 (MN19413), serial
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

	// SubCPU GPIO port state for DSP routing
	uint8_t m_subcpu_p7;     // Port 7: bit3=~WR, bit4=~RD, bit5=~CS1, bit6=C/~D
	uint8_t m_subcpu_pz;     // Port Z: data bus to DSP1
	uint8_t m_subcpu_pe;     // Port E: bit6=~CS2 (DSP2 chip select)
	uint8_t m_subcpu_pf;     // Port F: bit0=SDA, bit2=SCLK (DSP2 serial)
	uint16_t m_dsp2_shift;   // DSP2 serial shift register
	uint8_t m_dsp2_bit_count; // DSP2 serial bit counter

	// Computer Interface (SubCPU SC1) — UART byte decoder
	uint8_t m_comif_txd_state;    // Current TXD level
	uint8_t m_comif_sclk_state;   // Current SCLK level
	uint8_t m_comif_shift;        // Shift register for received bits
	uint8_t m_comif_bit_count;    // Bits accumulated in current byte
	bool m_comif_receiving;       // True after tx_start, until byte complete

	// Audio mixer/attenuator at 0x150000 (register-indirect)
	uint8_t m_audiomix_addr;              // Current register address
	uint8_t m_audiomix_regs[256];         // Register file
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

	// Latch logging wrappers
	void subcpu_latch_w(uint8_t data);
	uint8_t subcpu_latch_r();
	void maincpu_latch_w(uint8_t data);
	uint8_t maincpu_latch_r();

	// Keybed scan timer — polls MAME input ports and injects events into tonegen device
	uint8_t m_keybed_prev[61];
	emu_timer *m_keybed_timer;
	TIMER_CALLBACK_MEMBER(keybed_scan);
	static constexpr uint8_t KEYBED_VELOCITY = 100; // fixed velocity for PC keyboard

	// Diagnostic heartbeat for hang detection
	emu_timer *m_heartbeat_timer;
	TIMER_CALLBACK_MEMBER(heartbeat);

	// ~NMI (SNS) — on real hardware this fires at power-off.  The NMI handler
	// (NMI_HANDLER at EF08A5) calls NMI_StorePayloadChecksums (EF08D4), which:
	//   1. Checks the NMI guard flag (DRAM[0x400] == 0x80)
	//   2. Saves voice presets and flush data
	//   3. Checksums the payload-shadow region in DRAM (0xF180..0xFE7F) and
	//      stores the results at DRAM[0xFFD4/0xFFD2] so SubCPU_Payload_Verify
	//      can compare against them on the next boot
	//   4. Copies DRAM[0xF980..] to backup SRAM at 0x1E8000
	//   5. Returns to NMI_HANDLER, which then executes halt (machine powers off)
	//
	// Two emulation mechanisms ensure the correct checksums are always in DRAM:
	//
	// (A) Boot-time write tap on DRAM[0xFFD4] (primary, always active):
	//     Boot_DisplayScreen clears DRAM[0xFFD4] = 0 on every boot.  Our tap
	//     intercepts that write and replaces it with the correct checksum, so
	//     SubCPU_Payload_Verify always passes on the same boot.  This is an HLE
	//     of the observable effect rather than the mechanism.
	//
	// (B) Exit-time SNS NMI (secondary, fires the real ROM handler):
	//     A 60 Hz periodic timer detects machine().exit_pending() and fires the
	//     real NMI.  NMI_StorePayloadChecksums at EF08D4 checks the NMI guard
	//     (internal CPU RAM at 0x0400 == 0x80) itself; if the guard is not set
	//     it returns immediately without halting, so it is safe to fire the NMI
	//     at any time.  When the guard IS set the handler computes checksums,
	//     stores them, and halts.  nvram_save() then captures the ROM-computed
	//     values.  This fires at most one frame before MAME closes.
	//     Due to MAME's exit-scheduler ordering (eat_all_cycles() is called
	//     before the timer can fire), this path is best-effort: it succeeds when
	//     the exit is triggered from the UI thread between timeslices, but may
	//     not execute the ROM handler when triggered by -seconds_to_run (where
	//     eat_all_cycles() pre-empts CPU execution).  Mechanism (A) is the
	//     reliable fallback in all cases.

	// Sequencer execution detection counters (reset each heartbeat)
	uint32_t m_seq_event_loop_hits;   // Hits at Seq_ProcessEventLoop (0xEF14CA)
	uint32_t m_putc_mrx_bf_hits;      // Hits at putc_mrx_bf_X (0xF1EDD4)
	uint32_t m_rhythm_rom_hits;       // Reads from rhythm_data ROM (0x400000-0x7FFFFF)
	uint32_t m_rhythm_buf_writes;     // Bytes written to rhythm ring buffer (0x01EF5D)
	uint32_t m_rhythm_buf_reads;      // Events read from rhythm ring buffer (LABEL_EF1525)

	emu_timer *m_sns_exit_check_timer;
	TIMER_CALLBACK_MEMBER(sns_exit_check);

	// Audio mixer/attenuator at 0x150000 (register-indirect interface)
	void audiomix_addr_w(uint8_t data);
	void audiomix_data_w(uint8_t data);
	uint8_t audiomix_data_r();

	void nvram2_init(nvram_device &device, void *data, size_t size);
	void maincpu_mem(address_map &map) ATTR_COLD;
	void subcpu_mem(address_map &map) ATTR_COLD;
};

void kn5000_state::subcpu_latch_w(uint8_t data)
{
	m_subcpu_latch_write_count++;
	// Log command bytes (E1, E2, E3) and first few data bytes
	if (data == 0xe1 || data == 0xe2 || data == 0xe3)
		TLOGMASKED(LOG_LATCH, "MainCPU -> SubCPU latch: cmd 0x%02X (write #%u) PC=%06X\n",
			data, m_subcpu_latch_write_count, m_maincpu->pc());
	else
		TLOGMASKED(LOG_LATCH_DATA, "MainCPU -> SubCPU latch: 0x%02X (write #%u)\n",
			data, m_subcpu_latch_write_count);

	// Force tight CPU interleaving so subcpu HDMA can process each byte
	// before the next one is written. On real hardware, HDMA steals cycles
	// between main CPU instructions.
	machine().scheduler().perfect_quantum(attotime::from_usec(100));

	// Force-clear the latch pending state before writing. On real hardware,
	// each latch write generates a new INT0 edge regardless of whether the
	// previous value was read. But generic_latch's data_pending_callback only
	// fires on state CHANGES — if the latch is already marked "written" (e.g.
	// because the receiver's INT0 handler exited without reading due to a
	// handshake flag), subsequent writes silently update the value without
	// triggering INT0. This caused the SubCPU to miss the E2 command during
	// boot, resulting in a 10-second timeout loop.
	if (m_subcpu_latch->pending_r())
		m_subcpu_latch->acknowledge_w(0);

	m_subcpu_latch->write(data);

	// Abort the current timeslice immediately. perfect_quantum only affects
	// FUTURE scheduling decisions — the writing CPU (maincpu) would otherwise
	// continue its timeslice and potentially write more bytes before the
	// subcpu gets a chance to read. Each latch write must be followed by a
	// context switch so the receiver can process it via HDMA.
	m_maincpu->abort_timeslice();
}

void kn5000_state::maincpu_latch_w(uint8_t data)
{
	m_maincpu_latch_write_count++;
	if (data == 0xe1 || data == 0xe2 || data == 0xe3)
		TLOGMASKED(LOG_LATCH, "SubCPU -> MainCPU latch: cmd 0x%02X (write #%u) PC=%06X\n",
			data, m_maincpu_latch_write_count, m_subcpu->pc());
	else
		TLOGMASKED(LOG_LATCH_DATA, "SubCPU -> MainCPU latch: 0x%02X (write #%u)\n",
			data, m_maincpu_latch_write_count);


	// Force tight CPU interleaving so maincpu DMAR can read each byte
	// before the next one is written. Without this, subcpu HDMA ch2 can
	// write multiple bytes to the latch during a single timeslice,
	// overwriting unread data.
	machine().scheduler().perfect_quantum(attotime::from_usec(100));

	// Force-clear pending state — see subcpu_latch_w for detailed explanation.
	if (m_maincpu_latch->pending_r())
		m_maincpu_latch->acknowledge_w(0);

	m_maincpu_latch->write(data);

	// Abort the current timeslice immediately — see subcpu_latch_w comment.
	m_subcpu->abort_timeslice();
}

uint8_t kn5000_state::subcpu_latch_r()
{
	uint8_t val = m_subcpu_latch->read();
	// Synchronously clear INT0 level to prevent stale m_level re-assertion.
	// generic_latch::read() calls set_input_line(INT0, CLEAR) which defers
	// via synchronize(), leaving m_level stale until the timeslice ends.
	m_subcpu->clear_int0_level();
	return val;
}

uint8_t kn5000_state::maincpu_latch_r()
{
	uint8_t val = m_maincpu_latch->read();
	m_maincpu->clear_int0_level();
	return val;
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
				m_tonegen->inject_key_event(data);
				TLOGMASKED(LOG_KEYBED, "Keybed: note ON raw=%d MIDI=%d vel=%d data=0x%04X\n",
					raw_note, raw_note + 0x24, KEYBED_VELOCITY, data);
			}
			else if (!pressed && prev)
			{
				// Key released: data = (0xFF << 8) | raw_note
				uint16_t data = (0xFF00) | raw_note;
				m_tonegen->inject_key_event(data);
				TLOGMASKED(LOG_KEYBED, "Keybed: note OFF raw=%d MIDI=%d data=0x%04X\n",
					raw_note, raw_note + 0x24, data);
			}
			m_keybed_prev[raw_note] = pressed;
		}
	}
}

// Diagnostic: snapshot CPU PCs every second for hang detection
TIMER_CALLBACK_MEMBER(kn5000_state::heartbeat)
{
	// Read sequencer diagnostic state from maincpu address space
	auto &space = m_maincpu->space(AS_PROGRAM);
	uint16_t seq_wr_ptr = space.read_word(0x01f377);  // Ring buffer write pointer
	uint16_t seq_rd_ptr = space.read_word(0x01f373);  // Ring buffer read pointer
	// Sequencer state machine variable - controls what sequencer dispatcher does
	// Values 0x10-0x16 cause sequencer dispatcher (F53318) to skip ALL processing
	// Playback states are typically 0x6C-0x7A, 0x85-0x98, etc.
	uint8_t seq_state = space.read_byte(0x8d36);
	// Rhythm ROM offset - set by header validation at LABEL_F54651
	// 0xFFFFFFFF means validation failed (rhythm ROM not usable)
	uint32_t rhythm_offset = space.read_dword(0x3277);
	// Sequencer startup flag - must be non-zero for Seq_StartMainControlAlt
	// to call F42EA4 (audio hardware configuration)
	uint16_t seq_start_flag = space.read_word(0x0251d8);
	// Rhythm ring buffer pointers (512-byte buffer at 0x01EF5D)
	uint16_t rhy_wr_ptr = space.read_word(0x01ef59);  // Write pointer
	uint16_t rhy_rd_ptr = space.read_word(0x01ef55);  // Read pointer
	// Event queue diagnostics (DispatchEvent stores last dispatched event here)
	uint32_t evt_last_id = space.read_dword(0x02bc24);    // Last dispatched event ID
	uint32_t evt_last_bc = space.read_dword(0x02bc28);    // Last dispatched event param1 (XBC)
	uint8_t evt_q_wr = space.read_byte(0x02f83a);         // Event queue write index
	uint8_t evt_q_rd = space.read_byte(0x02f838);         // Event queue read index

	TLOGMASKED(LOG_HEARTBEAT, "HEARTBEAT: MainCPU PC=%06X  SubCPU PC=%06X  SeqBuf wr=%04X rd=%04X  RhyBuf wr=%04X rd=%04X  evtloop=%u putc_mrx=%u rhythm_rom=%u rhy_bufwr=%u rhy_bufrd=%u  state=%02X rhy_ofs=%08X startflag=%04X  evtQ wr=%02X rd=%02X last=%08X/%08X\n",
		m_maincpu->pc(), m_subcpu->pc(), seq_wr_ptr, seq_rd_ptr,
		rhy_wr_ptr, rhy_rd_ptr,
		m_seq_event_loop_hits, m_putc_mrx_bf_hits, m_rhythm_rom_hits,
		m_rhythm_buf_writes, m_rhythm_buf_reads,
		seq_state, rhythm_offset, seq_start_flag,
		evt_q_wr, evt_q_rd, evt_last_id, evt_last_bc);
	m_seq_event_loop_hits = 0;
	m_putc_mrx_bf_hits = 0;
	m_rhythm_rom_hits = 0;
	m_rhythm_buf_writes = 0;
	m_rhythm_buf_reads = 0;
}


// Audio mixer/attenuator — register-indirect device at 0x150000/0x150002
// Firmware writes address to 0x150000, then data to 0x150002.
// Register map (from init code at EF17F4):
//   0x10-0x17, 0x90-0x97: 8 channel pairs (level/attenuation)
//   0x1F, 0x3F, 0x5F, 0x7F: channel group enable (stride 0x20)
void kn5000_state::audiomix_addr_w(uint8_t data)
{
	m_audiomix_addr = data;
	TLOGMASKED(LOG_AUDIOMIX, "AudioMix: addr = 0x%02X  PC=%06X\n",
		data, m_maincpu->pc());
}

void kn5000_state::audiomix_data_w(uint8_t data)
{
	TLOGMASKED(LOG_AUDIOMIX, "AudioMix: reg[0x%02X] = 0x%02X  PC=%06X\n",
		m_audiomix_addr, data, m_maincpu->pc());
	m_audiomix_regs[m_audiomix_addr] = data;
}

uint8_t kn5000_state::audiomix_data_r()
{
	uint8_t val = m_audiomix_regs[m_audiomix_addr];
	TLOGMASKED(LOG_AUDIOMIX, "AudioMix: reg[0x%02X] read = 0x%02X  PC=%06X\n",
		m_audiomix_addr, val, m_maincpu->pc());
	return val;
}

void kn5000_state::maincpu_mem(address_map &map)
{
	map(0x000000, 0x0fffff).ram().share("nvram1"); // 1Mbyte = 2 * 4Mbit DRAMs @ IC9, IC10 (CS3)
	// Button states and LED control are now handled via serial protocol to cpanel HLE device
	// FDC IC208 (uPD72068): CPU A1 -> FDC A0
	map(0x110008, 0x110008).rw(m_fdc, FUNC(upd72067_device::msr_r), FUNC(upd72067_device::auxcmd_w));
	map(0x11000a, 0x11000a).rw(m_fdc, FUNC(upd72067_device::fifo_r), FUNC(upd72067_device::fifo_w));
	// FDC DMA data port (software DMA ch3 transfers one byte per INT5/DRQ)
	map(0x120000, 0x120000).rw(m_fdc, FUNC(upd72067_device::dma_r), FUNC(upd72067_device::dma_w));
	map(0x140000, 0x14ffff).r(FUNC(kn5000_state::maincpu_latch_r)); // @ IC23 (logged wrapper)
	map(0x140000, 0x14ffff).w(FUNC(kn5000_state::subcpu_latch_w)); // @ IC22 (logged wrapper)
	map(0x150000, 0x150000).w(FUNC(kn5000_state::audiomix_addr_w));  // Audio mixer address port
	map(0x150002, 0x150002).rw(FUNC(kn5000_state::audiomix_data_r), FUNC(kn5000_state::audiomix_data_w)); // Audio mixer data port
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
	map(0x100000, 0x100001).rw(m_tonegen, FUNC(tc183c230002_device::config_addr_r), FUNC(tc183c230002_device::config_addr_w));   // Tone gen IC303 config address
	map(0x100002, 0x100003).rw(m_tonegen, FUNC(tc183c230002_device::config_data_r), FUNC(tc183c230002_device::config_data_w)); // Tone gen IC303 config data
	map(0x110000, 0x110001).r(m_tonegen, FUNC(tc183c230002_device::keyboard_data_r));   // Tone gen IC303 keybed data (HLE)
	map(0x110002, 0x110003).r(m_tonegen, FUNC(tc183c230002_device::keyboard_status_r)); // Tone gen IC303 keybed status (HLE)
	map(0x120000, 0x12ffff).r(FUNC(kn5000_state::subcpu_latch_r)); // @ IC22 (logged wrapper)
	map(0x120000, 0x12ffff).w(FUNC(kn5000_state::maincpu_latch_w)); // @ IC23 (logged wrapper)
	map(0x130000, 0x130001).w(m_dsp1, FUNC(ds3613gf3ba_device::addr_w));  // DSP1 (IC311) address register
	map(0x130002, 0x130003).rw(m_dsp1, FUNC(ds3613gf3ba_device::data_r), FUNC(ds3613gf3ba_device::data_w));  // DSP1 (IC311) data register
	map(0x1e0000, 0x1effff).noprw(); // Waveform/sample RAM (stub - not yet emulated)
	map(0xfe0000, 0xffffff).rom().region("subcpu", 0); // 1Mbit MASK ROM @ IC30

	// DSP2 (IC310, MN19413) uses SubCPU serial port 0
}

static void kn5000_floppies(device_slot_interface &device)
{
	// KN5000 uses 3.5" HD (1.44 MB) floppy drives — confirmed by:
	// - FDC format configuration supporting 1440K (18 sectors/track, 80 tracks)
	// - Firmware update disc images: FAT12, OEM-ID "Technics", 2880 sectors, 18 s/t
	device.option_add("35hd", FLOPPY_35_HD);
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
	save_item(NAME(m_subcpu_p7));
	save_item(NAME(m_subcpu_pz));
	save_item(NAME(m_subcpu_pe));
	save_item(NAME(m_subcpu_pf));
	save_item(NAME(m_dsp2_shift));
	save_item(NAME(m_dsp2_bit_count));
	save_item(NAME(m_comif_txd_state));
	save_item(NAME(m_comif_sclk_state));
	save_item(NAME(m_comif_shift));
	save_item(NAME(m_comif_bit_count));
	save_item(NAME(m_comif_receiving));
	save_item(NAME(m_audiomix_addr));
	save_item(NAME(m_audiomix_regs));
	save_item(NAME(m_seq_event_loop_hits));
	save_item(NAME(m_putc_mrx_bf_hits));
	save_item(NAME(m_rhythm_buf_writes));
	save_item(NAME(m_rhythm_buf_reads));

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

	// Heartbeat: snapshot CPU PCs every second for hang detection
	m_heartbeat_timer = timer_alloc(FUNC(kn5000_state::heartbeat), this);
	m_heartbeat_timer->adjust(attotime::from_seconds(1), 0, attotime::from_seconds(1));

	// SNS exit-check timer: fires at 60 Hz and triggers the real NMI handler
	// when the machine is about to exit (see class comment for details).
	m_sns_exit_check_timer = timer_alloc(FUNC(kn5000_state::sns_exit_check), this);
	m_sns_exit_check_timer->adjust(attotime::from_hz(60), 0, attotime::from_hz(60));

	// Sequencer ring buffer write tap: monitor pointer changes at 0x01F370-0x01F37A
	// Ring buffer header layout (base=0x01F37B, little-endian 16-bit words):
	//   0x01F371 (base-10): saved read pointer
	//   0x01F373 (base-8):  current read pointer
	//   0x01F375 (base-6):  saved write pointer
	//   0x01F377 (base-4):  current write pointer
	//   0x01F379 (base-2):  capacity remaining
	//
	// The firmware uses LDW (word store) to update these pointers.
	// On the 16-bit bus, each LDW generates one tap call at the aligned word address.
	m_maincpu->space(AS_PROGRAM).install_write_tap(
		0x01f370, 0x01f37b,
		"seqbuf_ptr_w",
		[this](offs_t offset, u16 &data, u16 mem_mask)
		{
			TLOGMASKED(LOG_SEQBUF, "SeqBuf write @%06X = %04X (mask=%04X) PC=%06X\n",
				offset, data, mem_mask, m_maincpu->pc());
		});

	// Execution detection: tap opcode fetches at key sequencer code addresses
	// Seq_ProcessEventLoop at 0xEF14CA — if hit, the event loop is running
	m_maincpu->space(AS_PROGRAM).install_read_tap(
		0xef14ca, 0xef14cb,
		"seq_event_loop_detect",
		[this](offs_t offset, u16 &data, u16 mem_mask)
		{
			m_seq_event_loop_hits++;
		});

	// putc_mrx_bf_X at 0xF1EDD4 — if hit, something is writing to the ring buffer
	m_maincpu->space(AS_PROGRAM).install_read_tap(
		0xf1edd4, 0xf1edd5,
		"putc_mrx_bf_detect",
		[this](offs_t offset, u16 &data, u16 mem_mask)
		{
			m_putc_mrx_bf_hits++;
		});

	// One-shot detection taps for DEMO mode code path
	// Addresses aligned to 16-bit word boundaries (required by bus width)
	// DemoMode_Main_Operation at 0xF8696F → tap at 0xF8696E
	m_maincpu->space(AS_PROGRAM).install_read_tap(
		0xf8696e, 0xf8696f,
		"demo_main_op_detect",
		[this](offs_t offset, u16 &data, u16 mem_mask)
		{
			TLOGMASKED(LOG_SEQBUF, "*** DemoMode_Main_Operation entered! PC=%06X\n", m_maincpu->pc());
		});

	// DemoMode_Initialize at 0xF869E3 → tap at 0xF869E2
	m_maincpu->space(AS_PROGRAM).install_read_tap(
		0xf869e2, 0xf869e3,
		"demo_init_detect",
		[this](offs_t offset, u16 &data, u16 mem_mask)
		{
			TLOGMASKED(LOG_SEQBUF, "*** DemoMode_Initialize entered! PC=%06X\n", m_maincpu->pc());
		});

	// Seq_StartMainControl at 0xF846BF → tap at 0xF846BE
	m_maincpu->space(AS_PROGRAM).install_read_tap(
		0xf846be, 0xf846bf,
		"seq_start_main_detect",
		[this](offs_t offset, u16 &data, u16 mem_mask)
		{
			TLOGMASKED(LOG_SEQBUF, "*** Seq_StartMainControl entered! PC=%06X\n", m_maincpu->pc());
		});

	// Seq_StartMainControlAlt at 0xF846CF → tap at 0xF846CE
	m_maincpu->space(AS_PROGRAM).install_read_tap(
		0xf846ce, 0xf846cf,
		"seq_start_alt_detect",
		[this](offs_t offset, u16 &data, u16 mem_mask)
		{
			TLOGMASKED(LOG_SEQBUF, "*** Seq_StartMainControlAlt entered! PC=%06X\n", m_maincpu->pc());
		});

	// Event queue monitoring: write pointer at 0x02F83A, count at 0x02F842
	m_maincpu->space(AS_PROGRAM).install_write_tap(
		0x02f838, 0x02f843,
		"evtqueue_ptr_w",
		[this](offs_t offset, u16 &data, u16 mem_mask)
		{
			TLOGMASKED(LOG_SEQBUF, "EvtQueue write @%06X = %04X (mask=%04X) PC=%06X\n",
				offset, data, mem_mask, m_maincpu->pc());
		});

	// Rhythm data ROM access counter (0x400000-0x7FFFFF)
	// Detects if firmware ever reads song data from the rhythm ROM
	m_maincpu->space(AS_PROGRAM).install_read_tap(
		0x400000, 0x7fffff,
		"rhythm_rom_detect",
		[this](offs_t offset, u16 &data, u16 mem_mask)
		{
			if (m_rhythm_rom_hits == 0)
				TLOGMASKED(LOG_SEQBUF, "*** First rhythm ROM read @%06X = %04X PC=%06X\n",
					offset, data, m_maincpu->pc());
			m_rhythm_rom_hits++;
		});

	// Sequencer state change detector - watches writes to (8D36h)
	// This is the master state machine variable for the sequencer.
	// Values 0x10-0x16 cause the sequencer dispatcher to skip ALL processing.
	m_maincpu->space(AS_PROGRAM).install_write_tap(
		0x8d36, 0x8d37,
		"seq_state_change",
		[this](offs_t offset, u16 &data, u16 mem_mask)
		{
			uint8_t new_state = data & 0xff;
			TLOGMASKED(LOG_SEQBUF, "*** Sequencer state change: (8D36h) = 0x%02X  PC=%06X\n",
				new_state, m_maincpu->pc());
		});

	// Rhythm ring buffer write detection — RhythmBuf_WriteByte at 0xEF2563
	// This is the entry point called by Rhythm_SendByte (0xF5549B) to push
	// bytes into the 512-byte rhythm ring buffer at 0x01EF5D.
	m_maincpu->space(AS_PROGRAM).install_read_tap(
		0xef2562, 0xef2563,
		"rhythm_buf_write_detect",
		[this](offs_t offset, u16 &data, u16 mem_mask)
		{
			m_rhythm_buf_writes++;
		});

	// Rhythm ring buffer read/dispatch detection — RhythmBuf_DispatchEvent at 0xEF1525
	// This reads events from the rhythm buffer and dispatches them to the
	// MIDI handler (RhythmMidi_Dispatcher at 0xFE0B06) for Note On, CC, etc.
	m_maincpu->space(AS_PROGRAM).install_read_tap(
		0xef1524, 0xef1525,
		"rhythm_buf_read_detect",
		[this](offs_t offset, u16 &data, u16 mem_mask)
		{
			m_rhythm_buf_reads++;
		});

	// SNS NMI emulation — mechanism (A): boot-time checksum write tap.
	// Boot_DisplayScreen clears DRAM[0xFFD4] = 0 on every boot (to invalidate
	// the stored checksum so the next power cycle without a clean NMI is detected).
	// We intercept that write and substitute the correct one's-complement checksum
	// of the current payload-shadow region, so SubCPU_Payload_Verify passes on
	// the same boot and shows the KN5000 logo animation instead of
	// "ALL INITIAL SETTING!".  See class comment for the full design rationale.
	m_maincpu->space(AS_PROGRAM).install_write_tap(
		0xFFD4, 0xFFD5,
		"sns_nmi_checksum_w",
		[this](offs_t offset, u16 &data, u16 mem_mask)
		{
			// Only intercept the 16-bit zero-write from Boot_DisplayScreen.
			if ((mem_mask & 0xFFFF) == 0xFFFF && data == 0x0000)
			{
				address_space &space = m_maincpu->space(AS_PROGRAM);
				// Region 1: 0xF180, 0x800 words — one's complement of sum
				uint32_t sum = 0;
				for (int i = 0; i < 0x800; i++)
					sum = (sum + space.read_word(0xF180 + i * 2)) & 0xFFFF;
				const uint16_t cksum1 = ~sum & 0xFFFF;
				// Region 2: 0xF980, 0x280 words
				sum = 0;
				for (int i = 0; i < 0x280; i++)
					sum = (sum + space.read_word(0xF980 + i * 2)) & 0xFFFF;
				const uint16_t cksum2 = ~sum & 0xFFFF;
				data = cksum1;                  // override what Boot_DisplayScreen writes
				space.write_word(0xFFD2, cksum2);
				LOGMASKED(LOG_BOOT, "SNS NMI (payload checksum): cksum1=0x%04X cksum2=0x%04X at PC=%06X\n",
					cksum1, cksum2, m_maincpu->pc());
			}
		});
}

void kn5000_state::machine_reset()
{
	m_checking_device_led_cn11 = 0;
	m_checking_device_led_cn12 = 0;

	// Clear keybed state (device resets are handled by device_reset())
	memset(m_keybed_prev, 0, sizeof(m_keybed_prev));

	// Initialize SubCPU GPIO ports to deasserted state (all bits high)
	m_subcpu_p7 = 0xff;
	m_subcpu_pz = 0x00;
	m_subcpu_pe = 0xff;
	m_subcpu_pf = 0x00;
	m_dsp2_shift = 0;
	m_dsp2_bit_count = 0;
	m_comif_txd_state = 1;  // Idle high
	m_comif_sclk_state = 0;
	m_comif_shift = 0;
	m_comif_bit_count = 0;
	m_comif_receiving = false;
	m_audiomix_addr = 0;
	memset(m_audiomix_regs, 0, sizeof(m_audiomix_regs));
}

// SNS NMI exit-check timer — mechanism (B): fire the real ROM handler at exit.
// Polls machine().exit_pending() at 60 Hz.  NMI_StorePayloadChecksums (EF08D4)
// checks the NMI guard in its own internal CPU RAM — if the guard is not 0x80
// (e.g. machine exits before Boot_DisplayScreen has run) it returns harmlessly.
// When the guard IS set the handler computes checksums, stores them in
// DRAM[0xFFD4/0xFFD2], and halts.  See class comment for timing caveats.
TIMER_CALLBACK_MEMBER(kn5000_state::sns_exit_check)
{
	if (!machine().exit_pending())
		return;

	m_maincpu->pulse_input_line(INPUT_LINE_NMI, attotime::from_usec(1));
	LOGMASKED(LOG_BOOT, "SNS NMI fired at exit (PC=%06X)\n", m_maincpu->pc());
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
	// Interrupt 4: FDC INTRQ (command completion)
	// Interrupt 5: FDC DRQ (data request — firmware handles via software DMA channel 3)
	// Interrupt 6: FDC.H/D (head load — handler is empty RETI)
	// Interrupt 7: FDC.I/O (disk change — handler is empty RETI)
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
		TLOGMASKED(LOG_RESET, "SubCPU reset: %s (PA=0x%02X) PC=%06X\n",
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
	// Bit 6: FD.I/O — floppy disk change signal
	m_maincpu->portd_read().set([this] () -> u8 {
		u8 data = 0;
		auto *conn = subdevice<floppy_connector>("fdc:0");
		floppy_image_device *floppy = conn ? conn->get_device() : nullptr;
		if (!floppy || floppy->dskchg_r())
			data |= 0x40;
		return data;
	});


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
			TLOGMASKED(LOG_HANDSHAKE, "MSTAT: %d -> %d (PZ=0x%02X) PC=%06X\n",
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
			TLOGMASKED(LOG_HANDSHAKE, "SSTAT: %d -> %d (PD=0x%02X) PC=%06X\n",
				m_sstat, new_sstat, data, m_subcpu->pc());
		m_sstat = new_sstat;
	});


	// SUBCPU PORT 7 (DSP1 parallel bus control):
	//   bit 3 = ~WR (write strobe, active low)
	//   bit 4 = ~RD (read strobe, active low)
	//   bit 5 = ~CS1 (DSP1 chip select, active low)
	//   bit 6 = C/~D (1=data, 0=command)
	m_subcpu->port7_write().set([this](u8 data) {
		m_subcpu_p7 = data;
	});


	// SUBCPU PORT Z (DSP1 parallel data bus):
	//   Firmware writes data byte with LD (PZ), A while WR+CS are active
	m_subcpu->portz_write().set([this](u8 data) {
		m_subcpu_pz = data;

		// Check if this is a DSP1 data transfer (WR asserted, CS1 selected)
		bool wr_active = !BIT(m_subcpu_p7, 3);
		bool cs1_active = !BIT(m_subcpu_p7, 5);

		if (wr_active && cs1_active)
		{
			bool is_command = !BIT(m_subcpu_p7, 6);  // bit 6: 0=command, 1=data
			if (is_command)
				m_dsp1->parallel_command_w(data);
			else
				m_dsp1->parallel_data_w(data);
		}
	});


	// SUBCPU PORT E:
	//   bit 6 = ~CS2 (DSP2 chip select, active low)
	//   bit 0 = MUTE (audio output mute control)
	m_subcpu->porte_write().set([this](u8 data) {
		uint8_t old_pe = m_subcpu_pe;
		m_subcpu_pe = data;

		// Audio mute control (PE.0) — confirmed by MUTE_AND_HALT routine at 0x9360
		if (BIT(old_pe, 0) != BIT(data, 0))
			LOGMASKED(LOG_SOUND, "Audio mute: %s\n", BIT(data, 0) ? "OFF (unmuted)" : "ON (muted)");

		// CS2 deassert (rising edge of PE.6) — end of DSP2 serial byte
		if (!BIT(old_pe, 6) && BIT(data, 6))
		{
			// Both DSP2_Send_Command and DSP2_Send_Data produce 9 SCLK rising edges:
			//   Command: 1 (ClockPulseHigh) + 7 (bit loop, 1st absorbed) + 1 (trailing) = 9
			//   Data:    8 (bit loop) + 1 (trailing) = 9
			// Shift right by 1 to discard the trailing bit, leaving 8 data bits.
			// The MN19413 device auto-detects command vs data by transaction state.
			if (m_dsp2_bit_count == 9)
			{
				uint8_t byte = (m_dsp2_shift >> 1) & 0xff;
				m_dsp2->parallel_data_w(byte);
			}
			else if (m_dsp2_bit_count > 0)
			{
				LOGMASKED(LOG_DSP2, "DSP2 frame: %d bits, shift=0x%04X\n",
					m_dsp2_bit_count, m_dsp2_shift);
			}
			m_dsp2_bit_count = 0;
			m_dsp2_shift = 0;
		}

		// CS2 assert (falling edge) — start of DSP2 serial transaction
		if (BIT(old_pe, 6) && !BIT(data, 6))
		{
			m_dsp2_bit_count = 0;
			m_dsp2_shift = 0;
		}
	});


	// SUBCPU PORT F (DSP2 bit-banged serial):
	//   bit 0 = SDA (serial data to DSP2)
	//   bit 2 = SCLK (serial clock to DSP2)
	m_subcpu->portf_write().set([this](u8 data) {
		uint8_t old_pf = m_subcpu_pf;
		m_subcpu_pf = data;

		// Detect SCLK rising edge (PF.2: 0->1) while CS2 is active
		bool cs2_active = !BIT(m_subcpu_pe, 6);
		if (!BIT(old_pf, 2) && BIT(data, 2) && cs2_active)
		{
			// Shift in data bit from PF.0 (MSB first)
			m_dsp2_shift = (m_dsp2_shift << 1) | BIT(data, 0);
			m_dsp2_bit_count++;
		}
	});


	// SUBCPU PORT H (DSP status):
	//   bit 0 = DSP ready (1=ready, 0=busy)
	m_subcpu->porth_read().set([this]() -> u8 {
		return 0x01;  // Always ready — prevents 8000-iteration timeout loops
	});


	// SubCPU serial port 0: DSP2 (IC310, MN19413)
	m_subcpu->m_serial[0].lookup()->txd().set(m_dsp2, FUNC(mn19413_device::rxd));
	m_subcpu->m_serial[0].lookup()->sclk_out().set(m_dsp2, FUNC(mn19413_device::sclk));

	// SubCPU serial port 1: Computer Interface (TO HOST connector)
	// Selected via COM_SELECT DIP switch (MIDI/PC1/PC2/Mac on main CPU Port Z)
	m_subcpu->m_serial[1].lookup()->tx_start().set([this](int state) {
		m_comif_receiving = true;
		m_comif_bit_count = 0;
		m_comif_shift = 0;
		TLOGMASKED(LOG_COMIF, "ComIF TX start (state=%d)\n", state);
	});
	m_subcpu->m_serial[1].lookup()->txd().set([this](int state) {
		m_comif_txd_state = state;
	});
	m_subcpu->m_serial[1].lookup()->sclk_out().set([this](int state) {
		// Rising edge while receiving: sample TXD bit (LSB first, UART convention)
		if (state && !m_comif_sclk_state && m_comif_receiving)
		{
			m_comif_shift >>= 1;
			m_comif_shift |= (m_comif_txd_state << 7);
			m_comif_bit_count++;
			if (m_comif_bit_count >= 8)
			{
				TLOGMASKED(LOG_COMIF, "ComIF TX: 0x%02X '%c'\n",
					m_comif_shift,
					(m_comif_shift >= 0x20 && m_comif_shift < 0x7f) ? (char)m_comif_shift : '.');
				m_comif_receiving = false;
			}
		}
		m_comif_sclk_state = state;
	});


	GENERIC_LATCH_8(config, m_maincpu_latch); // @ IC23
	m_maincpu_latch->data_pending_callback().set_inputline(m_maincpu, TLCS900_INT0);

	GENERIC_LATCH_8(config, m_subcpu_latch); //  @ IC22
	m_subcpu_latch->data_pending_callback().set_inputline(m_subcpu, TLCS900_INT0);

	/* Audio chips */
	TC183C230002(config, m_tonegen, 0);  // IC303 — tone generator, memory-mapped at 0x100000/0x110000
	DS3613GF3BA(config, m_dsp1, 0);      // IC311 — effect DSP, memory-mapped at 0x130000
	MN19413(config, m_dsp2, 0);          // IC310 — effect DSP, serial via SubCPU port 0

	// Audio output — tone generator produces stereo sine waves (placeholder, wavetable ROM undumped)
	SPEAKER(config, "lspeaker").front_left();
	SPEAKER(config, "rspeaker").front_right();
	m_tonegen->add_route(0, "lspeaker", 1.0);
	m_tonegen->add_route(1, "rspeaker", 1.0);

	UPD72067(config, m_fdc, 32'000'000); // actual controller is UPD72068GF-3B9 at IC208
	m_fdc->intrq_wr_callback().set_inputline(m_maincpu, TLCS900_INT4);
	m_fdc->drq_wr_callback().set_inputline(m_maincpu, TLCS900_INT5);
	// TODO: TC signal — maincpu Timer 0 output (TO0) wired to FDC TC input.
	// TMP94C241 timer output pin callbacks not yet implemented in MAME.
	// Multi-sector FDC transfers may not terminate correctly without TC.


	FLOPPY_CONNECTOR(config, "fdc:0", kn5000_floppies, "35hd", floppy_image_device::default_mfm_floppy_formats).enable_sound(true);

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
CONS(1998, kn5000,    0,       0, kn5000, kn5000, kn5000_state, empty_init, "Technics", "SX-KN5000", MACHINE_NOT_WORKING|MACHINE_IMPERFECT_SOUND)
