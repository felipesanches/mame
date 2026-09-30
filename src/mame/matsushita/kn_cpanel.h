// license:GPL-2.0+
// copyright-holders:Felipe Sanches

// Front panel shared by the KN6000 and KN7000 families: the sub-CPUs that scan
// the buttons and drive the LEDs, and their serial link to the main CPU.

#ifndef MAME_MATSUSHITA_KN_CPANEL_H
#define MAME_MATSUSHITA_KN_CPANEL_H

#pragma once

class kn_cpanel_base_device : public device_t
{
public:
	// The analog controls live in the driver's input ports
	template <typename T> void set_dial_port(T &&tag) { m_dial.set_tag(std::forward<T>(tag)); }
	template <typename T> void set_volapcseq_port(T &&tag) { m_volapcseq.set_tag(std::forward<T>(tag)); }
	template <typename T> void set_tempoknob_port(T &&tag) { m_tempoknob.set_tag(std::forward<T>(tag)); }

	auto atn() { return m_atn_cb.bind(); }   // ATN line, to an external interrupt pin
	auto rxd() { return m_rxd_cb.bind(); }   // a reply byte for the main CPU's receiver

	void tx_byte(u8 data);                   // a byte from the main CPU
	void rx_enable(int state);               // the main CPU's receiver is enabled

protected:
	static constexpr int MAX_SEGS = 0x40;

	kn_cpanel_base_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock);

	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// The model's button matrix and LEDs
	virtual int num_scan_ports() const = 0;
	virtual u8 scan_port_read(int port) = 0;
	virtual u8 port_seg(int port) const = 0;
	virtual int num_segs() const = 0;
	virtual u8 seg_wire_addr(int seg) const = 0;   // 0xff: no wire address
	virtual void panel_led_frame(u8 addr, u8 data) = 0;

private:
	optional_ioport m_dial;
	optional_ioport m_volapcseq;
	optional_ioport m_tempoknob;
	devcb_write_line m_atn_cb;
	devcb_write8 m_rxd_cb;

	emu_timer *m_panel_evt;                  // param: 1 = raise ATN, 2 = deliver a byte, 3 = drop ATN
	emu_timer *m_panel_timer;
	ioport_field *m_tempoknob_field;

	u8 m_panel_pos;                          // position within the 7-byte frame from the main CPU
	u8 m_panel_p1;
	u8 m_panel_p2;
	u8 m_panel_resp[64];                     // replies and button events for the main CPU
	u8 m_panel_resp_len;
	u8 m_panel_resp_pos;
	u8 m_btn_prev[MAX_SEGS];
	u8 m_vol_apcseq_prev;
	bool m_vol_apcseq_synced;
	u8 m_dial_prev;
	bool m_dial_synced;
	u8 m_tempoknob_prev;
	bool m_tempoknob_synced;

	void panel_queue(const u8 *bytes, int n);
	TIMER_CALLBACK_MEMBER(panel_event);
	TIMER_CALLBACK_MEMBER(panel_scan);
};

#endif // MAME_MATSUSHITA_KN_CPANEL_H
