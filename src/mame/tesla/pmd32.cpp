// license:BSD-3-Clause
// copyright-holders:Felipe Corrêa da Silva Sanches
/*******************************************************************************

    PMD-32 floppy disk unit (low-level emulation) -- see pmd32.h.

    This models the real PMD-32 hardware (8080A + 8255 + 8272A FDC + 8257 DMA +
    two 5.25" drives) running the unit's own control program.  WIP: the DMA/FDC
    inter-chip wiring and the host parallel link are a first cut and are not yet
    verified on a host build.

*******************************************************************************/

#include "emu.h"
#include "pmd32.h"

#include "formats/upd765_dsk.h"

#define LOG_PROTO (1U << 1)   // verbose: drive/motor latch + per-byte

#define VERBOSE (LOG_GENERAL)
#include "logmacro.h"

#define LOGPROTO(...) LOGMASKED(LOG_PROTO, __VA_ARGS__)


DEFINE_DEVICE_TYPE(PMD32, pmd32_device, "pmd32", "PMD-32 floppy disk unit")


pmd32_device::pmd32_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, PMD32, tag, owner, clock)
	, m_cpu(*this, "cpu")
	, m_ppi(*this, "ppi")
	, m_fdc(*this, "fdc")
	, m_dma(*this, "dma")
	, m_floppy(*this, "fdc:%u", 0U)
	, m_out_data_cb(*this)
	, m_out_ctrl_cb(*this)
	, m_host_byte(0xff)
	, m_drive(0)
	, m_out_count(0)
	, m_in_count(0)
	, m_fdc_log(0)
{
}


//-------------------------------------------------
//  ROM -- a verified silicon dump (see header / PROVENANCE.md)
//-------------------------------------------------

ROM_START(pmd32)
	ROM_REGION(0x0800, "rom", 0)
	// Verified EPROM dump of the PMD-32 control program. It corroborates the
	// earlier community reconstruction (assembled from Roman Bórik's RM-TEAM
	// disassembly, pmd85.borik.net) to the byte: 2046 of the 2047 programmed
	// bytes match exactly. The lone difference is at 0x069E -- the last byte of
	// a ROM data table -- which the reconstruction left as 0xFF (its source
	// stopped one byte short) but which the real device holds at 0x05. The
	// 0x069F-0x07FF tail is genuinely unprogrammed (0xFF).
	ROM_LOAD("pmd32.rom", 0x0000, 0x0800, CRC(5c28d71d) SHA1(28ef888a4de259a36095177e4d05425d05814d59))
ROM_END

const tiny_rom_entry *pmd32_device::device_rom_region() const
{
	return ROM_NAME(pmd32);
}


//-------------------------------------------------
//  address maps
//-------------------------------------------------

void pmd32_device::mem_map(address_map &map)
{
	map(0x0000, 0x07ff).rom().region("rom", 0);
	map(0x1800, 0x1bff).ram();
}

void pmd32_device::io_map(address_map &map)
{
	map.global_mask(0xff);
	map(0x00, 0x00).r(m_fdc, FUNC(i8272a_device::msr_r));                                  // FDC status
	map(0x01, 0x01).rw(FUNC(pmd32_device::fdc_fifo_r), FUNC(pmd32_device::fdc_fifo_w));    // FDC data (logged)
	map(0x20, 0x23).rw(m_ppi, FUNC(i8255_device::read), FUNC(i8255_device::write));        // host link
	map(0x40, 0x48).rw(m_dma, FUNC(i8257_device::read), FUNC(i8257_device::write));        // DMA
	map(0xe0, 0xe0).w(FUNC(pmd32_device::drive_w));                                         // drive/motor latch
}


//-------------------------------------------------
//  config
//-------------------------------------------------

namespace {

// 8" SSSD FM (IBM 3740-style): 77 tracks x 26 sectors x 128 bytes -- the Consul
// courseware geometry the unit's firmware reads.
class pmd32_disc_format : public upd765_format
{
public:
	pmd32_disc_format() : upd765_format(formats) { }
	const char *name() const noexcept override { return "pmd32"; }
	const char *description() const noexcept override { return "Consul 2717 / PMD-32 8\" disk image"; }
	const char *extensions() const noexcept override { return "img,dz8,p32"; }
private:
	static const format formats[];
};

const pmd32_disc_format::format pmd32_disc_format::formats[] = {
	{
		floppy_image::FF_8, floppy_image::SSSD, floppy_image::FM,
		2000, 26, 77, 1, 128, {}, 1, {}, 40, 26, 11, 27
	},
	{}
};

const pmd32_disc_format FLOPPY_PMD32_FORMAT;

} // anonymous namespace

void pmd32_device::floppy_formats(format_registration &fr)
{
	fr.add(FLOPPY_PMD32_FORMAT);
	fr.add_mfm_containers();
}

static void pmd32_floppies(device_slot_interface &device)
{
	device.option_add("8dsdd", FLOPPY_8_DSDD);
}

