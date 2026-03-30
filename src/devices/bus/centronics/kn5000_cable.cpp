// license:BSD-3-Clause
// copyright-holders:Felipe Sanches
/***************************************************************************

    HD-AE5000 ↔ PC parallel port null cable

***************************************************************************/

#include "emu.h"
#include "kn5000_cable.h"

DEFINE_DEVICE_TYPE(KN5000_PARPORT_CABLE, kn5000_parport_cable_device, "kn5000_parport_cable", "HD-AE5000 Parallel Port Cable")

kn5000_parport_cable_device::kn5000_parport_cable_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, KN5000_PARPORT_CABLE, tag, owner, clock),
	device_centronics_peripheral_interface(mconfig, *this),
	m_pc_data_cb(*this),
	m_pc_busy_cb(*this),
	m_pc_ack_cb(*this),
	m_pc_select_cb(*this),
	m_pc_fault_cb(*this),
	m_kn_data(0),
	m_pc_data(0xff)
{
}

void kn5000_parport_cable_device::device_start()
{
	save_item(NAME(m_kn_data));
	save_item(NAME(m_pc_data));
}

// --- PC → HDAE5000 direction ---
// These are called by the wrapper driver when the PC's LPT writes.
// They drive the centronics output lines back to the HDAE5000's PPI.

void kn5000_parport_cable_device::pc_data_w(uint8_t data)
{
	m_pc_data = data;
	// Drive individual data lines back to the HDAE5000 centronics port
	// (which feeds into the PPI Port A input via input_buffer)
	output_data0(BIT(data, 0));
	output_data1(BIT(data, 1));
	output_data2(BIT(data, 2));
	output_data3(BIT(data, 3));
	output_data4(BIT(data, 4));
	output_data5(BIT(data, 5));
	output_data6(BIT(data, 6));
	output_data7(BIT(data, 7));
}

void kn5000_parport_cable_device::pc_strobe_w(int state)
{
	// PC STROBE → HDAE5000 sees as BUSY signal on centronics status
	output_busy(state);
}

void kn5000_parport_cable_device::pc_autofd_w(int state)
{
	// PC AUTOFEED → HDAE5000 sees as ACK signal on centronics status
	output_ack(state);
}

void kn5000_parport_cable_device::pc_init_w(int state)
{
	// PC INIT → HDAE5000 sees as SELECT signal on centronics status
	output_select(state);
}

void kn5000_parport_cable_device::pc_select_in_w(int state)
{
	// PC SELECT_IN → HDAE5000 sees as FAULT signal on centronics status
	output_fault(state);
}
