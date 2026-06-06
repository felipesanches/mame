// license:BSD-3-Clause
// copyright-holders:Felipe Corrêa da Silva Sanches
/*******************************************************************************

    DS 2717 floppy disk controller (Consul 2717) -- see ds2717.h.

    First cut: wires the real i8272 FDC at the host ports 0xF4-0xF7 and
    instruments the surrounding board glue (0xF4 control, 0xF5 status) so host
    traces can pin the remaining bit assignments down.  Driven by the Consul's
    own (verified) system ROM.

*******************************************************************************/

#include "emu.h"
#include "ds2717.h"

#include "formats/upd765_dsk.h"

#define LOG_PROTO (1U << 1)   // per-access firehose (F5/F4 polling)

#define VERBOSE (LOG_GENERAL)
#include "logmacro.h"

#define LOGPROTO(...) LOGMASKED(LOG_PROTO, __VA_ARGS__)


DEFINE_DEVICE_TYPE(DS2717, ds2717_device, "ds2717", "Consul 2717 DS2717 disk controller")


ds2717_device::ds2717_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, DS2717, tag, owner, clock)
	, m_fdc(*this, "fdc")
	, m_floppy(*this, "fdc:%u", 0U)
	, m_control(0)
	, m_f6(0)
	, m_f5_track(0)
	, m_intrq(0)
	, m_drq(0)
{
}


namespace {

// Consul 2717 8" disc: IBM 3740-style single-sided single-density (FM),
// 77 tracks x 26 sectors x 128 bytes (= 256256 bytes), matching the friend's dumps.
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
	device.option_add("8dsdd", FLOPPY_8_DSDD);
}

void ds2717_device::device_add_mconfig(machine_config &config)
{
	I8272A(config, m_fdc, 8'000'000);   // clocked by the board's 8224 / 8 MHz
	m_fdc->intrq_wr_callback().set(FUNC(ds2717_device::fdc_intrq_w));
	m_fdc->drq_wr_callback().set(FUNC(ds2717_device::fdc_drq_w));

	FLOPPY_CONNECTOR(config, "fdc:0", ds2717_floppies, "8dsdd", floppy_formats);
	FLOPPY_CONNECTOR(config, "fdc:1", ds2717_floppies, "8dsdd", floppy_formats);
}


//-------------------------------------------------
//  host access, offset 0..3 = ports 0xF4..0xF7
//-------------------------------------------------

uint8_t ds2717_device::read(offs_t offset)
{
	switch (offset & 3)
	{
	case 0:  // 0xF4 -- control latch / drive status (bit layout WIP)
		LOGPROTO("F4 read -> %02X\n", m_control);
		return m_control;

	case 1:  // 0xF5 -- board status (muxed; bit assignments from ROM reverse-engineering)
	{
		// bit6 (0x40) = active-high "ready" strobe the firmware gates on (IN F5; ANI 40h);
		// bit5 (0x20) = network/console TX-ready; bits0-6 = head-position counter the seek
		// routine verifies against the target track; bit7 = write-protect/index (0 = ready).
		// WIP: asserting ready+TX-ready to break the boot's stuck F5 poll, then refine the
		// muxed head-position behaviour from the next host trace.
		uint8_t const v = 0x40 | 0x20 | (m_f5_track & 0x1f);
		LOGPROTO("F5 read -> %02X\n", v);
		return v;
	}

	case 2:  // 0xF6 -- i8272 main status register
	{
		uint8_t const v = m_fdc->msr_r();
		LOGPROTO("F6 read (MSR) -> %02X\n", v);
		return v;
	}

	case 3:  // 0xF7 -- i8272 data register
	{
		uint8_t const v = m_fdc->fifo_r();
		LOG("F7 read (FDC data) -> %02X\n", v);
		return v;
	}
	}
	return 0xff;
}

void ds2717_device::write(offs_t offset, uint8_t data)
{
	switch (offset & 3)
	{
	case 0:  // 0xF4 -- drive-select / motor / head-step (bit layout WIP)
		LOG("F4 write %02X\n", data);
		m_control = data;
		for (int i = 0; i < 2; i++)
			if (floppy_image_device *fd = m_floppy[i]->get_device())
				fd->mon_w(BIT(data, i) ? 0 : 1);
		break;

	case 1:  // 0xF5
		LOG("F5 write %02X\n", data);
		break;

	case 2:  // 0xF6 -- control strobe (the ROM toggles bit 1: TC/reset? WIP)
		LOG("F6 write %02X\n", data);
		m_f6 = data;
		break;

	case 3:  // 0xF7 -- i8272 data register (command + write data)
		LOG("F7 write (FDC cmd/data) = %02X\n", data);
		m_fdc->fifo_w(data);
		break;
	}
}


//-------------------------------------------------
//  device_t
//-------------------------------------------------

void ds2717_device::device_start()
{
	save_item(NAME(m_control));
	save_item(NAME(m_f6));
	save_item(NAME(m_f5_track));
	save_item(NAME(m_intrq));
	save_item(NAME(m_drq));
}

void ds2717_device::device_reset()
{
	m_control = 0;
	m_f6 = 0;
	m_f5_track = 0;
}
