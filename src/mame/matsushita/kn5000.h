// license:GPL2+
// copyright-holders:Felipe Sanches
/******************************************************************************

    Technics SX-KN5000 music keyboard — device declaration

    This header exposes kn5000_state as a device_t so it can be instantiated
    as a sub-device in composite drivers (e.g., KN5000 + PC for TechManager5000).
    For the standalone driver, kn5000.cpp provides a thin driver_device wrapper.

******************************************************************************/

#ifndef MAME_MATSUSHITA_KN5000_H
#define MAME_MATSUSHITA_KN5000_H

#pragma once

#include "cpu/tlcs900/tmp94c241.h"

class kn5000_state : public device_t
{
public:
	kn5000_state(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	// Public accessor for CPU (needed by composite drivers for address space wiring)
	tmp94c241_device *maincpu() { return m_maincpu; }

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;
	virtual const tiny_rom_entry *device_rom_region() const override ATTR_COLD;
	virtual ioport_constructor device_input_ports() const override ATTR_COLD;

private:
	required_device<class kn5000_cpanel_device> m_cpanel;
	required_device<tmp94c241_device> m_maincpu;
	required_device<tmp94c241_device> m_subcpu;
	required_device<class generic_latch_8_device> m_maincpu_latch;
	required_device<class generic_latch_8_device> m_subcpu_latch;
	required_device<class upd72067_device> m_fdc;
	required_ioport m_com_select;
	required_device<class kn5000_extension_connector> m_extension;

	required_ioport_array<11> m_CPL_SEG; // buttons on "Control Panel Left" PCB
	required_ioport_array<11> m_CPR_SEG; // buttons on "Control Panel Right" PCB
	output_finder<> m_checking_device_led_cn11;
	output_finder<> m_checking_device_led_cn12;
	uint8_t m_mstat;
	uint8_t m_sstat;
	uint8_t m_cpanel_inta;

	void maincpu_mem(address_map &map) ATTR_COLD;
	void subcpu_mem(address_map &map) ATTR_COLD;
};

DECLARE_DEVICE_TYPE(KN5000, kn5000_state)

#endif // MAME_MATSUSHITA_KN5000_H
