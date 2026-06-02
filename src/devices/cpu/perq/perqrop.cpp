// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ RasterOp datapath - implementation

    Ported from PERQemu Memory/RasterOp.cs by Josh Dersch (GPL-3.0+).

***************************************************************************/

#include "emu.h"
#include "perqrop.h"
#include "perqmem.h"


void perq_rasterop::reset()
{
	m_state = ST_OFF;
	m_phase = PH_BEGIN;
	m_setup_done = false;

	m_latch_on = false;
	m_enabled = false;
	m_extra_src_word = false;
	m_direction = DIR_LTOR;

	m_width_extra_words = 0;
	m_width_extra_bits = 0;

	m_function = FN_INSERT;
	m_src_bit_offset = m_src_word_position = 0;
	m_dest_bit_offset = m_dest_word_position = 0;

	m_x_offset = 0;
	m_last_src_position = 0;
	m_src_needs_aligned = true;
	m_left_over = false;
	m_left_edge_mask = m_right_edge_mask = m_both_edges_mask = 0;

	m_half_pipe = rop_word{};
	clear_fifo(m_src_fifo);
	clear_fifo(m_dest_fifo);
	m_shifter.reset();
}

void perq_rasterop::load_roms(const u8 *rds, u32 rdslen, const u8 *rsc, u32 rsclen)
{
	// RDS00: 9-bit index -> CombinerFlags; RSC03: 7-bit index -> EdgeStrategy
	for (int i = 0; i < 512; i++) m_rds_table[i] = (u32(i) < rdslen) ? rds[i] : 0;
	for (int i = 0; i < 128; i++) m_rsc_table[i] = (u32(i) < rsclen) ? rsc[i] : 0;
}

void perq_rasterop::cntl_rasterop(u16 value)
{
	m_latch_on       = (value & 0x40) != 0;
	m_extra_src_word = (value & 0x20) != 0;
	m_phase          = u8((value & 0x1c) >> 2);
	m_enabled        = (value & 0x02) != 0;
	m_direction      = (value & 0x01) != 0 ? DIR_RTOL : DIR_LTOR;

	if (m_enabled)
	{
		// pre-compute masks / program the shifter (a no-op if already set up,
		// so resuming after a phase change or paused interrupt is safe)
		setup();

		// CntlRasterOp is issued in T1, two cycles ahead of the Fetch that begins
		// the source-dest-idle cycle, so force the initial state to SrcFetch.
		m_state = ST_SRCFETCH;
	}
	else
	{
		m_state = ST_OFF;
		// if the latch bit is clear this transfer is finishing (not pausing for a
		// video interrupt), so allow a fresh Setup() next time
		if (!m_latch_on)
			m_setup_done = false;
	}
}

void perq_rasterop::wid_rasterop(u16 value)
{
	// the 16K Multiply/Divide control on bits <7:6> is handled by the CPU's SF
	// dispatch; this unit only needs the widths
	m_width_extra_words = (value & 0x30) >> 4;
	m_width_extra_bits  = (value & 0x0f);

	// loading the width register clears the FIFOs
	clear_fifo(m_src_fifo);
	clear_fifo(m_dest_fifo);
	m_half_pipe = rop_word{};
}

void perq_rasterop::dst_rasterop(u16 value)
{
	m_function           = u8((m_function & 0x4) | (((~value) & 0xc0) >> 6));
	m_dest_word_position = (value & 0x30) >> 4;
	m_dest_bit_offset    = (value & 0x0f);
}

void perq_rasterop::src_rasterop(u16 value)
{
	m_function          = u8((m_function & 0x3) | (((~value) & 0x40) >> 4));
	m_src_word_position = (value & 0x30) >> 4;
	m_src_bit_offset    = (value & 0x0f);

	// value bit7 == 0 is the PERQ-1 power-supply latch (power off).  POS keeps it
	// set throughout boot, so we do not act on it here; a clean machine power-off
	// is wired in a later phase.
}

void perq_rasterop::clock()
{
	m_state = next_state();

	if (!m_enabled)
		return;

	switch (m_state)
	{
	case ST_IDLE:
		// realign the source FIFO's leading edge at the start of a scan line
		if (m_src_needs_aligned && !m_src_fifo.empty())
			clear_leading_src_words();
		break;

	case ST_DESTFETCH:
		{
			rop_word w = fetch_next_word();
			w.mask = dest_word_mask(w.index);
			m_dest_fifo.push(compute_result(w));
		}
		break;

	case ST_SRCFETCH:
		if (m_left_over)
			clear_extra_src_words();
		m_src_fifo.push(fetch_next_word());
		break;

	case ST_OFF:
	default:
		break;
	}
}

u16 perq_rasterop::result()
{
	if (m_dest_fifo.empty())
		return 0;   // a Store is only issued when a result is ready; defensive
	const u16 v = m_dest_fifo.front().data;
	m_dest_fifo.pop();
	return v;
}

