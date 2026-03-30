// license:BSD-3-Clause
// copyright-holders:Felipe Sanches
/***************************************************************************

    Technics KN5000 + HD-AE5000 as a centronics peripheral

    Wraps a complete KN5000 keyboard (with HDAE5000 extension pre-selected)
    as a centronics peripheral device.  When plugged into a PC's LPT slot,
    the PC's parallel port signals are cross-wired to the HDAE5000's PPI:

    PC -> KN5000 direction:
      PC Data (0x378)           ->  HDAE5000 PPI Port A input
      PC STROBE/AUTOFD/INIT/SEL ->  HDAE5000 PPI Port C input (handshake)

    KN5000 -> PC direction:
      HDAE5000 PPI Port A output ->  PC Data input
      HDAE5000 PPI Port B output ->  PC BUSY/ACK/SELECT/FAULT

    This enables running the original unmodified HD-TechManager5000 Windows
    software on a stock MAME PC driver (e.g., at486) with the KN5000 as
    a peripheral:

      fs_mame at486 -board4:lpt:centronics kn5000_hdae

***************************************************************************/

#include "emu.h"
#include "bus/centronics/kn5000_cable.h"
#include "bus/technics/kn5000/hdae5000.h"
#include "cpu/tlcs900/tmp94c241.h"
#include "cpu/tlcs900/tmp94c241_serial.h"
#include "imagedev/floppy.h"
#include "machine/gen_latch.h"
#include "machine/upd765.h"
#include "kn5000.h"
#include "kn5000_cpanel.h"


namespace {

class kn5000_parport_cable_device : public device_t,
	public device_centronics_peripheral_interface
{
public:
	kn5000_parport_cable_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;

	// centronics peripheral interface — signals from PC's LPT
	virtual void input_data0(int state) override { if (state) m_pc_data |= 0x01; else m_pc_data &= ~0x01; update_kn_data(); }
	virtual void input_data1(int state) override { if (state) m_pc_data |= 0x02; else m_pc_data &= ~0x02; update_kn_data(); }
	virtual void input_data2(int state) override { if (state) m_pc_data |= 0x04; else m_pc_data &= ~0x04; update_kn_data(); }
	virtual void input_data3(int state) override { if (state) m_pc_data |= 0x08; else m_pc_data &= ~0x08; update_kn_data(); }
	virtual void input_data4(int state) override { if (state) m_pc_data |= 0x10; else m_pc_data &= ~0x10; update_kn_data(); }
	virtual void input_data5(int state) override { if (state) m_pc_data |= 0x20; else m_pc_data &= ~0x20; update_kn_data(); }
	virtual void input_data6(int state) override { if (state) m_pc_data |= 0x40; else m_pc_data &= ~0x40; update_kn_data(); }
	virtual void input_data7(int state) override { if (state) m_pc_data |= 0x80; else m_pc_data &= ~0x80; update_kn_data(); }

	// PC control signals -> HDAE5000 PPI Port C input (handshake from PC)
	virtual void input_strobe(int state) override { m_pc_control = (m_pc_control & ~0x01) | (state ? 0x01 : 0); update_kn_status(); }
	virtual void input_autofd(int state) override { m_pc_control = (m_pc_control & ~0x02) | (state ? 0x02 : 0); update_kn_status(); }
	virtual void input_init(int state) override   { m_pc_control = (m_pc_control & ~0x04) | (state ? 0x04 : 0); update_kn_status(); }
	virtual void input_select_in(int state) override { m_pc_control = (m_pc_control & ~0x08) | (state ? 0x08 : 0); update_kn_status(); }

private:
	required_device<kn5000_state> m_kn5000;

	uint8_t m_pc_data;     // accumulated data byte from PC
	uint8_t m_pc_control;  // accumulated control bits from PC

	void update_kn_data();
	void update_kn_status();
};


kn5000_parport_cable_device::kn5000_parport_cable_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, KN5000_PARPORT_CABLE, tag, owner, clock),
	device_centronics_peripheral_interface(mconfig, *this),
	m_kn5000(*this, "kn5000"),
	m_pc_data(0),
	m_pc_control(0)
{
}


void kn5000_parport_cable_device::device_add_mconfig(machine_config &config)
{
	// The entire KN5000 keyboard lives inside this centronics peripheral
	KN5000(config, m_kn5000, 0);
}


void kn5000_parport_cable_device::device_start()
{
	save_item(NAME(m_pc_data));
	save_item(NAME(m_pc_control));
}


void kn5000_parport_cable_device::update_kn_data()
{
	// PC data byte -> HDAE5000 PPI Port A input
	// TODO: Write m_pc_data into the HDAE5000's PPI Port A input buffer.
	logerror("PC->KN5000 data: 0x%02X\n", m_pc_data);
}


void kn5000_parport_cable_device::update_kn_status()
{
	// PC control signals -> HDAE5000 PPI Port C input
	// TODO: Write m_pc_control into the HDAE5000's PPI Port C input buffer.
	logerror("PC->KN5000 control: 0x%02X\n", m_pc_control);
}

} // anonymous namespace

DEFINE_DEVICE_TYPE_PRIVATE(KN5000_PARPORT_CABLE, device_centronics_peripheral_interface, kn5000_parport_cable_device, "kn5000_hdae", "Technics KN5000 + HD-AE5000 (parallel port)")
