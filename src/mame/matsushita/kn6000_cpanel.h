// license:GPL-2.0+
// copyright-holders:Felipe Sanches

// KN6000 and KN6500 front panel: two boards, left (CPL) and right (CPR).

#ifndef MAME_MATSUSHITA_KN6000_CPANEL_H
#define MAME_MATSUSHITA_KN6000_CPANEL_H

#pragma once

#include "kn_cpanel.h"

class kn6000_cpanel_device : public kn_cpanel_base_device
{
public:
	kn6000_cpanel_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

protected:
	// device_t implementation
	virtual ioport_constructor device_input_ports() const override ATTR_COLD;

	// kn_cpanel_base_device implementation
	virtual int num_scan_ports() const override;
	virtual u8 scan_port_read(int port) override;
	virtual u8 port_seg(int port) const override;
	virtual int num_segs() const override;
	virtual u8 seg_wire_addr(int seg) const override;
	virtual void panel_led_frame(u8 addr, u8 data) override;

private:
	required_ioport_array<20> m_phys;
	output_finder<512> m_cpl_leds;
	output_finder<512> m_cpr_leds;
};

DECLARE_DEVICE_TYPE(KN6000_CPANEL, kn6000_cpanel_device)

#endif // MAME_MATSUSHITA_KN6000_CPANEL_H
