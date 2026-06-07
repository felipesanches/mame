// license:BSD-3-Clause
// copyright-holders:Felipe Corrêa da Silva Sanches
/*******************************************************************************

    DS 2717 floppy disk controller (Consul 2717) -- see ds2717.h.

    Low-level emulation of the dumb i8272 board at host I/O ports 0xC8-0xCF,
    driven directly by the verified PLAIN c2717 system ROM.  The C8/C9 pair is
    the i8272's MSR / DATA register; CA is the board control latch + transfer-
    end flag; CC/CF are the 8253 PIT counter-0 (the per-sector transfer byte
    counter) and its control-word register.  The i8272 runs in DMA mode and its
    per-byte DRQ is forwarded to the host 8080 INTR (-> RST 7 -> the 0x0038 ISR
    the ROM installs), which reads each byte from C9 via dma_r.

*******************************************************************************/

#include "emu.h"
#include "ds2717.h"

#include "formats/upd765_dsk.h"

#define LOG_PROTO (1U << 1)   // per-access firehose (every C8-CF port touch)
#define LOG_CMD   (1U << 2)   // i8272 command/param bytes written to C9
#define LOG_SEEK  (1U << 3)   // CC/CF 8253 transfer-byte-counter activity

#define VERBOSE (LOG_GENERAL | LOG_CMD | LOG_SEEK | LOG_PROTO)
#include "logmacro.h"

#define LOGPROTO(...) LOGMASKED(LOG_PROTO, __VA_ARGS__)
#define LOGCMD(...)   LOGMASKED(LOG_CMD,   __VA_ARGS__)
#define LOGSEEK(...)  LOGMASKED(LOG_SEEK,  __VA_ARGS__)

// cap the bring-up firehose so a stuck poll cannot fill the log -- high enough
// to capture a full multi-sector transfer (a CP/M boot moves ~6.5 KB) and its TC
// + result phase, which a 4000-entry cap was truncating mid-stream.
static constexpr uint32_t LOG_CAP = 20000;


DEFINE_DEVICE_TYPE(DS2717, ds2717_device, "ds2717", "Consul 2717 DS2717 disk controller")


ds2717_device::ds2717_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, DS2717, tag, owner, clock)
	, m_fdc(*this, "fdc")
	, m_floppy(*this, "fdc:%u", 2U)   // drives are at i8272 units 2 (A) and 3 (B)
	, m_pit(*this, "pit")
	, m_int_cb(*this)
	, m_control(0)
	, m_byte_count(0)
	, m_count_phase(0)
	, m_count_active(false)
	, m_count_done(false)
	, m_intrq(0)
	, m_drq(0)
	, m_ca_last(0)
	, m_log_count(0)
{
}


namespace {

// Consul 2717 8" disc: IBM 3740-style single-sided single-density (FM),
// 77 tracks x 26 sectors x 128 bytes (= 256256 bytes), matching the dumps.
class ds2717_disc_format : public upd765_format
{
public:
	ds2717_disc_format() : upd765_format(formats) { }
	const char *name() const noexcept override { return "ds2717"; }
	const char *description() const noexcept override { return "Consul 2717 8\" disk image"; }
	const char *extensions() const noexcept override { return "img,dz8,p32"; }
private:
	static const format formats[];
};

const ds2717_disc_format::format ds2717_disc_format::formats[] = {
	{
		floppy_image::FF_8, floppy_image::SSSD, floppy_image::FM,
		2000, // 8" cell rate (2 us); FM spends 2 cells/bit => 250 kbps
		26, 77, 1,
		128, {},
		1, {},
		40, 26, 11, 27   // IBM 3740 FM gaps
	},
	{}
};

const ds2717_disc_format FLOPPY_DS2717_FORMAT;

} // anonymous namespace

void ds2717_device::floppy_formats(format_registration &fr)
{
	fr.add(FLOPPY_DS2717_FORMAT);
	fr.add_mfm_containers();   // also accept HxC/MFI containers
}

static void ds2717_floppies(device_slot_interface &device)
{
	device.option_add("8sssd", FLOPPY_8_SSSD);   // IBM-3740 single-sided single-density 8"
}

