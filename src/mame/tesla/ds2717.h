// license:BSD-3-Clause
// copyright-holders:Felipe Corrêa da Silva Sanches
/*******************************************************************************

    DS 2717 floppy disk controller (Consul 2717)

    The Consul 2717's built-in 8" disk controller -- a "dumb" board with no
    local CPU, reverse-engineered from the sapi.cz schematic 616_143 and from
    the verified PLAIN c2717 system ROM (CRC da1703b1).  The board is just an
    Intel 8272/SM609R FDC, an 8253 PIT (IC10), a 74LS174 control latch, a 74S138
    address decoder and a pair of 74LS193 binary counters (IC11/IC12) wired into
    the FM/MFM data-window / write-clock separator.  The Consul's own 8080 drives
    it directly through the system ROM over I/O ports 0xC8-0xCF.

    The C8-CF window is a STANDARD i8272 interface plus a little board glue, NOT
    an abstract byte handshake.  Per-port map (all confirmed by ROM access):

        C8  IN   i8272 MAIN STATUS REGISTER (MSR): bit7=RQM, bit6=DIO.
        C9  IN   i8272 DATA register (FIFO) -- result and execution-phase data.
        C9  OUT  i8272 DATA register (FIFO) -- command and parameter bytes.
        CA  IN   board status; bit0 = transfer-END flag (see below).
        CA  OUT  control latch (74LS174): static drive-select/motor/config in
                 the upper bits; bit0 = operation-run / transfer-arm gate.
                 0x2B = idle/armed, 0x2A = running.
        CC  OUT  8253 counter-0 preload, written LSB then MSB on two consecutive
                 OUT CC (the ROM loads 0x007F = 127 = sector_size - 1).
        CD  OUT  8253 counter-1 preload (second-stage loader only) -> pit8253.
        CE  IN   8253 counter-2 latched read-back (second-stage loader only).
        CE  OUT  8253 counter-2 preload (second-stage loader only) -> pit8253.
        CF  OUT  8253 control-word register.  Counter-0 words (SC=00, e.g. 0x30:
                 LSB-then-MSB, Mode 0, binary) (re)arm the per-sector byte
                 counter.  Counter-1/2 words (SC=01/10, e.g. 0x74/0xB4 Mode-2
                 rate generators and the 0x80 counter-2 latch command) route to
                 the pit8253 modelling counters 1 and 2.
        CB             not accessed by the driver.

    The board runs the i8272 in DMA mode: the SPECIFY command writes byte2=0x24
    (ND bit = 0), so the i8272 raises DRQ once per execution-phase byte.  With no
    DMA controller on the board, DRQ is routed onto the 8080 INTR line; with the
    default interrupt vector 0xFF that becomes RST 7 -> 0x0038, where the ROM
    installs a one-byte transfer handler (IN C9 via dma_r once per interrupt).
    The device forwards DRQ to the host via out_int_cb().

    8253 counters 1 and 2.  Counter 0 is the per-sector byte counter described
    above (hand-rolled; OUT0 -> i8272 TC + CA bit0).  The system ROM never
    touches counters 1 or 2 -- but the on-disc second-stage loader does: after a
    full-track READ it programs counter 1 (CF=0x74, Mode 2, preload 0x2710) and
    counter 2 (CF=0xB4, Mode 2, preload 0x0000 = 65536) as rate generators, then
    spins in a poll/timeout loop that repeatedly latches counter 2 (CF=0x80) and
    reads it back (two IN CE), computing elapsed = (saved_start - live_count) to
    measure wall time.  Per the schematic both CLK1/CLK2 are buffered taps of the
    8224-derived divided system clock (the FM data-window timebase, ~250-500 kHz),
    and OUT1/OUT2 feed the IC5 74LS153 data-separator window mux -- NOT motor or
    step lines (those are on the 74LS174 CA latch).  For the loader to advance,
    counter 2 only needs to be a running Mode-2 generator whose latched 16-bit
    count decreases over time; with it inert the elapsed subtraction stays 0 and
    the loader spins forever.  A real pit8253 (clk1/clk2 = the divided clock)
    models counters 1 and 2; CD/CE and the counter-1/2 control words route to it,
    while counter 0 stays hand-rolled so its OUT0 still pulses the i8272 TC.

    The head is positioned ENTIRELY by the i8272's own RECALIBRATE/SEEK commands
    over C9 (verified in the boot trace: RECALIBRATE/SEEK-to-cyl-5/RECALIBRATE
    head-settling, head left on track 0).  CC/CF do NO head movement.  Instead
    the 8253 counter-0, preloaded to 127, counts the 128 data bytes of one FM
    sector; its terminal count (after the 128th byte) both pulses the i8272 TC
    input -- there is no DMA controller to assert it otherwise -- ending the
    single-sector execution phase, and raises the CA bit0 transfer-END flag the
    RST 7 read ISR polls.  The same CA-poll loop is reused by the ROM's seek/
    recalibrate waits (which move no data), so CA bit0 must ALSO reflect the
    i8272 end-of-command INTRQ; hence bit0 = (byte-counter done) OR (INTRQ).

    NOTE: the byte-counter -> TC / CA bit0 wiring is inferred from the schematic
    and ROM, not yet confirmed against a live host read.

*******************************************************************************/

#ifndef MAME_TESLA_DS2717_H
#define MAME_TESLA_DS2717_H

#pragma once

#include "machine/upd765.h"
#include "machine/pit8253.h"
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
	static void floppy_formats(format_registration &fr);

	required_device<i8272a_device> m_fdc;
	required_device_array<floppy_connector, 2> m_floppy;
	required_device<pit8253_device> m_pit;   // 8253 counters 1 and 2 (counter 0 is hand-rolled)

	devcb_write_line m_int_cb;

	// board glue state
	uint8_t  m_control;      // CA control latch (74LS174): drive/motor/config + run gate (bit0)
	uint16_t m_byte_count;   // 8253 counter-0 preload (CC); loaded 0x007F=127, borrows after the 128th dma_r
	uint8_t  m_count_phase;  // 0 = expect LSB next on OUT CC, 1 = expect MSB
	bool     m_count_active; // CF=0x30 armed the counter (it gates the per-byte decrement)
	bool     m_count_done;   // counter terminal-count latch (set on borrow, cleared by CF=0x30 arm)
	int      m_intrq;        // last i8272 INTRQ level (end-of-command; NOT routed to the host)
	int      m_drq;          // last i8272 DRQ level; drives the host INT and gates the byte-counter decrement
	uint8_t  m_ca_last;      // last CA bit0 value reported (edge-only logging of the poll)

	// capped bring-up logging
	uint32_t m_log_count;
};

DECLARE_DEVICE_TYPE(DS2717, ds2717_device)

#endif // MAME_TESLA_DS2717_H
