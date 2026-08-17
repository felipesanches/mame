// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Connector for the sound synthesiser of the EPUSP

    See epusp.h for why the instrument gets a port of its own instead of a
    slot on the Patinho Feio's I/O bus.

***************************************************************************/

#include "emu.h"
#include "epusp.h"

#include "synth.h"


DEFINE_DEVICE_TYPE(EPUSP_SYNTH_PORT, epusp_synth_port_device, "epusp_synth_port", "EPUSP synthesiser port")

epusp_synth_port_device::epusp_synth_port_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, EPUSP_SYNTH_PORT, tag, owner, clock)
	, device_single_card_slot_interface<device_epusp_synth_port_interface>(mconfig, *this)
	, m_sync_handler(*this)
	, m_dev(nullptr)
{
}

epusp_synth_port_device::~epusp_synth_port_device()
{
}

void epusp_synth_port_device::device_config_complete()
{
	m_dev = get_card_device();
}

void epusp_synth_port_device::device_start()
{
}

/* NOTHING PLUGGED IN IS A LEGAL CONFIGURATION, and it has to be silent rather
   than fatal: the host is a computer that ran for years with no instrument on
   the other end of those duplex boards.  With the port empty every transfer is
   dropped here and the machine behaves exactly as if the cable were unplugged,
   which is the whole point of putting a connector in between. */
void epusp_synth_port_device::command_w(uint16_t pair)
{
	if (m_dev)
		m_dev->command_w(pair);
}

void epusp_synth_port_device::tick_w(int state)
{
	if (m_dev)
		m_dev->tick_w(state);
}


device_epusp_synth_port_interface::device_epusp_synth_port_interface(const machine_config &mconfig, device_t &device)
	: device_interface(device, "epusp_synth_port")
	, m_port(dynamic_cast<epusp_synth_port_device *>(device.owner()))
{
}

device_epusp_synth_port_interface::~device_epusp_synth_port_interface()
{
}


void epusp_synth_devices(device_slot_interface &device)
{
	device.option_add("synth", EPUSP_SYNTH); // EPUSP synthesiser, Guido Stolfi, 1977
}