perq_rasterop::rop_word perq_rasterop::compute_result(rop_word dest)
{
	rop_word src = dest;             // init to silence -Wmaybe-uninitialized
	u8  e = ES_NOPOP_NOPEEK;
	u16 aligned, combined;

	// --- pick the source word(s) for this destination word -------------------
	switch (dest.mask)
	{
	case CF_DONTMASK:
		// outside the first edge: return dest unmodified, leave the source FIFO
		return dest;

	case CF_LEFTOVER:
		// outside the second edge: return dest unmodified, but drop a source word
		if (!m_src_fifo.empty())
		{
			src = m_src_fifo.front();
			m_src_fifo.pop();
		}
		return dest;

	case CF_FULLWORD:
		// full word: there should always be a matching source word
		if (!m_src_fifo.empty())
		{
			src = m_src_fifo.front();
			m_src_fifo.pop();
		}
		break;

	case CF_LEFTEDGE:
	case CF_RIGHTEDGE:
		m_left_over = (dest.mask == CF_LEFTEDGE && m_direction == DIR_RTOL) ||
		              (dest.mask == CF_RIGHTEDGE && m_direction == DIR_LTOR);
		if (!m_src_fifo.empty())
		{
			src = m_src_fifo.front();           // peek
			src.mask = src_word_mask(src.index);
			e = get_edge_strategy(dest.mask, src.mask);
		}
		else if (m_left_over && (m_extra_src_word || m_x_offset > 0))
		{
			// ran out of source words while peeking forward at end of line:
			// reuse the half-pipeline register to finish the line
			src = m_half_pipe;
		}
		break;

	case CF_BOTH:
		m_left_over = true;
		if (!m_src_fifo.empty())
		{
			src = m_src_fifo.front();           // peek
			src.mask = src_word_mask(src.index);
			e = get_edge_strategy(dest.mask, src.mask);
		}
		break;

	default:                                    // CF_INVALID etc.
		return dest;
	}

	// pop the current source word?
	if (e == ES_POP_PEEK || e == ES_POP_NOPEEK)
	{
		if (!m_src_fifo.empty())
			m_src_fifo.pop();
	}

	// peek ahead to the next source word?
	if (e == ES_POP_PEEK || e == ES_NOPOP_PEEK)
	{
		if (!m_src_fifo.empty())
		{
			src = m_src_fifo.front();
			m_src_fifo.pop();
			src.mask = src_word_mask(src.index);
		}
	}

	// --- bit-align the source word if SrcX != DstX ----------------------------
	if (m_x_offset != 0)
	{
		// the MSB of the combined shifter inputs is the leftmost pixel in the
		// update region (so it depends on the transfer direction)
		if (m_direction == DIR_LTOR)
			m_shifter.shift(src.data, m_half_pipe.data);
		else
			m_shifter.shift(m_half_pipe.data, src.data);

		m_half_pipe = src;
		aligned = m_shifter.output();
	}
	else
	{
		aligned = src.data;
	}

	// --- combine source & destination through the appropriate mask ------------
	switch (dest.mask)
	{
	case CF_LEFTEDGE:  combined = combine(dest.data, aligned, m_left_edge_mask);  break;
	case CF_RIGHTEDGE: combined = combine(dest.data, aligned, m_right_edge_mask); break;
	case CF_BOTH:      combined = combine(dest.data, aligned, m_both_edges_mask); break;
	case CF_FULLWORD:  combined = combine(dest.data, aligned, 0xffff);            break;
	default:           combined = dest.data;                                      break;
	}

	dest.data = combined;
	return dest;
}

void perq_rasterop::setup()
{
	if (m_setup_done)
		return;

	// shifter: rotate by the bit difference between SrcX and DstX
	m_x_offset = (16 + (m_dest_bit_offset - m_src_bit_offset)) & 0xf;
	m_shifter.set_command(u8(perq_shifter::ROTATE), u8(m_x_offset), 0);

	// region bitmasks
	m_left_edge_mask  = u16(0xffff >> m_dest_bit_offset);
	m_right_edge_mask = u16(~(0xffff >> (((m_dest_bit_offset + m_width_extra_bits) & 0xf) + 1)));
	m_both_edges_mask = u16(~(m_left_edge_mask ^ m_right_edge_mask));   // xnor 'em

	const bool span_src_words  = (m_src_bit_offset  + m_width_extra_bits > 15);
	const bool span_dest_words = (m_dest_bit_offset + m_width_extra_bits > 15);

	// destination width, accounting for wrap & direction
	int width = (4 + (m_width_extra_words - m_dest_word_position)) & 0x3;
	if (span_dest_words) width--;

	m_last_src_position = (m_src_word_position + width) & 0x3;
	if (span_src_words) m_last_src_position = (m_last_src_position + 1) & 0x3;

	m_src_needs_aligned = true;   // start outside the update region
	m_left_over = false;
	m_setup_done = true;
}