void pmd32_device::device_add_mconfig(machine_config &config)
{
	I8080(config, m_cpu, 2'048'000);   // MHB 8080A; clock approximate
	m_cpu->set_addrmap(AS_PROGRAM, &pmd32_device::mem_map);
	m_cpu->set_addrmap(AS_IO, &pmd32_device::io_map);

	I8255(config, m_ppi);
	m_ppi->in_pa_callback().set(FUNC(pmd32_device::host_byte_r));
	m_ppi->out_pa_callback().set(FUNC(pmd32_device::ppi_pa_w));
	m_ppi->out_pc_callback().set(FUNC(pmd32_device::ppi_pc_w));

	I8272A(config, m_fdc, 8'000'000);
	m_fdc->drq_wr_callback().set(m_dma, FUNC(i8257_device::dreq0_w));
	m_fdc->intrq_wr_callback().set_inputline(m_cpu, I8085_INTR_LINE);

	I8257(config, m_dma, 2'048'000);
	m_dma->out_hrq_cb().set(FUNC(pmd32_device::hrq_w));
	m_dma->in_memr_cb().set(FUNC(pmd32_device::dma_mem_r));
	m_dma->out_memw_cb().set(FUNC(pmd32_device::dma_mem_w));
	m_dma->in_ior_cb<0>().set(m_fdc, FUNC(i8272a_device::dma_r));
	m_dma->out_iow_cb<0>().set(m_fdc, FUNC(i8272a_device::dma_w));
	m_dma->out_tc_cb().set(m_fdc, FUNC(i8272a_device::tc_line_w));

	FLOPPY_CONNECTOR(config, "fdc:0", pmd32_floppies, "8dsdd", pmd32_device::floppy_formats);
	FLOPPY_CONNECTOR(config, "fdc:1", pmd32_floppies, "8dsdd", pmd32_device::floppy_formats);
}


//-------------------------------------------------
//  host parallel link (8255 port A) and DMA glue
//-------------------------------------------------

void pmd32_device::ppi_pa_w(uint8_t data)
{
	// the unit's firmware put a byte on the host link (0xAA presentation, then
	// ACK/ERR/sector data once the protocol is running)
	if (m_out_count < 300)
	{
		char const *tag = (data == 0xaa) ? "  (presentation)"
			: (data == 0x33) ? "  (ACK)"
			: (data == 0x99) ? "  (NAK)" : "";
		LOG("unit -> host: %02X%s\n", data, tag);
		m_out_count++;
	}
	m_out_data_cb(data);
}

void pmd32_device::ppi_pc_w(uint8_t data)
{
	m_out_ctrl_cb(data);
}

void pmd32_device::host_data_w(uint8_t data)
{
	// host put a byte (command letter, parameter or CRC) on the bus: just latch it
	// as the drive 8255's port-A input value.  It is strobed in only when the host's
	// /OBFa actually asserts (host_strobe), not on every port-A write callback --
	// the host 8255 also fires its port-A callback on mode-set, with no real byte.
	if (m_in_count < 300)
	{
		LOG("host -> unit: %02X%s\n", data, (data == 0x42) ? "  ('B' boot)" : "");
		m_in_count++;
	}
	m_host_byte = data;
}

void pmd32_device::host_strobe()
{
	// host /OBFa asserted: pulse the drive 8255's /STBa so it latches m_host_byte
	// into its port-A input buffer and raises IBFa
	m_ppi->pc4_w(0);
	m_ppi->pc4_w(1);
}

uint8_t pmd32_device::fdc_fifo_r()
{
	uint8_t const v = m_fdc->fifo_r();
	if (m_fdc_log < 200)
	{
		LOG("unit FDC -> %02X\n", v);
		m_fdc_log++;
	}
	return v;
}

void pmd32_device::fdc_fifo_w(uint8_t data)
{
	// the firmware's command bytes (e.g. 06=READ DATA, 0F=SEEK, 07=RECALIBRATE)
	if (m_fdc_log < 200)
	{
		LOG("unit FDC <- %02X\n", data);
		m_fdc_log++;
	}
	m_fdc->fifo_w(data);
}

void pmd32_device::drive_w(uint8_t data)
{
	// E0: DS1 DS0 MO1 MO0 ENA . . .  -- select drive + spin motor
	LOG("drive/motor latch = %02X (drive %u)\n", data, BIT(data, 6));
	m_drive = data;
	floppy_image_device *fd = m_floppy[BIT(data, 6)]->get_device();
	if (fd)
		fd->mon_w(BIT(data, 4) ? 0 : 1);   // motor on when MO bit set (active-low mon)
}

uint8_t pmd32_device::dma_mem_r(offs_t offset)
{
	return m_cpu->space(AS_PROGRAM).read_byte(offset);
}

void pmd32_device::dma_mem_w(offs_t offset, uint8_t data)
{
	m_cpu->space(AS_PROGRAM).write_byte(offset, data);
}

void pmd32_device::hrq_w(int state)
{
	m_cpu->set_input_line(INPUT_LINE_HALT, state);
	m_dma->hlda_w(state);
}


//-------------------------------------------------
//  device_t
//-------------------------------------------------

void pmd32_device::device_start()
{
	save_item(NAME(m_host_byte));
	save_item(NAME(m_drive));
	save_item(NAME(m_out_count));
	save_item(NAME(m_in_count));
	save_item(NAME(m_fdc_log));
}

void pmd32_device::device_reset()
{
	m_out_count = 0;
	m_in_count = 0;
	m_fdc_log = 0;
	LOG("PMD-32 unit reset; its 8080 will run the firmware\n");
}