void ds2717_device::device_add_mconfig(machine_config &config)
{
	I8272A(config, m_fdc, 8'000'000);   // clocked by the board's 8 MHz oscillator
	m_fdc->intrq_wr_callback().set(FUNC(ds2717_device::fdc_intrq_w));
	m_fdc->drq_wr_callback().set(FUNC(ds2717_device::fdc_drq_w));

	// The board wires its two drives to the i8272's unit-select lines 2 and 3, not
	// 0/1: the ROM builds the command unit byte as (2 | drive) -- drive A = unit 2
	// (@0x971E: XRA A; STC; RAL; RLC -> 2; ORA [CFB4]), drive B = unit 3.  So the
	// connectors are fdc:2 / fdc:3 (= the i8272a's flopi[2] / flopi[3]).
	FLOPPY_CONNECTOR(config, "fdc:2", ds2717_floppies, "8sssd", floppy_formats);
	FLOPPY_CONNECTOR(config, "fdc:3", ds2717_floppies, "8sssd", floppy_formats);

	// 8253 PIT (IC10).  Counter 0 is the per-sector byte counter, hand-rolled in
	// read()/write() because its OUT0 must pulse the i8272 TC and the CA bit0 end
	// flag at terminal count -- behaviour a Mode-0 pit8253 clocked by the (unknown)
	// system divisor would not reproduce -- so it is NOT wired here.
	//
	// Counters 1 and 2 are a two-stage cascade that the on-disc CP/M BIOS uses as
	// its keyboard auto-repeat / debounce timebase (NOT a disk timer): at boot it
	// programs counter 1 = 10000 (Mode 2 rate generator) and counter 2 = 0 (= 65536,
	// Mode 2, free-running), then in CONST it latches counter 2 (CF=0x80; IN CE x2)
	// and times the elapsed down-count against the repeat thresholds 20 (initial
	// ~98 ms) and 3 (repeat ~15 ms).  Those thresholds only make sense at a ~200 Hz
	// counter-2 tick, which is exactly counter 1's output: CLK1 is the CPU
	// instruction-cycle clock (the maincpu's XTAL(18'432'000)/9 = 2'048'000 Hz) and
	// OUT1 feeds CLK2 (10000-divide -> 204.8 Hz).  Wiring OUT1 -> CLK2 (rather than
	// giving counter 2 its own free-running tap) keeps the BIOS timebase at the
	// intended rate; an independent fast clock makes the CONST elapsed-vs-threshold
	// test pass on every poll, firing the heavy ROM keyboard-rescan/auto-repeat
	// branch continuously and crawling spurious characters across the framebuffer.
	// OUT2 stays unbound (the loader only reads back the latched counter-2 value).
	PIT8253(config, m_pit, 0);
	m_pit->set_clk<1>(XTAL(18'432'000) / 9);   // CLK1 = CPU instruction-cycle clock (2.048 MHz)
	m_pit->out_handler<1>().set(m_pit, FUNC(pit8253_device::write_clk2));   // OUT1 -> CLK2 cascade
}


//-------------------------------------------------
//  i8272 INTRQ / DRQ
//-------------------------------------------------

void ds2717_device::fdc_intrq_w(int state)
{
	// End-of-command / result-phase interrupt.  The ROM's SPECIFY selects DMA mode
	// (ND=0), so INTRQ fires only at command completion -- NOT per data byte -- and
	// the board does NOT route it to the CPU.  It contributes the SEEK/RECALIBRATE-
	// completion term of the CA bit0 end-flag OR (those waits move no data and so
	// cannot use the byte counter).  It self-clears when the ROM reads out the i8272
	// result phase (the 7 fifo_r in helper 0x96FA).
	m_intrq = state;
	if (m_log_count < LOG_CAP)
	{
		LOGPROTO("INTRQ -> %d (end flag)\n", state);
		m_log_count++;
	}
}

