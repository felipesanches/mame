// license:BSD-3-Clause
// copyright-holders:Felipe Corrêa da Silva Sanches
/*******************************************************************************

    DS 2717 floppy disk controller (Consul 2717)

    The Consul 2717's built-in 8" disk controller -- a "dumb" board (no CPU)
    reverse-engineered from sapi.cz schematic 616_143: an Intel 8272 FDC, an
    8253 PIT, an 8224 clock generator and a forest of 74LS glue (latches, muxes
    and 74LS193 counters that form an external head-position counter).  It is
    driven directly by the Consul's own 8080 through the verified system ROM.

    Host I/O ports (decoded by the board's 74S138 / glue):
        0xF7  i8272 data register (commands and data)
        0xF6  i8272 status (MSR) on read; a control strobe on write
        0xF5  board status -- polled (FDC ready/DRQ, head-position counter)
        0xF4  drive-select / head-step control on write; status on read

    WIP: the exact bit assignments of the 0xF4/0xF5/0xF6 glue are still being
    worked out from host traces; this first cut wires the real i8272 and
    instruments the glue.

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

	// host access: offset 0..3 maps to I/O ports 0xF4..0xF7
	uint8_t read(offs_t offset);
	void write(offs_t offset, uint8_t data);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;

private:
	void fdc_intrq_w(int state) { m_intrq = state; }
	void fdc_drq_w(int state)   { m_drq = state; }
	static void floppy_formats(format_registration &fr);

	required_device<i8272a_device> m_fdc;
	required_device_array<floppy_connector, 2> m_floppy;

	uint8_t m_control;   // 0xF4 latch (drive/motor/step)
	uint8_t m_f6;        // 0xF6 write latch (control strobe)
	uint8_t m_f5_track;  // synthesized head-position counter read back on 0xF5
	int m_intrq;
	int m_drq;
};

DECLARE_DEVICE_TYPE(DS2717, ds2717_device)

#endif // MAME_TESLA_DS2717_H
