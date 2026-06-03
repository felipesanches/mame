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
{
}


//-------------------------------------------------
//  ROM -- a BAD_DUMP reconstruction (see header / PROVENANCE.md)
//-------------------------------------------------

ROM_START(pmd32)
	ROM_REGION(0x0800, "rom", 0)
	// RECONSTRUCTION, not a verified silicon dump: assembled from RM-TEAM's
	// commented disassembly of the PMD-32 control program (Roman Bórik, 2006;
	// pmd85.borik.net download id 16). Cross-checked two ways (round-trip
	// disassembly and an independent ASL assembly) but never verified against a
	// real EPROM, so it is flagged BAD_DUMP. The unprogrammed tail (0x069E-0x07FF)
	// is padded 0xFF by assumption. A real dump is still wanted.
	ROM_LOAD("pmd32-reconstructed.bin", 0x0000, 0x0800, BAD_DUMP CRC(2da51576) SHA1(e4b0bc86a27d3e64e2fca492a947a6fda3463504))
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
	map(0x01, 0x01).rw(m_fdc, FUNC(i8272a_device::fifo_r), FUNC(i8272a_device::fifo_w));   // FDC data
	map(0x20, 0x23).rw(m_ppi, FUNC(i8255_device::read), FUNC(i8255_device::write));        // host link
	map(0x40, 0x48).rw(m_dma, FUNC(i8257_device::read), FUNC(i8257_device::write));        // DMA
	map(0xe0, 0xe0).w(FUNC(pmd32_device::drive_w));                                         // drive/motor latch
}


//-------------------------------------------------
//  config
//-------------------------------------------------

void pmd32_device::floppy_formats(format_registration &fr)
{
	fr.add_mfm_containers();
}

static void pmd32_floppies(device_slot_interface &device)
{
	device.option_add("525dd", FLOPPY_525_DD);
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

	FLOPPY_CONNECTOR(config, "fdc:0", pmd32_floppies, "525dd", pmd32_device::floppy_formats);
	FLOPPY_CONNECTOR(config, "fdc:1", pmd32_floppies, "525dd", pmd32_device::floppy_formats);
}


//-------------------------------------------------
//  host parallel link (8255 port A) and DMA glue
//-------------------------------------------------

void pmd32_device::ppi_pa_w(uint8_t data)
{
	// the unit's firmware put a byte on the host link (e.g. the 0xAA presentation)
	if (m_out_count < 16)
	{
		LOG("unit -> host: %02X%s\n", data, (data == 0xaa) ? "  (presentation)" : "");
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
	// host wrote a byte: present it on the drive 8255's port-A input and strobe
	m_host_byte = data;
	m_ppi->pc4_w(0);
	m_ppi->pc4_w(1);
}

void pmd32_device::drive_w(uint8_t data)
{
	// E0: DS1 DS0 MO1 MO0 ENA . . .  -- select drive + spin motor
	LOGPROTO("drive/motor latch = %02X (drive %u)\n", data, BIT(data, 6));
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
}

void pmd32_device::device_reset()
{
	m_out_count = 0;
	LOG("PMD-32 unit reset; its 8080 will run the firmware\n");
}
