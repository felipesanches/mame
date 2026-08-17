// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Optical punched tape reader for the Patinho Feio -- HP-2737-A

    See ptreader.h for the documentary basis.

    The handshake is the one chapter 12 of the assembler manual describes for
    every peripheral.  The reel only turns while CONTROLE is asserted:
    "FNC /E6" turns it on, and delivering a frame drops it again -- which is
    what the manual's "ENTRA C/ DADOS E PARA FITA" says in as many words.  Each
    further frame therefore needs a fresh "FNC /E6", and that is exactly what
    the le_dado_da_fita and ncfim routines of Guido Stolfi's synthesizer
    executor do.

***************************************************************************/

#include "emu.h"
#include "ptreader.h"

DEFINE_DEVICE_TYPE(PATINHO_PTREADER, patinho_ptreader_device, "patinho_ptreader", "HP-2737-A optical punched tape reader")

patinho_ptreader_device::patinho_ptreader_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: paper_tape_reader_device(mconfig, PATINHO_PTREADER, tag, owner, clock)
	, device_patinho_io_card_interface(mconfig, *this)
{
}

void patinho_ptreader_device::device_start()
{
	m_step_timer = timer_alloc(FUNC(patinho_ptreader_device::step), this);

	save_item(NAME(m_skip_feed));
	save_item(NAME(m_read_pos));

	// The five flip-flops of chapter 12 (DADO, CONTROLE, ESTADO, PEDIDO,
	// PERMITE) are registered once for every board by
	// device_patinho_io_card_interface::interface_pre_start(), so that no card
	// can forget them.
}

/* THE READING HEAD, saved by hand because nobody else saves it.

   device_image_interface holds an open host file and does not register its
   position with the save state machinery.  So the position is fetched here on
   the way out and pushed back on the way in.  Without this pair the tape is the
   only part of the machine that would not travel with the state: everything
   else -- core, registers, the five flip-flops, the step timer -- would come
   back to the saved instant while the head stayed at the live one. */
void patinho_ptreader_device::device_pre_save()
{
	if (is_loaded())
		m_read_pos = ftell();
}

void patinho_ptreader_device::device_post_load()
{
	// A state saved with a roll mounted can be loaded with none, or with a
	// different one; seeking a file that is not there would be fatal.
	if (is_loaded())
		fseek(m_read_pos, SEEK_SET);
}

void patinho_ptreader_device::device_reset()
{
	m_step_timer->reset();
	m_skip_feed = false;
}

void patinho_ptreader_device::card_reset()
{
	device_patinho_io_card_interface::card_reset();

	m_step_timer->reset();
	m_skip_feed = false;
}

void patinho_ptreader_device::control_w(int state)
{
	// 300 frames per second at most (doc 03, chapter 1)
	if (state)
		m_step_timer->adjust(attotime::from_hz(300), 0, attotime::from_hz(300));
	else
		m_step_timer->reset();
}

void patinho_ptreader_device::func_w(uint8_t cmd)
{
	// "FNC /E8": the manual says this one only works on the tape reader --
	// "ignora todos os feed-frames (bytes nulos) da fita, ate a proxima
	// perfuracao (1o byte nao nulo)".
	if (cmd == 8)
		m_skip_feed = true;
	else
		device_patinho_io_card_interface::func_w(cmd);
}

TIMER_CALLBACK_MEMBER(patinho_ptreader_device::step)
{
	uint8_t frame;

	do
	{
		if (!is_loaded() || (fread(&frame, 1U) != 1U))
		{
			// End of the roll, or no roll mounted. STATUS stays busy and the
			// program waits, which is what the real machine did when the tape
			// ran out of the head.
			return;
		}
	}
	while (m_skip_feed && !frame);

	m_skip_feed = false;

	// Drops CONTROLE, sets STATUS ready, and raises PEDIDO if enabled.
	receive_byte(frame);
}
