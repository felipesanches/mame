// license:GPL2+
// copyright-holders:Felipe Sanches
/***************************************************************************

    MN19413 Effect DSP (IC310)

    Serial-interfaced DSP used in the Technics SX-KN5000. Connected to
    Sub CPU via serial port 0 (TX0/RX0/SCLK0).

    The SubCPU firmware refers to this as "DSP2" and uses debug strings:
      "DSP %d reset."          "DSP %d anti reset."
      "EFF %d mute."           "DSP %d mute."
      "EFF %d disconnect."     "EFF %d link."
      "argo change %d"         (algorithm/routing change)

    Known command bytes (from firmware analysis):
      0x01 — init/reset
      0x03 — algorithm select
      0x30 — parameter write
      0x60 — bulk data transfer

***************************************************************************/

#ifndef MAME_SOUND_MN19413_H
#define MAME_SOUND_MN19413_H

#pragma once

#include <vector>

class mn19413_device : public device_t
{
public:
	mn19413_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// Serial interface (from SubCPU serial port 0)
	void rxd(int state);              // Serial data input (from CPU TX0)
	void sclk(int state);             // Serial clock input (from CPU SCLK0)

	// Parallel port interface (delivered by driver from PF bit-bang protocol)
	void parallel_command_w(uint8_t data);
	void parallel_data_w(uint8_t data);

	// Callback to CPU serial port
	auto txd() { return m_txd_cb.bind(); }

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	void process_command();

	// Serial RX state
	uint8_t m_rx_shift;               // Shift register for incoming bits
	uint8_t m_rx_bit_count;           // Bits received in current byte
	uint8_t m_rxd_state;              // Current RXD line state
	uint8_t m_sclk_state;             // Current SCLK line state

	// Protocol state
	uint8_t m_byte_count;             // Bytes received in current transaction
	uint8_t m_current_command;        // Last command byte received

	// Parallel port protocol state (from driver-level PF bit-bang decoding)
	uint8_t m_par_cmd;                // Current command byte
	std::vector<uint8_t> m_par_data;  // Data bytes for current command

	// Callback
	devcb_write_line m_txd_cb;
};

DECLARE_DEVICE_TYPE(MN19413, mn19413_device)

#endif // MAME_SOUND_MN19413_H
