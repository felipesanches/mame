// license:GPL2+
// copyright-holders:Felipe Sanches
/***************************************************************************

    MN19413 Effect DSP (IC310)

    Serial-interfaced DSP used in the Technics SX-KN5000. Connected to
    Sub CPU via GPIO bit-bang on Port F (SDA=PF.0, SCLK=PF.2) with
    chip select on Port E (CS2=PE.6, active low).

    The SubCPU firmware refers to this as "DSP2" and routes commands via
    DSP_DispatchCommand(chip=1) / DSP_DispatchData(chip=1).
    Both DSP1 (DS3613GF-3BA) and DSP2 share the same command protocol.

    Protocol (from firmware bytecode interpreter Op0E handler):
      Transaction = command byte + N data bytes
      Each byte is a separate CS assert/deassert cycle (9 SCLK rising edges).
      The first byte after an idle period is the command.
      The bytecode Op0D (state change) calls DSP2_SPI_BusIdle between
      command groups, marking the end of one transaction.

    Boot-time register writes observed (CMD 0x30):
      addr=0xD0 value=0x0000
      addr=0xD3 value=0x0000
      addr=0x3C value=0x4000

    Debug strings in firmware:
      "DSP %d reset."          "DSP %d anti reset."
      "EFF %d mute."           "DSP %d mute."
      "EFF %d disconnect."     "EFF %d link."
      "argo change %d"         (algorithm/routing change)

***************************************************************************/

#ifndef MAME_SOUND_MN19413_H
#define MAME_SOUND_MN19413_H

#pragma once

#include <vector>

class mn19413_device : public device_t
{
public:
	mn19413_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// Parallel port interface (delivered by driver from PF bit-bang protocol)
	// All bytes arrive via parallel_data_w(); the device auto-detects the
	// first byte of each transaction as the command byte.
	void parallel_data_w(uint8_t data);

	// Transaction boundary signal (called when DSP2_SPI_BusIdle runs,
	// or when inter-transaction idle is detected)
	void transaction_end();

	// Serial interface (directly from SubCPU serial port — not currently used
	// since DSP2 uses GPIO bit-bang, but kept for completeness)
	void rxd(int state);
	void sclk(int state);

	// Callback to CPU serial port
	auto txd() { return m_txd_cb.bind(); }

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	void process_transaction();

	// Serial RX state (unused — DSP2 uses GPIO bit-bang, not serial port)
	uint8_t m_rx_shift;
	uint8_t m_rx_bit_count;
	uint8_t m_rxd_state;
	uint8_t m_sclk_state;

	// Transaction state
	uint8_t m_cmd;                    // Command byte (first byte of transaction)
	std::vector<uint8_t> m_data;      // Data bytes (subsequent bytes)
	bool m_awaiting_cmd;              // True = next byte is command

	// Register state (for tracking parameter writes)
	uint16_t m_regs[256];             // Register file (addr -> 16-bit value)

	// Idle detection timer
	emu_timer *m_idle_timer;
	TIMER_CALLBACK_MEMBER(idle_timeout);

	// Callback
	devcb_write_line m_txd_cb;
};

DECLARE_DEVICE_TYPE(MN19413, mn19413_device)

#endif // MAME_SOUND_MN19413_H
