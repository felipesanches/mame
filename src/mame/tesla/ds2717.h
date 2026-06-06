// license:BSD-3-Clause
// copyright-holders:Felipe Corrêa da Silva Sanches
/*******************************************************************************

    DS 2717 floppy disk controller (Consul 2717)

    The Consul 2717's built-in 8" disk controller -- a "dumb" board with no
    local CPU, reverse-engineered from the sapi.cz schematic 616_143 and from
    the verified PLAIN c2717 system ROM (CRC da1703b1).  The board is just an
    Intel 8272/SM609R FDC, an 8253 PIT, a 74LS174 control latch, a 74S138
    address decoder and a pair of 74LS193 binary counters that form an external
    head-position counter.  The Consul's own 8080 drives it directly through the
    system ROM over I/O ports 0xC8-0xCF.

    The C8-CF window is a STANDARD i8272 PIO interface plus a little board glue,
    NOT an abstract byte handshake.  Per-port map (all confirmed by ROM access):

        C8  IN   i8272 MAIN STATUS REGISTER (MSR): bit7=RQM, bit6=DIO.
        C9  IN   i8272 DATA register (FIFO) -- result and execution-phase data.
        C9  OUT  i8272 DATA register (FIFO) -- command and parameter bytes.
        CA  IN   board status; bit0 = transfer-END / TC flag.
        CA  OUT  control latch (74LS174): static drive-select/motor/config in
                 the upper bits; bit0 = operation-run / transfer-arm gate.
                 0x2B = idle/armed, 0x2A = running.
        CC  OUT  16-bit hardware-seek track address to the 74LS193 counters,
                 written low byte then high byte on two consecutive OUT CC.
        CF  OUT  address-load strobe; value 0x30 latches the CC track address
                 into the 74LS193 counters (the hardware seek trigger).
        CB / CD / CE   not accessed by the driver.

    The board has no DMA controller: the i8272 runs in non-DMA (PIO) mode and
    its per-byte data request is routed onto the 8080 INTR line.  With the
    default interrupt vector 0xFF that becomes RST 7 -> 0x0038, where the ROM
    installs a one-byte transfer handler (IN/OUT C9 once per interrupt).  The
    device forwards the i8272 INTRQ to the host via out_int_cb().

    The head is positioned ENTIRELY by the board's 74LS193 counters via CC/CF in
    the normal boot path -- the ROM issues no i8272 SEEK there.  The device
    therefore translates the CC/CF hardware-seek into a physical floppy head
    move itself, so the subsequent i8272 READ DATA finds the right cylinder.
    (i8272 SEEK/RECALIBRATE/READ-ID over C9 are still honoured for the ROM's
    error-recovery path.)

*******************************************************************************/

#ifndef MAME_TESLA_DS2717_H
#define MAME_TESLA_DS2717_H

#pragma once

#include "machine/upd765.h"
#include "imagedev/floppy.h"


class ds2717_device : public device_t
{
public:
	ds2717_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// INTRQ line to the host 8080 INTR (default vector 0xFF -> RST 7 -> 0x0038)
	auto out_int_cb() { return m_int_cb.bind(); }

	// host access: offset 0..7 maps to I/O ports 0xC8..0xCF
	uint8_t read(offs_t offset);
	void write(offs_t offset, uint8_t data);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;

private:
	void fdc_intrq_w(int state);
	void fdc_drq_w(int state);
	void do_hardware_seek();
	static void floppy_formats(format_registration &fr);

	required_device<i8272a_device> m_fdc;
	required_device_array<floppy_connector, 2> m_floppy;

	devcb_write_line m_int_cb;

	// board glue state
	uint8_t  m_control;      // CA control latch (74LS174): drive/motor/config + run gate (bit0)
	uint16_t m_seek_addr;    // CC 74LS193 counter image (the hardware-seek target track)
	uint8_t  m_seek_phase;   // 0 = expect low byte next on OUT CC, 1 = expect high byte
	int      m_intrq;        // last i8272 INTRQ level
	int      m_drq;          // last i8272 DRQ level (unused by the host in PIO; kept for trace)

	// capped bring-up logging
	uint32_t m_log_count;
};

DECLARE_DEVICE_TYPE(DS2717, ds2717_device)

#endif // MAME_TESLA_DS2717_H
