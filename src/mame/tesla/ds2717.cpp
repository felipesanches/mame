// license:BSD-3-Clause
// copyright-holders:Felipe Corrêa da Silva Sanches
/*******************************************************************************

    DS 2717 floppy disk controller (Consul 2717) -- see ds2717.h.

    Low-level emulation of the dumb i8272 board at host I/O ports 0xC8-0xCF,
    driven directly by the verified PLAIN c2717 system ROM.  The C8/C9 pair is
    the i8272's MSR / DATA register; CA is the board control latch + transfer-
    end flag; CC/CF are the 74LS193 hardware-seek counter and its load strobe.
    The i8272 runs in non-DMA mode and its per-byte INTRQ is forwarded to the
    host 8080 INTR (-> RST 7 -> the 0x0038 ISR the ROM installs).

*******************************************************************************/

#include "emu.h"
#include "ds2717.h"

#include "formats/upd765_dsk.h"

#define LOG_PROTO (1U << 1)   // per-access firehose (every C8-CF port touch)
#define LOG_CMD   (1U << 2)   // i8272 command/param bytes written to C9
#define LOG_SEEK  (1U << 3)   // CC/CF hardware-seek activity

#define VERBOSE (LOG_GENERAL | LOG_CMD | LOG_SEEK | LOG_PROTO)
#include "logmacro.h"

#define LOGPROTO(...) LOGMASKED(LOG_PROTO, __VA_ARGS__)
#define LOGCMD(...)   LOGMASKED(LOG_CMD,   __VA_ARGS__)
#define LOGSEEK(...)  LOGMASKED(LOG_SEEK,  __VA_ARGS__)

// cap the bring-up firehose so a stuck poll cannot fill the log
static constexpr uint32_t LOG_CAP = 4000;


DEFINE_DEVICE_TYPE(DS2717, ds2717_device, "ds2717", "Consul 2717 DS2717 disk controller")


ds2717_device::ds2717_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, DS2717, tag, owner, clock)
	, m_fdc(*this, "fdc")
	, m_floppy(*this, "fdc:%u", 0U)
	, m_int_cb(*this)
	, m_control(0)
	, m_seek_addr(0)
	, m_seek_phase(0)
	, m_intrq(0)
	, m_drq(0)
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

	FLOPPY_CONNECTOR(config, "fdc:0", ds2717_floppies, "8sssd", floppy_formats);
	FLOPPY_CONNECTOR(config, "fdc:1", ds2717_floppies, "8sssd", floppy_formats);
}


//-------------------------------------------------
//  i8272 INTRQ / DRQ
//-------------------------------------------------

void ds2717_device::fdc_intrq_w(int state)
{
	// End-of-command / result-phase interrupt.  The ROM's SPECIFY selects DMA mode
	// (ND=0), so INTRQ fires only at command completion -- NOT per data byte -- and
	// the board does NOT route it to the CPU (the result phase is polled via the MSR
	// at C8).  It feeds the CA bit0 transfer-END flag the read ISR waits on.
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
	// 8080's own EI/DI (IM_IE) gates delivery.
	m_drq = state;
	if (m_log_count < LOG_CAP)
	{
		LOGPROTO("DRQ -> %d (host INT)\n", state);
		m_log_count++;
	}
	m_int_cb(state);
}


//-------------------------------------------------
//  CC/CF 74LS193 hardware seek
//-------------------------------------------------

