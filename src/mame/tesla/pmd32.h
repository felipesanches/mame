// license:BSD-3-Clause
// copyright-holders:Felipe Corrêa da Silva Sanches
/*******************************************************************************

    PMD-32 floppy disk unit (low-level emulation)

    The PMD-32 is the external "intelligent" floppy disk unit for the Tesla
    PMD-85 family and the Consul 2717.  It is a self-contained 8080A machine:

        MHB 8080A CPU
        2 KB ROM (control program) at 0x0000
        1 KB RAM at 0x1800 (stack at 0x1C00)
        FDC 8272A          at I/O 0x00-0x01
        PIO 8255           at I/O 0x20-0x23  (port A = parallel link to the host)
        DMA 8257           at I/O 0x40-0x48  (channel 0 feeds the FDC)
        drive/motor latch  at I/O 0xE0       (bits: DS1 DS0 MO1 MO0 ENA . . .)
        two 5.25" drives

    It communicates with the host (PMD-85 / Consul 2717) over the 8255 port-A
    bidirectional parallel channel, master-slave, with a presentation-byte
    handshake and char+CRC command protocol.

    WIP: the inter-chip (DMA/FDC) and host-link handshakes are a first cut and
    not yet verified on a host build.

    The firmware ROM is a verified silicon dump, corroborated to the byte by an
    independent community reconstruction. See src/mame/tesla/pmd32.cpp.

*******************************************************************************/

#ifndef MAME_TESLA_PMD32_H
#define MAME_TESLA_PMD32_H

#pragma once

#include "cpu/i8085/i8085.h"
#include "machine/i8255.h"
#include "machine/upd765.h"
#include "machine/i8257.h"
#include "imagedev/floppy.h"


class pmd32_device : public device_t
{
public:
	pmd32_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// parallel link to the host's 8255 port A (wire these up in the host machine config)
	auto out_data_cb() { return m_out_data_cb.bind(); }  // drive -> host : port-A output byte
	auto out_ctrl_cb() { return m_out_ctrl_cb.bind(); }  // drive -> host : port-C handshake byte

	void host_data_w(uint8_t data);                      // host -> drive : latch a byte on port-A input
	void host_strobe();                                  // host /OBFa asserted : strobe the byte in (/STBa)
	void host_ack_w(int state) { m_ppi->pc6_w(state); }  // host acknowledges the drive's output (/ACKa)

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;
	virtual const tiny_rom_entry *device_rom_region() const override ATTR_COLD;

private:
	void mem_map(address_map &map) ATTR_COLD;
	void io_map(address_map &map) ATTR_COLD;

	void drive_w(uint8_t data);                 // 0xE0 drive/motor select latch
	uint8_t fdc_fifo_r();                       // bring-up: logged FDC data-register read
	void fdc_fifo_w(uint8_t data);              // bring-up: logged FDC command/data write
	uint8_t host_byte_r() { return m_host_byte; }            // drive 8255 port-A input
	void ppi_pa_w(uint8_t data);                             // drive 8255 port-A output -> host
	void ppi_pc_w(uint8_t data);                             // drive 8255 port-C -> host
	uint8_t dma_mem_r(offs_t offset);
	void dma_mem_w(offs_t offset, uint8_t data);
	void hrq_w(int state);
	static void floppy_formats(format_registration &fr);

	required_device<i8080_cpu_device> m_cpu;
	required_device<i8255_device> m_ppi;
	required_device<i8272a_device> m_fdc;
	required_device<i8257_device> m_dma;
	required_device_array<floppy_connector, 2> m_floppy;

	devcb_write8 m_out_data_cb;
	devcb_write8 m_out_ctrl_cb;
	uint8_t m_host_byte;
	uint8_t m_drive;
	uint16_t m_out_count;  // bring-up: count of logged unit->host bytes (capped)
	uint16_t m_in_count;   // bring-up: count of logged host->unit bytes (capped)
	uint16_t m_fdc_log;    // bring-up: count of logged FDC fifo accesses (capped)
};

DECLARE_DEVICE_TYPE(PMD32, pmd32_device)

#endif // MAME_TESLA_PMD32_H
