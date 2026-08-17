// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Optical punched tape reader for the Patinho Feio

    Hewlett-Packard HP-2737-A, historically on channel /E.  J. J. Neto,
    "Aspectos do Projeto de Software de um Minicomputador" (1975), chapter 1,
    lists "Leitora Otica de Fita de Papel HP-2737-A, 300 caracteres por
    segundo (maximo)"; chapter 12 of the July 1977 assembler manual lists
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

	// device_patinho_io_card_interface implementation
	virtual void control_w(int state) override;
	virtual void func_w(uint8_t cmd) override;
	virtual bool device_ok() const override { return is_loaded(); }
	virtual void card_reset() override;

private:
	TIMER_CALLBACK_MEMBER(step);

	emu_timer *m_step_timer = nullptr;
	bool m_skip_feed = false;
};

DECLARE_DEVICE_TYPE(PATINHO_PTREADER, patinho_ptreader_device)

#endif // MAME_BUS_PATINHO_PTREADER_H
