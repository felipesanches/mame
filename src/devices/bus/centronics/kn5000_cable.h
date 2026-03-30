// license:BSD-3-Clause
// copyright-holders:Felipe Sanches
/***************************************************************************

    HD-AE5000 ↔ PC parallel port null cable

    This centronics peripheral device models the DB15-to-DB25 cable that
    connects the HDAE5000's parallel port to a PC's LPT port. It plugs
    into the HDAE5000's centronics slot and exposes callbacks for the
    PC side, which the combined wrapper driver connects to the PC's
    LPT device.

    Physical cable: DB15 (HDAE5000 rear) ←→ DB25 (PC parallel port)

    Signal flow:
      HDAE5000 PPI Port A (data)    ←→  PC Data (0x378)
      HDAE5000 PPI Port B (control) →   PC Status (0x379)
      HDAE5000 PPI Port C (status)  ←   PC Control (0x37A)

***************************************************************************/

#ifndef MAME_BUS_CENTRONICS_KN5000_CABLE_H
#define MAME_BUS_CENTRONICS_KN5000_CABLE_H

#pragma once

#include "ctronics.h"

class kn5000_parport_cable_device : public device_t,
	public device_centronics_peripheral_interface
{
public:
	kn5000_parport_cable_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	// PC-side output callbacks (HDAE5000 → PC direction)
	// The wrapper driver connects these to the PC's LPT input signals.
	auto pc_data_callback() { return m_pc_data_cb.bind(); }
	auto pc_busy_callback() { return m_pc_busy_cb.bind(); }
	auto pc_ack_callback() { return m_pc_ack_cb.bind(); }
	auto pc_select_callback() { return m_pc_select_cb.bind(); }
	auto pc_fault_callback() { return m_pc_fault_cb.bind(); }

	// PC-side input methods (PC → HDAE5000 direction)
	// The wrapper driver calls these when the PC's LPT writes data/control.
	void pc_data_w(uint8_t data);
	void pc_strobe_w(int state);
	void pc_autofd_w(int state);
	void pc_init_w(int state);
	void pc_select_in_w(int state);

protected:
	virtual void device_start() override ATTR_COLD;

	// centronics peripheral interface — signals from HDAE5000
	virtual void input_data0(int state) override { if (state) m_kn_data |= 0x01; else m_kn_data &= ~0x01; m_pc_data_cb(m_kn_data); }
	virtual void input_data1(int state) override { if (state) m_kn_data |= 0x02; else m_kn_data &= ~0x02; m_pc_data_cb(m_kn_data); }
	virtual void input_data2(int state) override { if (state) m_kn_data |= 0x04; else m_kn_data &= ~0x04; m_pc_data_cb(m_kn_data); }
	virtual void input_data3(int state) override { if (state) m_kn_data |= 0x08; else m_kn_data &= ~0x08; m_pc_data_cb(m_kn_data); }
	virtual void input_data4(int state) override { if (state) m_kn_data |= 0x10; else m_kn_data &= ~0x10; m_pc_data_cb(m_kn_data); }
	virtual void input_data5(int state) override { if (state) m_kn_data |= 0x20; else m_kn_data &= ~0x20; m_pc_data_cb(m_kn_data); }
	virtual void input_data6(int state) override { if (state) m_kn_data |= 0x40; else m_kn_data &= ~0x40; m_pc_data_cb(m_kn_data); }
	virtual void input_data7(int state) override { if (state) m_kn_data |= 0x80; else m_kn_data &= ~0x80; m_pc_data_cb(m_kn_data); }

	// HDAE5000 PPI Port B control signals → PC status signals
	virtual void input_strobe(int state) override { m_pc_busy_cb(state); }
	virtual void input_autofd(int state) override { m_pc_ack_cb(state); }
	virtual void input_init(int state) override { m_pc_select_cb(state); }
	virtual void input_select_in(int state) override { m_pc_fault_cb(state); }

private:
	// Callbacks to PC side
	devcb_write8 m_pc_data_cb;
	devcb_write_line m_pc_busy_cb;
	devcb_write_line m_pc_ack_cb;
	devcb_write_line m_pc_select_cb;
	devcb_write_line m_pc_fault_cb;

	uint8_t m_kn_data;   // accumulated data byte from HDAE5000
	uint8_t m_pc_data;   // data byte from PC
};

DECLARE_DEVICE_TYPE(KN5000_PARPORT_CABLE, kn5000_parport_cable_device)

#endif // MAME_BUS_CENTRONICS_KN5000_CABLE_H
