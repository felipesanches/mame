// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ 20-bit ALU

    Five cascaded 74S181 4-bit ALUs (20 bits) plus a condition-code PAL.
    PERQemu models the datapath directly and replaces the PAL with a
    64-entry precomputed flag table; this port reproduces both.

    Ported from PERQemu CPU/ALU.cs by Josh Dersch (GPL-3.0+); see perq.h.

***************************************************************************/

#ifndef MAME_CPU_PERQ_PERQALU_H
#define MAME_CPU_PERQ_PERQALU_H

#pragma once

class perq_alu
{
public:
	// the 4-bit ALU function field (Instruction.cs ALUOperation)
	enum
	{
		OP_A = 0x0, OP_B = 0x1, OP_NOTA = 0x2, OP_NOTB = 0x3,
		OP_AANDB = 0x4, OP_AANDNOTB = 0x5, OP_ANANDB = 0x6, OP_AORB = 0x7,
		OP_AORNOTB = 0x8, OP_ANORB = 0x9, OP_AXORB = 0xa, OP_AXNORB = 0xb,
		OP_APLUSB = 0xc, OP_APLUSBPLUSCARRY = 0xd, OP_AMINUSB = 0xe, OP_AMINUSBMINUSCARRY = 0xf
	};

	struct regs
	{
		u32  r;        // 20-bit result (masked to 0xfffff)
		int  carry19;  // 0/1 carry out of bit 19 (subtract: 1 == no borrow)
		bool ovf, cry, leq, lss, geq, gtr, neq, eql;
	};

	perq_alu() { build_flag_table(); reset(); }

	void reset() { m_regs = regs{ 0, 0, false, false, false, false, false, false, false, false }; }

	void do_op(u32 amux, u32 bmux, u8 op);

	// overwrite R without disturbing any flags (victim/MQ back door);
	// callers mask, matching ALU.cs SetR()
	void set_r(u32 r) { m_regs.r = r; }

	const regs &registers() const { return m_regs; }

private:
	void build_flag_table();

	regs m_regs;
	u8   m_pal[64];   // packed flag bits per PAL index
};

#endif // MAME_CPU_PERQ_PERQALU_H
