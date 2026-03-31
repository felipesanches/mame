// license:BSD-3-Clause
// copyright-holders:Felipe Sanches
/***************************************************************************

    Technics KN5000 + HD-AE5000 as a centronics peripheral

    Wraps a complete KN5000 keyboard (with HDAE5000 extension pre-selected)
    as a centronics peripheral device.  When plugged into a PC's LPT slot,
    the PC's parallel port signals are cross-wired to the HDAE5000's PPI:

    PC -> KN5000 direction:
      PC Data (0x378)           ->  HDAE5000 PPI Port A input ("parport_data_in")
      PC STROBE/AUTOFD/INIT/SEL ->  HDAE5000 PPI Port C input ("parport_status")

    KN5000 -> PC direction:
      HDAE5000 PPI Port A output ->  PC Data input (via centronics output_data)
      HDAE5000 PPI Port B output ->  PC Status (via centronics busy/ack/select/fault)

    The HDAE5000's own "parport" centronics slot remains empty — this device
    bypasses it by driving the PPI input buffers directly and intercepting
    PPI output signals via the HDAE5000's centronics output callbacks.

    Usage on a stock MAME PC driver:

      fs_mame ct486 -board4:lpt:lpt:centronics kn5000_cable \
          -board4:lpt:lpt:centronics:kn5000_cable:kn5000:extension hdae5000

***************************************************************************/

#include "emu.h"
#include "bus/centronics/kn5000_cable.h"
#include "bus/centronics/ctronics.h"
#include "bus/technics/kn5000/hdae5000.h"
#include "cpu/tlcs900/tmp94c241.h"
#include "cpu/tlcs900/tmp94c241_serial.h"
#include "imagedev/floppy.h"
#include "machine/gen_latch.h"
#include "machine/input_merger.h"
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

	// Cached pointers to HDAE5000's PPI input buffers (resolved at device_start)
	input_buffer_device *m_hdae_data_in;   // HDAE5000's "parport_data_in"
	input_buffer_device *m_hdae_status_in; // HDAE5000's "parport_status"

	void update_kn_data();
	void update_kn_status();

	// KN5000 -> PC direction: callbacks from HDAE5000's centronics output
	void kn_data_w(uint8_t data);
	void kn_busy_w(int state);
	void kn_ack_w(int state);
	void kn_select_w(int state);
	void kn_fault_w(int state);
};


kn5000_parport_cable_device::kn5000_parport_cable_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, KN5000_PARPORT_CABLE, tag, owner, clock),
	device_centronics_peripheral_interface(mconfig, *this),
	m_kn5000(*this, "kn5000"),
	m_pc_data(0),
	m_pc_control(0),
	m_hdae_data_in(nullptr),
	m_hdae_status_in(nullptr)
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

	// Resolve pointers to HDAE5000's PPI input buffers.
	// These are deep in the sub-device tree:
	//   kn5000 -> extension -> hdae5000 -> parport_data_in / parport_status
	// They will be null if the HDAE5000 extension is not selected.
	m_hdae_data_in = subdevice<input_buffer_device>("kn5000:extension:hdae5000:parport_data_in");
	m_hdae_status_in = subdevice<input_buffer_device>("kn5000:extension:hdae5000:parport_status");

	if (!m_hdae_data_in || !m_hdae_status_in)
	{
		logerror("kn5000_cable: HDAE5000 extension not found — parallel port communication disabled\n");
		return;
	}

	// KN5000 -> PC direction: install write taps on the PPI I/O addresses
	// in the main CPU's address space to intercept HDAE5000 firmware writes.
	//
	// The PPI is mapped at 0x160000-0x160007 with umask16(0x00ff) (low byte only).
	// Register offsets (byte addresses): Port A=0x160000, Port B=0x160002,
	// Port C=0x160004, Control=0x160006.
	//
	// When the firmware writes to Port A (data to PC) or Port B (status to PC),
	// the tap forwards the value to the PC's LPT via centronics output methods.
	address_space &space = m_kn5000->maincpu()->space(AS_PROGRAM);

	// Tap Port A writes (0x160000): HDAE5000 data -> PC data input
	space.install_write_tap(0x160000, 0x160001, "ppi_pa_tap",
		[this](offs_t offset, u16 &data, u16 mem_mask) {
			if (ACCESSING_BITS_0_7)
				kn_data_w(data & 0xff);
		});

	// Tap Port B writes (0x160002): HDAE5000 control -> PC status
	// The HDAE5000's ppi_pb_w maps: bit0=strobe, bit1=autofeed, bit2=init, bit3=select_in
	// These become PC status signals: busy, ack, select, fault
	space.install_write_tap(0x160002, 0x160003, "ppi_pb_tap",
		[this](offs_t offset, u16 &data, u16 mem_mask) {
			if (ACCESSING_BITS_0_7)
			{
				uint8_t pb = data & 0xff;
				kn_busy_w(BIT(pb, 0));
				kn_ack_w(BIT(pb, 1));
				kn_select_w(BIT(pb, 2));
				kn_fault_w(BIT(pb, 3));
			}
		});
}