void ds2717_device::fdc_drq_w(int state)
{
	// DMA mode (ND=0): the i8272 raises DRQ once per execution-phase byte.  The board
	// has no DMA controller -- it wires DRQ to the 8080 INTR line, so each byte fires
	// the RST 7 ISR (at RAM 0x0038), which reads the byte from C9 via dma_r.  The
	// 8080's own EI/DI (IM_IE) gates delivery.  DRQ also gates the per-byte 8253
	// counter-0 decrement in read() case 1.
	m_drq = state;
	// DRQ toggles once per data byte (~6.5 KB/boot) -- not logged, it would flood
	// the cap; the C9 command writes, TC pulses and result phase are what matter.
	m_int_cb(state);
}


//-------------------------------------------------
//  host access, offset 0..7 = ports 0xC8..0xCF
//-------------------------------------------------

uint8_t ds2717_device::read(offs_t offset)
{
	switch (offset & 7)
	{
	case 0:  // C8 -- i8272 MAIN STATUS REGISTER (MSR): bit7=RQM, bit6=DIO
	{
		uint8_t v = m_fdc->msr_r();

		// ROM 0x9581 ("IN C8; XRI 80h; RNZ") is a DUAL-purpose entry the firmware
		// reaches two ways:
		//   (a) the power-on FDC-presence gate: the cold-boot path runs it BEFORE
		//       its first OUT CA (the IN C8 at 0x9581 precedes OUT CA=2B at 0x9592),
		//       and FALLS THROUGH (RNZ not taken) into the bootstrap only when the
		//       status is exactly 0x80 -- "a present, idle controller, boot it";
		//   (b) the on-disc BIOS read finalize: every successful sector read ends
		//       at 0xCDBE "MVI L,00; RST 2" -> RAM bank thunk -> ROM service 0 =
		//       JMP 0x9581, which here must RETURN to the BIOS (continuing at
		//       0xCDC1: status, OUT CA=2B, RET).  It returns only when the status
		//       is NOT exactly 0x80 -- a bare XRI 80h, no ANI C0 mask (unlike the
		//       command-send wait at 0x972C), so any low bit suffices.
		// So the SAME instruction must read 0x80 at pristine power-on yet non-0x80
		// at every later finalize.  The discriminator on real hardware is not the
		// instantaneous i8272 MSR (which is a bare idle 0x80 at the finalize too --
		// the read's result phase has long since been drained by 0xCDBE): the board
		// C8 status carries a low "controller has been operated" bit, set the first
		// time the host drives the control latch and reflected in C8 thereafter, so
		// only the never-touched cold-start state reads a true 0x80.
		//
		// Model that: once any OUT CA has happened (m_control != 0 -- it is 0 only
		// at device_reset, before the power-on presence gate) and the i8272 is
		// otherwise idle (bare MSR == MSR_RQM), present a low status bit so 0x9581's
		// RNZ is taken and the read finalises instead of cold-rebooting.  The bit is
		// invisible to every other C8 reader (all mask ANI C0); only the bare
		// XRI 80h at 0x9581 sees it.  Use bit 2 (the boot unit's DnB -- the ROM
		// builds unit bytes as (2|drive) @0x971E, drive A = unit 2).
		//
		// CRUCIAL: gate on m_control != 0, NOT on a specific running value: the
		// finalize is reached with the latch already at its 0x2B idle value (the
		// BIOS writes CA=2B BEFORE the path that re-enters 0x9581), so an "== 0x2A"
		// (running) test would miss it and still cold-boot; and a "bit0 == 0" test
		// would wrongly fire at the m_control==0 power-on presence gate and report
		// "no controller", never booting.  m_control != 0 excludes only that reset
		// state.
		if (v == 0x80 && m_control != 0)
			v |= (1 << 2);   // board "operated" status; bare 0x9581 gate returns, masked elsewhere

		if (m_log_count < LOG_CAP)
		{
			LOGPROTO("%s C8 read (MSR) -> %02X\n", machine().describe_context(), v);
			m_log_count++;
		}
		return v;
	}

	case 1:  // C9 -- i8272 DATA register: execution-phase data (DMA) + result bytes
	{
		// The ROM's SPECIFY sets ND=0 (DMA mode), so execution-phase sector bytes
		// arrive through the DMA read path (dma_r), gated by DRQ.  In DMA mode the
		// i8272 never sets internal_drq, so fifo_r() would return 0xFF mid-read
		// (upd765 fifo_r PHASE_EXEC needs internal_drq).  Result-phase bytes (DRQ
		// low, polled via the MSR) are read normally through fifo_r().
		if (m_drq)
		{
			uint8_t const v = m_fdc->dma_r();   // deliver byte N to the CPU buffer FIRST

			// Each byte clocks the 8253 counter-0 once.  Preload 127, decrement per
			// byte: byte 1 -> 127->126 ... byte 127 -> 1->0; on the 128th byte the
			// count is already 0 -> borrow -> terminal count.  The board's OUT0 then
			// pulses the i8272 TC (no DMA controller does it) to end the single-sector
			// execution phase, and latches the CA bit0 transfer-END flag.
			if (m_count_active && !m_count_done)
			{
				if (m_byte_count == 0)
				{
					m_count_done = true;
					m_fdc->tc_w(1);   // edge-triggered: pulse high then low
					m_fdc->tc_w(0);
					if (m_log_count < LOG_CAP)
					{
						LOGSEEK("byte-counter terminal count -> TC pulse\n");
						m_log_count++;
					}
				}
				else
				{
					m_byte_count--;
				}
			}

			// per-byte data is NOT logged: ~6.5 KB/boot would blow the cap and hide
			// the TC pulse + result phase that tell us how each read ended.  The TC
			// pulse (LOGSEEK above) marks a transfer reaching its byte count.
			return v;
		}
		else
		{
			uint8_t const v = m_fdc->fifo_r();
			if (m_log_count < LOG_CAP)
			{
				LOGPROTO("%s C9 read (FDC result) -> %02X\n", machine().describe_context(), v);
				m_log_count++;
			}
			return v;
		}
	}

	case 2:  // CA -- board status; bit0 = transfer-END flag
	{
		// The ROM polls "IN CA; RRC; RC": bit0 set => the wait is over.  The SAME
		// 0x003E poll loop ends both data transfers AND seek/recalibrate waits, so
		// bit0 must satisfy both:
		//   - data read: the 8253 counter-0 reaches terminal count after the 128th
		//     byte (m_count_done) -- this is the board's OUT0 transfer-END net.
		//   - SEEK/RECALIBRATE: no data moves, so m_count_done never sets; those
		//     waits end on the i8272 end-of-command INTRQ (m_intrq).
		// Hence bit0 = (byte-counter done) OR (i8272 INTRQ).  In DMA mode INTRQ
		// fires only at command_end (never per byte), so it cannot prematurely
		// satisfy a mid-transfer poll.
		uint8_t const v = (m_count_done || m_intrq) ? 0x01 : 0x00;

		// The poll spins thousands of times per wait; log only the 0->1/1->0 edge so
		// the bring-up trace shows the transition that ends each wait, not the storm.
		if (v != m_ca_last)
		{
			if (m_log_count < LOG_CAP)
			{
				LOGPROTO("%s CA read (end-flag) %d->%d\n", machine().describe_context(), m_ca_last, v);
				m_log_count++;
			}
			m_ca_last = v;
		}
		return v;
	}

	case 6:  // CE -- 8253 counter-2 latched read-back (second-stage loader)
	{
		// The loader latches counter 2 (CF=0x80) then does two IN CE to read the
		// 16-bit count LSB-then-MSB, and times elapsed = saved_start - count.  Map
		// straight onto the pit's counter-2 register (pit offset 2); the running
		// Mode-2 counter makes the latched value advance so the loader's wait ends.
		uint8_t const v = m_pit->read(2);
		if (m_log_count < LOG_CAP)
		{
			LOGSEEK("CE read (counter2 latch) -> %02X\n", v);
			m_log_count++;
		}
		return v;
	}

	default:  // CB, CC, CD, CF -- no read path in the driver
		if (m_log_count < LOG_CAP)
		{
			LOGPROTO("C%X read (unused) -> FF\n", 8 + (offset & 7));
			m_log_count++;
		}
		return 0xff;
	}
}

