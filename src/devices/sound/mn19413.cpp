// license:GPL2+
// copyright-holders:Felipe Sanches
/***************************************************************************

    MN19413 Effect DSP (IC310)

    Serial-interfaced effect DSP used in the Technics SX-KN5000.
    This is an HLE stub that logs all received commands and tracks
    register state for protocol reverse engineering.

    Connected via SubCPU GPIO bit-bang on Port F (SDA=PF.0, SCLK=PF.2)
    with chip select on Port E (CS2=PE.6, active low).

    All bytes arrive as 9-bit frames (8 data + 1 trailing SCLK pulse).
    The byte stream is self-framing: 0x30 marks register write commands
    (followed by 4 data bytes each), while other byte patterns represent
    effect algorithm programming and coefficient loading.

    Both DSP1 (DS3613GF-3BA) and DSP2 (MN19413) share the same command
    protocol, routed by DSP_DispatchCommand(chip=0 or 1) in the SubCPU
    firmware bytecode interpreter.

***************************************************************************/

#include "emu.h"
#include "mn19413.h"

#define LOG_REGWRITE (1U << 1)   // Register writes (CMD 0x30)
#define LOG_STREAM   (1U << 2)   // Non-register-write bytes
#define LOG_SERIAL   (1U << 3)   // Serial byte reception (unused path)

#define VERBOSE (LOG_REGWRITE | LOG_STREAM)
#include "logmacro.h"

DEFINE_DEVICE_TYPE(MN19413, mn19413_device, "mn19413", "MN19413 Effect DSP")

mn19413_device::mn19413_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, MN19413, tag, owner, clock)
	, m_rx_shift(0)
	, m_rx_bit_count(0)
	, m_rxd_state(1)
	, m_sclk_state(0)
	, m_cmd(0)
	, m_awaiting_cmd(true)
	, m_idle_timer(nullptr)
	, m_txd_cb(*this)
{
}

void mn19413_device::device_start()
{
	m_idle_timer = timer_alloc(FUNC(mn19413_device::idle_timeout), this);

	save_item(NAME(m_rx_shift));
	save_item(NAME(m_rx_bit_count));
	save_item(NAME(m_rxd_state));
	save_item(NAME(m_sclk_state));
	save_item(NAME(m_cmd));
	save_item(NAME(m_awaiting_cmd));
	save_item(NAME(m_regs));
}

void mn19413_device::device_reset()
{
	m_rx_shift = 0;
	m_rx_bit_count = 0;
	m_rxd_state = 1;
	m_sclk_state = 0;
	m_cmd = 0;
	m_data.clear();
	m_awaiting_cmd = true;
	std::fill(std::begin(m_regs), std::end(m_regs), 0);
}

//--------------------------------------------------------------------------
//  Parallel port interface (from driver GPIO bit-bang decoder)
//
//  Bytes arrive one at a time. The firmware's bytecode interpreter sends:
//    Op0E: 1 command + N data bytes (tight sequence, no idle gap)
//    Op0D: calls DSP2_SPI_BusIdle (creates idle gap = transaction boundary)
//
//  We use a 2ms idle timer to detect transaction boundaries. Within a
//  transaction, the first byte is the command and subsequent are data.
//  Register writes (CMD 0x30) are decoded inline as 4-byte groups.
//--------------------------------------------------------------------------

void mn19413_device::parallel_data_w(uint8_t data)
{
	if (m_awaiting_cmd)
	{
		// First byte after idle = command
		m_cmd = data;
		m_data.clear();
		m_awaiting_cmd = false;
	}
	else
	{
		m_data.push_back(data);

		// Decode register write: CMD 0x30, data in groups of 4 bytes
		// [0x00, addr, val_hi, val_lo]
		if (m_cmd == 0x30 && (m_data.size() % 4) == 0)
		{
			size_t base = m_data.size() - 4;
			uint8_t addr = m_data[base + 1];
			uint16_t value = (uint16_t(m_data[base + 2]) << 8) | m_data[base + 3];
			m_regs[addr] = value;
			LOGMASKED(LOG_REGWRITE, "REG[0x%02X] = 0x%04X\n", addr, value);
		}
	}

	// Reset idle timer — firmware inter-byte gap within a transaction is <100us,
	// while Op0D (yield to scheduler) creates gaps of several ms
	m_idle_timer->adjust(attotime::from_msec(2));
}

void mn19413_device::transaction_end()
{
	if (!m_awaiting_cmd)
	{
		process_transaction();
		m_awaiting_cmd = true;
	}
	m_idle_timer->adjust(attotime::never);
}

TIMER_CALLBACK_MEMBER(mn19413_device::idle_timeout)
{
	transaction_end();
}

void mn19413_device::process_transaction()
{
	if (m_cmd == 0x30 && !m_data.empty())
	{
		size_t reg_writes = m_data.size() / 4;
		LOGMASKED(LOG_REGWRITE, "CMD 0x30: %zu reg write%s\n",
			reg_writes, reg_writes != 1 ? "s" : "");
	}
	else if (!m_data.empty())
	{
		LOGMASKED(LOG_STREAM, "CMD 0x%02X: %zu data bytes [",
			m_cmd, m_data.size());
		for (size_t i = 0; i < m_data.size() && i < 16; i++)
			LOGMASKED(LOG_STREAM, "%s0x%02X", i ? " " : "", m_data[i]);
		if (m_data.size() > 16)
			LOGMASKED(LOG_STREAM, " ...");
		LOGMASKED(LOG_STREAM, "]\n");
	}
	else
	{
		LOGMASKED(LOG_STREAM, "CMD 0x%02X (no data)\n", m_cmd);
	}

	m_data.clear();
}

//--------------------------------------------------------------------------
//  Serial interface (directly from SubCPU serial port)
//  NOTE: DSP2 uses GPIO bit-bang, not the serial port. These are kept
//  for completeness but are not used in normal operation.
//--------------------------------------------------------------------------

void mn19413_device::rxd(int state)
{
	m_rxd_state = state ? 1 : 0;
}

void mn19413_device::sclk(int state)
{
	uint8_t new_sclk = state ? 1 : 0;

	if (!m_sclk_state && new_sclk)
	{
		m_rx_shift = (m_rx_shift << 1) | m_rxd_state;
		m_rx_bit_count++;

		if (m_rx_bit_count >= 8)
		{
			LOGMASKED(LOG_SERIAL, "Serial RX byte 0x%02X\n", m_rx_shift);
			m_rx_bit_count = 0;
			m_rx_shift = 0;
		}
	}

	m_sclk_state = new_sclk;
}
