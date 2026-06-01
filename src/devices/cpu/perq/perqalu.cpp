// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ 20-bit ALU

    Ported from PERQemu CPU/ALU.cs by Josh Dersch (GPL-3.0+); see perqalu.h.

    C# uses signed 32-bit int; the intermediate result is kept in a wide
    signed value so the pre-mask carry/borrow comparisons behave the same,
    then clipped to 20 bits.

***************************************************************************/

#include "emu.h"
#include "perqalu.h"

void perq_alu::do_op(u32 amux, u32 bmux, u8 op)
{
	// carry-in is the PREVIOUS op's bit-15 carry, read before we overwrite it
	const int last_carry15 = m_regs.cry ? 1 : 0;
	m_regs.carry19 = 0;
	bool carry15 = false;
	bool arith_x = false;   // this op is a subtract
	bool arith_y = false;   // this op is an add

	const s32 a = s32(amux);
	const s32 b = s32(bmux);
	s32 res = 0;

	switch (op & 0xf)
	{
	case OP_A:        res = a;                 break;
	case OP_B:        res = b;                 break;
	case OP_NOTA:     res = ~a;                break;
	case OP_NOTB:     res = ~b;                break;
	case OP_AANDB:    res = a & b;             break;
	case OP_AANDNOTB: res = a & ~b;            break;
	case OP_ANANDB:   res = ~(a & b);          break;
	case OP_AORB:     res = a | b;             break;
	case OP_AORNOTB:  res = a | ~b;            break;
	case OP_ANORB:    res = ~(a | b);          break;
	case OP_AXORB:    res = a ^ b;             break;
	case OP_AXNORB:   res = (a & b) | (~a & ~b); break;

	case OP_APLUSB:
		res = a + b;
		arith_y = true;
		m_regs.carry19 = (res > 0xfffff) ? 1 : 0;
		carry15 = ((a & 0xffff) + (b & 0xffff)) > 0xffff;
		break;

	case OP_APLUSBPLUSCARRY:
		res = a + b + last_carry15;
		arith_y = true;
		m_regs.carry19 = (res > 0xfffff) ? 1 : 0;
		carry15 = ((a & 0xffff) + (b & 0xffff) + last_carry15) > 0xffff;
		break;

	case OP_AMINUSB:
		res = a - b;
		arith_x = true;
		m_regs.carry19 = ((a - b) < 0) ? 0 : 1;
		carry15 = ((a & 0xffff) - (b & 0xffff)) >= 0;
		break;

	case OP_AMINUSBMINUSCARRY:
	{
		const int borrow = (~last_carry15) & 0x1;
		res = a - b - borrow;
		arith_x = true;
		m_regs.carry19 = ((a - b - borrow) < 0) ? 0 : 1;
		carry15 = ((a & 0xffff) - (b & 0xffff) - borrow) >= 0;
		break;
	}
	}

	m_regs.r = u32(res) & 0xfffff;

	const bool r15   = (m_regs.r & 0x8000) == 0;   // NOTE: true when bit 15 is ZERO
	const bool laeqb = (m_regs.r & 0xffff) != 0;   // NOTE: true when low 16 bits NONzero
	const bool lb15  = (bmux & 0x8000) != 0;
	const bool la15  = (amux & 0x8000) != 0;

	m_regs.cry = carry15;
	m_regs.neq = laeqb;
	m_regs.eql = !laeqb;

	const int index = (r15 ? 0x01 : 0) | (la15 ? 0x02 : 0) | (lb15 ? 0x04 : 0)
			| (laeqb ? 0x08 : 0) | (arith_x ? 0x10 : 0) | (arith_y ? 0x20 : 0);

	const u8 flags = m_pal[index];
	m_regs.ovf = BIT(flags, 0);
	m_regs.leq = BIT(flags, 1);
	m_regs.lss = BIT(flags, 2);
	m_regs.geq = BIT(flags, 3);
	m_regs.gtr = BIT(flags, 4);
}

void perq_alu::build_flag_table()
{
	// reproduce the condition-code PAL equations (ALU.cs BuildFlagTable); the
	// outputs are active-low, hence the leading negation on each OR-chain
	for (int index = 0; index < 64; index++)
	{
		const bool r15   = BIT(index, 0);
		const bool la15  = BIT(index, 1);
		const bool lb15  = BIT(index, 2);
		const bool laeqb = BIT(index, 3);
		const bool ax    = BIT(index, 4);   // arithX (subtract)
		const bool ay    = BIT(index, 5);   // arithY (add)

		const bool ovf = !(
				( r15 && !la15) ||
				(!ax && r15 && !lb15) ||
				(!ay && r15 && lb15) ||
				(!r15 && la15) ||
				(ay && !r15 && lb15) ||
				(!ax && !ay && !r15) ||
				(ax && !r15 && !lb15));

		const bool leq = !(
				(ax && !ay && !r15 && laeqb && lb15 && !la15) ||
				(!ax && ay && !r15 && laeqb && !lb15 && !la15) ||
				(r15 && laeqb && !la15) ||
				(!ax && r15 && laeqb && !lb15) ||
				(!ay && r15 && laeqb && lb15));

		const bool lss = !(
				(ax && !ay && !r15 && lb15 && !la15) ||
				(!ax && ay && !r15 && !lb15 && !la15) ||
				(r15 && !la15) ||
				(!ax && r15 && !lb15) ||
				(!ay && r15 && lb15));

		const bool geq = !(
				(ax && !ay && r15 && !lb15 && la15) ||
				(!ax && ay && r15 && lb15 && la15) ||
				(!r15 && la15) ||
				(!ax && !ay && !r15) ||
				(ax && !r15 && !lb15) ||
				(ay && !r15 && lb15));

		const bool gtr = !(
				(!laeqb) ||
				(ax && !ay && r15 && !lb15 && la15) ||
				(!ax && ay && r15 && lb15 && la15) ||
				(!r15 && la15) ||
				(!ax && !ay && !r15) ||
				(ax && !r15 && !lb15) ||
				(ay && !r15 && lb15));

		m_pal[index] = (ovf ? 0x01 : 0) | (leq ? 0x02 : 0) | (lss ? 0x04 : 0)
				| (geq ? 0x08 : 0) | (gtr ? 0x10 : 0);
	}
}