void ds2717_device::write(offs_t offset, uint8_t data)
{
	switch (offset & 7)
	{
	case 1:  // C9 -- i8272 DATA register: command + parameter bytes
		{
			// The board's drives are single-sided 8" (SSSD, head 0 only), but the ROM
			// issues its READ DATA commands as 0x86 = READ DATA + MT (multi-track).  On
			// a two-sided drive MT lets one command span both heads of a cylinder; on a
			// single-sided drive it is wrong, and the i8272 honours it literally: when
			// the read reaches R == EOT it flips to head 1 and, unless the board's TC net
			// has ALREADY fired, keeps searching head 1 (upd765.cpp:2028-2050).  The hand-
			// rolled byte counter pulses TC only when the CPU consumes the final byte,
			// which (because the i8272 FIFO runs up to 16 bytes ahead of the CPU) is too
			// late: the EOT/MT branch is evaluated as soon as the live engine PUSHES the
			// last byte, with TC still low.  For any read whose last needed sector is the
			// EOT sector (the boot's track-1 BIOS read covers R=1..26, EOT=26) the i8272
			// then diverts to an endless head-1 ID search on the empty side, never
			// reaching the result phase, so MSR never returns to the 0x80 the ROM's
			// command-send wait needs and the loader re-runs its boot forever.
			//
			// Model the board for what it physically is -- single-sided -- by masking the
			// MT bit off the READ DATA / READ DELETED DATA opcode before it reaches the
			// i8272.  Without MT the command always terminates at the head-0 EOT (the
			// !(MT) guard at upd765.cpp:2036 is taken), going cleanly to the result phase
			// whether or not TC has fired, so the byte counter alone bounds the transfer
			// (its only real job on this medium) and every data byte is still delivered.
			// Gate strictly on the opcode slot: MSR == 0x80 (RQM set, CB clear) means the
			// i8272 is idle and this write is command[0], so we never touch a parameter
			// or a data byte; and only opcodes whose low five bits are READ DATA (0x06)
			// or READ DELETED DATA (0x0C) are rewritten.
			uint8_t out = data;
			if ((m_fdc->msr_r() & 0xd0) == 0x80 && BIT(out, 7))
			{
				uint8_t const op = out & 0x1f;
				if (op == 0x06 || op == 0x0c)
				{
					out &= 0x7f;   // strip MT: this board is single-sided
					if (m_log_count < LOG_CAP)
					{
						LOGCMD("C9 read opcode %02X -> %02X (MT masked, single-sided)\n", data, out);
						m_log_count++;
					}
				}
			}
			if (m_log_count < LOG_CAP)
			{
				LOGCMD("%s C9 write (FDC cmd/param) = %02X\n", machine().describe_context(), out);
				m_log_count++;
			}
			m_fdc->fifo_w(out);
			break;
		}

	case 2:  // CA -- control latch (74LS174): drive-select/motor/config + run gate (bit0)
	{
		// Observed: 0x2B idle/armed, 0x2A running (only bit0 differs); upper bits
		// (0x28) are static drive/motor/config.  No reset value or TC pulse is
		// written here in this path -- TC is generated by the board's decode logic.
		if (m_log_count < LOG_CAP)
		{
			LOGPROTO("%s CA write (control) = %02X%s\n", machine().describe_context(), data,
				BIT(data, 0) ? " [idle/armed]" : " [running]");
			m_log_count++;
		}
		m_control = data;

		// upper bits hold motor-on/drive-select; spin the present drives so the
		// i8272 sees a ready, indexing disc during the operation.
		for (int i = 0; i < 2; i++)
			if (floppy_image_device *fd = m_floppy[i]->get_device())
				fd->mon_w(0);   // board keeps the spindle running while armed
		break;
	}

	case 4:  // CC -- 8253 counter-0 preload, written LSB then MSB (RL=11 from CF=0x30)
		// The ROM does two OUT CC per sector: LSB=0x7F then MSB=0x00, yielding the
		// 16-bit preload 0x007F = 127 = (sector_size - 1).  Keep both writes so the
		// trailing 0x00 MSB does not clobber the 0x7F low byte.
		if (m_count_phase == 0)
		{
			m_byte_count = (m_byte_count & 0xff00) | data;
			m_count_phase = 1;
		}
		else
		{
			m_byte_count = (m_byte_count & 0x00ff) | (uint16_t(data) << 8);
			m_count_phase = 0;
		}
		LOGSEEK("CC write (counter %s) = %02X -> preload=%04X\n",
			m_count_phase ? "LSB" : "MSB", data, m_byte_count);
		break;

	case 5:  // CD -- 8253 counter-1 preload (second-stage loader) -> pit counter 1
		LOGSEEK("CD write (counter1) = %02X\n", data);
		m_pit->write(1, data);
		break;

	case 6:  // CE -- 8253 counter-2 preload (second-stage loader) -> pit counter 2
		LOGSEEK("CE write (counter2) = %02X\n", data);
		m_pit->write(2, data);
		break;

	case 7:  // CF -- 8253 control word.  SC (bits 7-6) selects the counter.
	{
		uint8_t const sc = data >> 6;
		LOGSEEK("CF write (control word) = %02X (SC=%d)\n", data, sc);
		if (sc == 0)
		{
			// Counter 0 = the hand-rolled per-sector byte counter (e.g. 0x30:
			// LSB-then-MSB, Mode 0, binary).  Arm it: restart the LSB/MSB load
			// sequence and clear the terminal-count latch.  The preload arrives
			// via the two OUT CC that follow.  No head movement here -- the
			// i8272's own RECALIBRATE/SEEK over C9 position the head.
			m_count_phase = 0;
			m_count_active = true;
			m_count_done = false;
		}
		else
		{
			// Counter 1/2 control words (SC=01/10: the loader's 0x74/0xB4 Mode-2
			// rate generators and the 0x80 counter-2 latch command) -> pit8253.
			m_pit->write(3, data);
		}
		break;
	}

	default:  // C8 (MSR is read-only), CB -- no write path in the driver
		if (m_log_count < LOG_CAP)
		{
			LOGPROTO("C%X write (unused) = %02X\n", 8 + (offset & 7), data);
			m_log_count++;
		}
		break;
	}
}


