// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Connector for the sound synthesiser of the EPUSP (Guido Stolfi, 1977)

    A generic port, in the shape of bus/rs232 and bus/midi: a host machine
    instantiates EPUSP_SYNTH_PORT, and whatever card the user plugs into it
    receives the traffic.  Three wires cross it:

        command_w   host -> instrument, one (command, data) pair per transfer
        tick_w      host -> instrument, the internal time base, offered so the
                    instrument can lay it onto a tape as a sync track
        output_sync instrument -> host, the time base recovered from a tape

    WHY THIS IS A PORT OF ITS OWN AND NOT A CARD OF THE PATINHO I/O BUS

    (a) It is historically false.  Chapter 12 of the July 1977 assembler manual
        lists what was plugged into each channel, and /6 and /7 hold
        general-purpose 8-bit duplex boards -- "INTERFACE DUPLEX DE 8 BITS",
        the same board type twice, addressable by anything.  The synthesiser
        has no board in the machine, no channel of its own, and none of the
        five state elements (CONTROL, STATUS, PEDIDO, PERMISSAO, the data
        register) that define device_patinho_io_card_interface.  It is a
        separate instrument that the computer talked to THROUGH those boards.

    (b) It would not fit anyway.  The two clock lines do not travel over the
        data channel: the recovered 1 kHz and the recorded tick belong to the
        time base generator, which is a card in channel /4.  Modelling the
        instrument as a card in /6 would move the coupling rather than remove
        it -- the card would still have to reach sideways into another slot.

    (c) It would kill any other host.  A card of the Patinho bus can only be
        instantiated by a machine that owns a patinho_io_bus_device, so any
        other computer -- an HP21MX, say -- would have to build a Patinho
        backplane in order to sound a note.  The instrument predates and
        outlives that wiring; the port is what makes it reusable.

***************************************************************************/
#ifndef MAME_BUS_EPUSP_EPUSP_H
#define MAME_BUS_EPUSP_EPUSP_H

#pragma once


class device_epusp_synth_port_interface;


// ======================> epusp_synth_port_device

class epusp_synth_port_device : public device_t, public device_single_card_slot_interface<device_epusp_synth_port_interface>
{
	friend class device_epusp_synth_port_interface;

public:
	template <typename T>
	epusp_synth_port_device(machine_config const &mconfig, char const *tag, device_t *owner, T &&opts, char const *dflt)
		: epusp_synth_port_device(mconfig, tag, owner, uint32_t(0))
	{
		set_options(std::forward<T>(opts), dflt, false);
	}
	epusp_synth_port_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
	virtual ~epusp_synth_port_device();

	// The time base the instrument recovers from a tape, on its way back to
	// whatever the host clocks from it.
	auto sync_handler() { return m_sync_handler.bind(); }

	// One transfer to the instrument: high byte = command, low byte = data.
	// Both of these are safe with an empty port -- that is the point of it.
	void command_w(uint16_t pair);

	// The host's own time base, offered to the instrument.
	void tick_w(int state);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_config_complete() override;

	devcb_write_line m_sync_handler;

private:
	device_epusp_synth_port_interface *m_dev;
};


// ======================> device_epusp_synth_port_interface

class device_epusp_synth_port_interface : public device_interface
{
	friend class epusp_synth_port_device;

public:
	virtual ~device_epusp_synth_port_interface();

	virtual void command_w(uint16_t pair) { }
	virtual void tick_w(int state) { }

protected:
	device_epusp_synth_port_interface(const machine_config &mconfig, device_t &device);

	// The one service the port offers a card: a way back to the host.
	void output_sync(int state) { if (m_port) m_port->m_sync_handler(state); }

	epusp_synth_port_device *m_port;
};


DECLARE_DEVICE_TYPE(EPUSP_SYNTH_PORT, epusp_synth_port_device)

void epusp_synth_devices(device_slot_interface &device) ATTR_COLD;

#endif // MAME_BUS_EPUSP_EPUSP_H