// --- PC -> HDAE5000 direction ---

void kn5000_parport_cable_device::update_kn_data()
{
	// PC data byte -> HDAE5000 PPI Port A input buffer
	if (m_hdae_data_in)
	{
		m_hdae_data_in->write_bit0(BIT(m_pc_data, 0));
		m_hdae_data_in->write_bit1(BIT(m_pc_data, 1));
		m_hdae_data_in->write_bit2(BIT(m_pc_data, 2));
		m_hdae_data_in->write_bit3(BIT(m_pc_data, 3));
		m_hdae_data_in->write_bit4(BIT(m_pc_data, 4));
		m_hdae_data_in->write_bit5(BIT(m_pc_data, 5));
		m_hdae_data_in->write_bit6(BIT(m_pc_data, 6));
		m_hdae_data_in->write_bit7(BIT(m_pc_data, 7));
	}
}


void kn5000_parport_cable_device::update_kn_status()
{
	// PC control signals -> HDAE5000 PPI Port C input buffer
	// The HDAE5000 firmware reads Port C to check handshake signals from the PC.
	// Bit mapping: strobe=0, autofeed=1, init=2, select_in=3
	if (m_hdae_status_in)
	{
		m_hdae_status_in->write_bit0(BIT(m_pc_control, 0));
		m_hdae_status_in->write_bit1(BIT(m_pc_control, 1));
		m_hdae_status_in->write_bit2(BIT(m_pc_control, 2));
		m_hdae_status_in->write_bit3(BIT(m_pc_control, 3));
	}
}


// --- HDAE5000 -> PC direction ---
// These are called by the HDAE5000's centronics output signals and
// forwarded to the PC's LPT status register via the centronics
// peripheral interface's output methods.

void kn5000_parport_cable_device::kn_data_w(uint8_t data)
{
	// HDAE5000 PPI Port A output -> PC data input
	output_data0(BIT(data, 0));
	output_data1(BIT(data, 1));
	output_data2(BIT(data, 2));
	output_data3(BIT(data, 3));
	output_data4(BIT(data, 4));
	output_data5(BIT(data, 5));
	output_data6(BIT(data, 6));
	output_data7(BIT(data, 7));
}

void kn5000_parport_cable_device::kn_busy_w(int state)
{
	output_busy(state);
}

void kn5000_parport_cable_device::kn_ack_w(int state)
{
	output_ack(state);
}

void kn5000_parport_cable_device::kn_select_w(int state)
{
	output_select(state);
}

void kn5000_parport_cable_device::kn_fault_w(int state)
{
	output_fault(state);
}

} // anonymous namespace

DEFINE_DEVICE_TYPE_PRIVATE(KN5000_PARPORT_CABLE, device_centronics_peripheral_interface, kn5000_parport_cable_device, "kn5000_hdae", "Technics KN5000 + HD-AE5000 (parallel port)")