//-------------------------------------------------
//  device_t
//-------------------------------------------------

void ds2717_device::device_start()
{
	save_item(NAME(m_control));
	save_item(NAME(m_byte_count));
	save_item(NAME(m_count_phase));
	save_item(NAME(m_count_active));
	save_item(NAME(m_count_done));
	save_item(NAME(m_intrq));
	save_item(NAME(m_drq));
	save_item(NAME(m_ca_last));
	save_item(NAME(m_log_count));
}

void ds2717_device::device_reset()
{
	m_control = 0;
	m_byte_count = 0;
	m_count_phase = 0;
	m_count_active = false;
	m_count_done = false;
	m_intrq = 0;
	m_drq = 0;
	m_ca_last = 0;
	m_log_count = 0;

	// i8272a has has_dor=false, so upd765_family_device::device_reset() leaves
	// dor=0 and soft_reset() never runs end_reset(); main_phase stays PHASE_IDLE
	// and msr_r() returns 0x00, which fails the ROM presence gate (IN C8; XRI 80h
	// needs MSR==0x80).  Pulse reset_w to drive dor bit2 through end_reset() so
	// main_phase becomes PHASE_CMD and msr_r() returns MSR_RQM (0x80) idle.
	m_fdc->reset_w(1);
	m_fdc->reset_w(0);

	// The i8272 has no data-rate register; its read/write clock comes from the
	// board's data separator (8224 8 MHz -> 74LS193 chain).  For 8" FM that is a
	// 500 kHz cell rate (2 us cells, matching the format's cell_size=2000); the
	// FM live PLL runs at cur_rate, so it must be 500000.  Left at the 250000
	// default the PLL samples at half the cell rate and never locks onto an
	// address mark (READ DATA fails ST1 = missing-address-mark, no data).
	m_fdc->set_rate(500000);
}
