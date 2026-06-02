// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ RasterOp datapath

    The RasterOp hardware works hand-in-hand with the RasterOp microcode to
    move rectangular regions of memory (and the screen) very quickly: the
    microcode issues overlapped quad-word Fetch/Store cycles, and this unit
    feeds the source and destination words through a shifter and a combiner
    (the source/destination word FIFOs + a "half-pipeline" register) to align
    and merge them according to the Control/Source/Destination/Width registers.

    Two small synthesized lookup ROMs drive the edge handling: RDS00 (a 9-bit
    index -> CombinerFlags, marking each word of a quad as left/right/both/full/
    leftover) and RSC03 (a 7-bit index -> an edge strategy telling the source
    FIFO when to pop/peek).

    POS uses RasterOp not only for graphics but for memory-segment relocation
    (CopySegment), so it is needed well before the display is up.

    Ported from PERQemu Memory/RasterOp.cs by Josh Dersch (GPL-3.0+); see perq.h.

***************************************************************************/

#ifndef MAME_CPU_PERQ_PERQROP_H
#define MAME_CPU_PERQ_PERQROP_H

#pragma once

#include "perqshift.h"
#include <queue>

class perq_memory;

class perq_rasterop
{
public:
	// CombinerFlags: the per-word mask the RDS00 ROM returns
	enum : u8
	{
		CF_INVALID  = 0x00,   // word not properly initialised (debugging)
		CF_DONTMASK = 0x01,   // pass word unmodified; beginning of scan line
		CF_LEFTEDGE = 0x02,   // word contains a left edge
		CF_RIGHTEDGE= 0x04,   // word contains a right edge
		CF_BOTH     = 0x06,   // word contains both edges (shortcut)
		CF_FULLWORD = 0x08,   // use all 16 bits
		CF_LEFTOVER = 0x10    // pass word unmodified; clear end of scan line
	};

	// EdgeStrategy: what the RSC03 ROM says to do with the source FIFO
	enum : u8 { ES_NOPOP_NOPEEK = 0, ES_NOPOP_PEEK = 1, ES_POP_NOPEEK = 2, ES_POP_PEEK = 3, ES_UNKNOWN = 7 };

	enum direction : u8 { DIR_LTOR = 0, DIR_RTOL = 1 };

	enum function : u8 { FN_INSERT = 0, FN_INSERTNOT, FN_AND, FN_ANDNOT, FN_OR, FN_ORNOT, FN_XOR, FN_XNOR };

	enum phase : u8 { PH_BEGIN = 0, PH_MID, PH_END, PH_BEGINEND, PH_XTRASOURCE, PH_FIRSTSOURCE, PH_ENDCLEAR, PH_BEGINENDCLEAR, PH_DONE };

	enum state : u8 { ST_IDLE = 0, ST_DESTFETCH, ST_SRCFETCH, ST_OFF };

	// a memory word travelling through the datapath
	struct rop_word { int address = 0; int index = 0; u16 data = 0; u8 mask = CF_INVALID; };

	explicit perq_rasterop(perq_memory &mem) : m_mem(mem) { reset(); }

	void reset();

	// copy the two synthesized lookup ROMs (rds00emu / rsc03emu) into the tables
	void load_roms(const u8 *rds, u32 rdslen, const u8 *rsc, u32 rsclen);

	bool enabled() const { return m_enabled; }

	// the four RasterOp registers, latched by the microinstruction SF function
	void cntl_rasterop(u16 value);   // CntlRasterOp := Z
	void wid_rasterop(u16 value);    // WidRasterOp := R (widths only; MulDiv stays in the CPU)
	void dst_rasterop(u16 value);    // DstRasterOp := R
	void src_rasterop(u16 value);    // SrcRasterOp := R

	// clocked once per micro-cycle after the memory tick, before DispatchFunction
	void clock();

	// a Store4/4R draws its word from here when the datapath has a result ready
	bool   result_ready() const { return !m_dest_fifo.empty(); }
	u16    result();

private:
	void     setup();
	state    next_state() const;
	rop_word fetch_next_word();
	u8       dest_word_mask(int index) const;
	u8       src_word_mask(int index) const;
	u8       get_edge_strategy(u8 dst_mask, u8 src_mask) const;
	void     clear_leading_src_words();
	void     clear_extra_src_words();
	rop_word compute_result(rop_word dest);
	u16      combine(u16 dst_word, u16 src_word, u16 mask) const;

	static void clear_fifo(std::queue<rop_word> &q) { std::queue<rop_word>().swap(q); }

	perq_memory  &m_mem;        // source of MDI / MADR / MIndex / TState
	perq_shifter  m_shifter;    // our own private shifter (the hardware has one)

	// RasterOp state
	u8   m_state;
	u8   m_phase;
	bool m_setup_done;

	// CntlRasterOp register
	bool m_latch_on;
	bool m_enabled;
	bool m_extra_src_word;
	u8   m_direction;

	// WidRasterOp register
	int  m_width_extra_words;
	int  m_width_extra_bits;

	// Src & DstRasterOp registers
	u8   m_function;
	int  m_src_bit_offset;
	int  m_src_word_position;
	int  m_dest_bit_offset;
	int  m_dest_word_position;

	// derived in setup()
	int  m_x_offset;            // bit offset between SrcX and DstX (shifter amount)
	int  m_last_src_position;   // 2nd source edge word, for the region test
	bool m_src_needs_aligned;   // looking for the first edge (source FIFO)
	bool m_left_over;           // the second edge has been processed (dest FIFO)
	u16  m_left_edge_mask;
	u16  m_right_edge_mask;
	u16  m_both_edges_mask;

	rop_word m_half_pipe;       // the "half-pipeline register"

	std::queue<rop_word> m_src_fifo;   // hardware: 16 words (4 quads)
	std::queue<rop_word> m_dest_fifo;  // hardware: 4 words (1 quad)

	u8   m_rds_table[512];      // 9-bit index -> CombinerFlags  (rds00emu.rom)
	u8   m_rsc_table[128];      // 7-bit index -> EdgeStrategy   (rsc03emu.rom)
};

#endif // MAME_CPU_PERQ_PERQROP_H