perq_rasterop::state perq_rasterop::next_state() const
{
	// clocked after the memory "tick"; the state change lands in T2, coinciding
	// with fetched data appearing on MDI
	const int t = m_mem.tstate();
	state next = state(m_state);

	switch (m_state)
	{
	case ST_OFF:
		break;

	case ST_IDLE:
		if (t == 2)
			next = (m_phase == PH_FIRSTSOURCE || m_phase == PH_XTRASOURCE) ? ST_SRCFETCH : ST_DESTFETCH;
		break;

	case ST_DESTFETCH:
		if (t == 2)
			next = ST_SRCFETCH;
		break;

	case ST_SRCFETCH:
		if (t == 2)
			next = ST_IDLE;
		break;

	default:
		break;
	}
	return next;
}

perq_rasterop::rop_word perq_rasterop::fetch_next_word()
{
	rop_word w;
	if (m_mem.mdi_valid())
	{
		// the microcode computes the addresses and issues the fetches; we just
		// pluck the next incoming word off MDI
		w.address = m_mem.mdi_address();
		w.index   = m_mem.mdi_index();
		w.data    = m_mem.mdi();
	}
	// (MDI invalid here would mean the microcode timing is off; leave w cleared)
	return w;
}

u8 perq_rasterop::dest_word_mask(int index) const
{
	const int lookup = ((int(m_phase) & 0x3) << 7) |
	                   (int(m_direction) << 6) |
	                   (m_dest_word_position << 4) |
	                   (m_width_extra_words << 2) |
	                   index;
	return m_rds_table[lookup & 0x1ff];
}

u8 perq_rasterop::src_word_mask(int index) const
{
	const int lookup = ((int(m_phase) & 0x3) << 7) |
	                   (int(m_direction) << 6) |
	                   (m_src_word_position << 4) |
	                   (m_last_src_position << 2) |
	                   index;
	return m_rds_table[lookup & 0x1ff];   // same table as the dest word
}

u8 perq_rasterop::get_edge_strategy(u8 dst_mask, u8 src_mask) const
{
	const int lookup = (int(m_direction) << 6) |
	                   ((dst_mask & 0x6) << 3) |
	                   ((src_mask & 0x6) << 1) |
	                   ((m_left_over ? 1 : 0) << 1) |
	                   ((m_extra_src_word && m_x_offset > 0) ? 1 : 0);
	return m_rsc_table[lookup & 0x7f];
}

void perq_rasterop::clear_leading_src_words()
{
	// only the first edge in a Begin phase needs aligning
	if (m_phase == PH_BEGIN || m_phase == PH_BEGINEND || m_phase == PH_BEGINENDCLEAR)
	{
		rop_word w = m_src_fifo.front();
		w.mask = src_word_mask(w.index);

		if (w.mask == CF_BOTH ||
		    (m_direction == DIR_LTOR && w.mask == CF_LEFTEDGE) ||
		    (m_direction == DIR_RTOL && w.mask == CF_RIGHTEDGE))
		{
			m_src_needs_aligned = false;   // found the first edge
			m_half_pipe = w;               // prime the half-pipeline register
		}

		if (m_src_needs_aligned)
			m_src_fifo.pop();              // drop the leading word
	}
}

void perq_rasterop::clear_extra_src_words()
{
	// only after the second edge in an End/Clear phase
	if (m_phase == PH_ENDCLEAR || m_phase == PH_BEGINENDCLEAR)
	{
		if (m_src_fifo.empty())
		{
			// used up all the source words: reset for the next scan line
			m_left_over = false;
			m_src_needs_aligned = true;
		}
		else
		{
			const rop_word w = m_src_fifo.front();
			if ((m_direction == DIR_LTOR && w.index == 3) ||
			    (m_direction == DIR_RTOL && w.index == 0))
			{
				m_left_over = false;
				m_src_needs_aligned = true;
				m_src_fifo.pop();
			}

			if (m_left_over)
				m_src_fifo.pop();
		}
	}
}

u16 perq_rasterop::combine(u16 dst_word, u16 src_word, u16 mask) const
{
	switch (m_function)
	{
	case FN_INSERT:                                                                          break;
	case FN_INSERTNOT: src_word = u16(~src_word);                                            break;
	case FN_AND:       src_word = u16(src_word & dst_word);                                  break;
	case FN_ANDNOT:    src_word = u16((~src_word) & dst_word);                               break;
	case FN_OR:        src_word = u16(src_word | dst_word);                                  break;
	case FN_ORNOT:     src_word = u16((~src_word) | dst_word);                               break;
	case FN_XOR:       src_word = u16(src_word ^ dst_word);                                  break;
	case FN_XNOR:      src_word = u16((src_word & dst_word) | ((~src_word) & (~dst_word)));  break;
	}
	return u16((dst_word & ~mask) | (src_word & mask));
}
