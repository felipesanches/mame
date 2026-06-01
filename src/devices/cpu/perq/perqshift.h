// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    PERQ shifter (shift / rotate / bitfield-extract unit)

    Driven by an 8-bit command byte that is decoded (inverted, since the
    control-store outputs are active-low) into a (command, amount, mask)
    triple.  The CPU instantiates two of these: the main shifter and, on
    the 16K CPU, a second one used by the hardware multiply/divide unit.

    Ported from PERQemu CPU/Shifter.cs by Josh Dersch (GPL-3.0+); see perq.h.

***************************************************************************/

#ifndef MAME_CPU_PERQ_PERQSHIFT_H
#define MAME_CPU_PERQ_PERQSHIFT_H

#pragma once

class perq_shifter
{
public:
	enum { LEFT_SHIFT = 0, RIGHT_SHIFT = 1, ROTATE = 2, FIELD = 3 };

	perq_shifter() { build_table(); reset(); }

	void reset() { m_params = entry{ u8(LEFT_SHIFT), 0, 0 }; m_output = 0; }

	// table form: the byte is complemented inside the table (ShiftOnZ / ~R sites)
	void set_command(int input) { m_params = m_table[input & 0xff]; }

	// direct form, no inversion (used only by the 16K MQ shifter)
	void set_command(u8 cmd, u8 amt, u16 mask) { m_params = entry{ cmd, amt, mask }; }

	void shift(int low, int high);
	void shift(int input) { shift(input, input); }

	u16 output() const { return m_output; }

private:
	struct entry { u8 command; u8 amount; u16 mask; };

	void build_table();

	entry m_table[256];
	entry m_params;
	u16   m_output;
};

#endif // MAME_CPU_PERQ_PERQSHIFT_H
