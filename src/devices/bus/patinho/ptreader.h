// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Optical punched tape reader for the Patinho Feio

    Hewlett-Packard HP-2737-A, historically on channel /E.

    Doc 03 (J. J. Neto, "Aspectos do Projeto de Software de um
    Minicomputador", 1975), chapter 1, lists the machine's configuration as
    carrying one "Leitora Otica de Fita de Papel HP-2737-A, 300 caracteres por
    segundo (maximo)".  Chapter 12 of the July 1977 assembler manual lists
    channel /E as "Leitora de Fita de Papel", input only.

***************************************************************************/
#ifndef MAME_BUS_PATINHO_PTREADER_H
#define MAME_BUS_PATINHO_PTREADER_H

#pragma once

#include "iobus.h"

#include "imagedev/papertape.h"


// ======================> patinho_ptreader_device

class patinho_ptreader_device : public paper_tape_reader_device,
		public device_patinho_io_card_interface
{
public:
	patinho_ptreader_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// device_image_interface implementation
	virtual const char *file_extensions() const noexcept override { return "bin,txt,ptp"; }

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_pre_save() override ATTR_COLD;
	virtual void device_post_load() override ATTR_COLD;

	// device_patinho_io_card_interface implementation
	virtual void control_w(int state) override;
	virtual void func_w(uint8_t cmd) override;
	virtual bool device_ok() const override { return is_loaded(); }
	virtual void card_reset() override;

private:
	TIMER_CALLBACK_MEMBER(step);

	/* NOT SAVED, and each one for a stated reason:

	     m_step_timer  the pointer is topology, allocated unconditionally in
	                   device_start() with a named FUNC().  The timer's STATE
	                   (period, expiry, enabled) is saved by the scheduler
	                   itself -- see emu_timer::register_save() in
	                   src/emu/schedule.cpp -- so the reel goes back to turning
	                   at 300 Hz after a load without anyone re-arming it here.

	     m_bus, m_channel (from the card interface)  topology, fixed at
	                   device_resolve_objects() time and identical either side
	                   of a load. */

	emu_timer *m_step_timer = nullptr;
	bool m_skip_feed = false;

	/* WHERE THE READING HEAD IS.  This is the one piece of this device's state
	   that does not live in a member: it is the host file position inside
	   device_image_interface, and nothing in MAME's image layer registers it
	   for save states.  Without the pre_save/post_load pair below, loading a
	   state taken half-way through a roll would leave the head wherever the
	   LIVE session had got to, and the absolute loader would then read frames
	   from the wrong offset -- a checksum failure with no error pointing at the
	   cause.  Kept as a plain member so that save_item() can see it. */
	u64 m_read_pos = 0;
};

DECLARE_DEVICE_TYPE(PATINHO_PTREADER, patinho_ptreader_device)

#endif // MAME_BUS_PATINHO_PTREADER_H
