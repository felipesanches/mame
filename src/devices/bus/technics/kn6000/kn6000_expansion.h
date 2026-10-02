// license:GPL-2.0+
// copyright-holders:Felipe Sanches

#ifndef MAME_BUS_TECHNICS_KN6000_KN6000_EXPANSION_H
#define MAME_BUS_TECHNICS_KN6000_KN6000_EXPANSION_H

#pragma once

// Expansion connector on the SX-KN6000 and SX-KN6500 main board. The KN6500
// service manual shows it as CN106, a 70-pin connector labelled "TO HDD", and
// the chip-select decoder on the same sheet emits EXP.CS0 and EXP.CS1 for it.
//
// Signals carried on CN106, per the KN6500 service manual's connector table:
//   HDD.CS  HDD.IOCS  HDD.INT      chip select, I/O select and interrupt for the unit
//   PP.INT                         interrupt from the unit's parallel-port interface
//   R/NW  RE  WE2  WE3  IORST      bus strobes and I/O reset
//   D0..D11+  A18  A19  ...        data and address bus
//   SDO1  SDO2  BCK  LRCK  DACCK  serial AUDIO OUT from the unit, plus its bit and
//                                  word clocks -- the same arrangement the HD-AE5000
//                                  uses on the KN5000 to provide separate outputs
//   CLK1  CKOUT                    clocks
//   +15M  +5A  +5D  +3.3D  E       supplies and ground
//
// Modelled here: the slot, and a hook for the fitted unit to map itself into the
// host's program space.

class device_kn6000_expansion_interface;

class kn6000_expansion_connector : public device_t, public device_single_card_slot_interface<device_kn6000_expansion_interface>
{
public:
	// let the fitted unit decode its own window in the host program space
	void program_map(address_space_installer &space);

	template <typename T>
	kn6000_expansion_connector(const machine_config &mconfig, const char *tag, device_t *owner, T &&opts, const char *dflt)
		: kn6000_expansion_connector(mconfig, tag, owner, 0)
	{
		set_options(std::forward<T>(opts), dflt, false);
	}

	kn6000_expansion_connector(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

protected:
	virtual void device_start() override ATTR_COLD;
};

class device_kn6000_expansion_interface : public device_interface
{
public:
	virtual void program_map(address_space_installer &space) = 0;

protected:
	device_kn6000_expansion_interface(const machine_config &mconfig, device_t &device);
};

DECLARE_DEVICE_TYPE(KN6000_EXPANSION, kn6000_expansion_connector)

void kn6000_expansion_intf(device_slot_interface &device);

#endif // MAME_BUS_TECHNICS_KN6000_KN6000_EXPANSION_H