void ds2717_device::do_hardware_seek()
{
	// CF=0x30 latches the CC track-address image into the 74LS193 counters, which
	// then step the head to that physical track.  The ROM issues no i8272 SEEK in
	// the boot path, so move the floppy head ourselves; the subsequent i8272 READ
	// DATA then matches the sector headers' C field at that physical cylinder.
	int const target = m_seek_addr & 0x7f;   // 0..76 valid; only the low track bits matter

	// The 74LS193 step line is shared; drive-select (held in the CA latch upper
	// bits) gates which spindle actually moves.  Move whichever drives are
	// present to the target -- the unselected one is harmless for a read.
	for (int i = 0; i < 2; i++)
	{
		floppy_image_device *fd = m_floppy[i]->get_device();
		if (!fd)
			continue;

		int cur = fd->get_cyl();
		if (cur == target)
			continue;

		fd->dir_w(cur > target ? 1 : 0);   // 1 = step toward track 0
		while (cur != target)
		{
			// the floppy steps on the high->low edge of the STEP line, so raise
			// it then drop it to clock one track per pulse
			fd->stp_w(1);
			fd->stp_w(0);
			int const moved = fd->get_cyl();
			if (moved == cur)   // hit an end stop
				break;
			cur = moved;
		}
		LOGSEEK("HW seek drive %d -> track %d (now %d)\n", i, target, fd->get_cyl());
	}
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
		uint8_t const v = m_fdc->msr_r();
		if (m_log_count < LOG_CAP)
		{
			LOGPROTO("C8 read (MSR) -> %02X\n", v);
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
		uint8_t const v = m_drq ? m_fdc->dma_r() : m_fdc->fifo_r();
		if (m_log_count < LOG_CAP)
		{
			LOGPROTO("C9 read (FDC %s) -> %02X\n", m_drq ? "data" : "result", v);
			m_log_count++;
		}
		return v;
	}

	case 2:  // CA -- board status; bit0 = transfer-END / TC flag
	{
		// The ROM polls "IN CA; RRC; RC": bit0 set => the sector transfer has
		// finished.  The transfer is over when the i8272 leaves the execution phase
		// and raises INTRQ (command complete / result phase ready) after the last
		// data byte, so the end flag is INTRQ itself.  (During the transfer INTRQ is
		// low and bit0 stays 0, keeping the ISR's wait loop spinning.)
		uint8_t const v = m_intrq ? 0x01 : 0x00;
		if (m_log_count < LOG_CAP)
		{
			LOGPROTO("CA read (end-flag) -> %02X\n", v);
			m_log_count++;
		}
		return v;
	}

	default:  // CB, CC, CD, CE, CF -- no read path in the driver
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
		if (m_log_count < LOG_CAP)
		{
			LOGCMD("C9 write (FDC cmd/param) = %02X\n", data);
			m_log_count++;
		}
		m_fdc->fifo_w(data);
		break;

	case 2:  // CA -- control latch (74LS174): drive-select/motor/config + run gate (bit0)
	{
		// Observed: 0x2B idle/armed, 0x2A running (only bit0 differs); upper bits
		// (0x28) are static drive/motor/config.  No reset value or TC pulse is
		// written here in this path -- TC is generated by the board's decode logic.
		if (m_log_count < LOG_CAP)
		{
			LOGPROTO("CA write (control) = %02X%s\n", data,
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

	case 4:  // CC -- 74LS193 hardware-seek track address: low byte then high byte
		if (m_seek_phase == 0)
		{
			m_seek_addr = (m_seek_addr & 0xff00) | data;
			m_seek_phase = 1;
		}
		else
		{
			m_seek_addr = (m_seek_addr & 0x00ff) | (uint16_t(data) << 8);
			m_seek_phase = 0;
		}
		LOGSEEK("CC write (seek addr %s) = %02X -> addr=%04X\n",
			m_seek_phase ? "lo" : "hi", data, m_seek_addr);
		break;

	case 7:  // CF -- address-load strobe; 0x30 latches CC into the 74LS193 and seeks
		LOGSEEK("CF write (strobe) = %02X\n", data);
		if (data == 0x30)
		{
			m_seek_phase = 0;   // the strobe restarts the lo/hi byte sequence
			do_hardware_seek();
		}
		break;

	default:  // C8 (MSR is read-only), CB, CD, CE -- no write path in the driver
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
	save_item(NAME(m_seek_addr));
	save_item(NAME(m_seek_phase));
	save_item(NAME(m_intrq));
	save_item(NAME(m_drq));
	save_item(NAME(m_log_count));
}

void ds2717_device::device_reset()
{
	m_control = 0;
	m_seek_addr = 0;
	m_seek_phase = 0;
	m_intrq = 0;
	m_drq = 0;
	m_log_count = 0;

	// i8272a has has_dor=false, so upd765_family_device::device_reset() leaves
	// dor=0 and soft_reset() never runs end_reset(); main_phase stays PHASE_IDLE
	// and msr_r() returns 0x00, which fails the ROM presence gate (IN C8; XRI 80h
	// needs MSR==0x80).  Pulse reset_w to drive dor bit2 through end_reset() so
	// main_phase becomes PHASE_CMD and msr_r() returns MSR_RQM (0x80) idle.
	m_fdc->reset_w(1);
	m_fdc->reset_w(0);
}
