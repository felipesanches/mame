// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    Three Rivers PERQ microengine disassembler

    The PERQ executes a 48-bit horizontal microinstruction out of a 4K or
    16K word writable control store.  This is currently a placeholder that
    dumps the raw microword; full field decoding (ported from PERQemu's
    Debugger/Disassembler.cs) lands in a later phase.

    Ported from PERQemu by Josh Dersch (GPL-3.0+); see perqdasm.h.

***************************************************************************/

#include "emu.h"
#include "perqdasm.h"

u32 perq_disassembler::opcode_alignment() const
{
	// one 48-bit microword per control-store location
	return 1;
}

offs_t perq_disassembler::disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params)
{
	const u64 word = opcodes.r64(pc) & 0x0000'ffff'ffff'ffffULL;

	util::stream_format(stream, "uword %012llx", (unsigned long long)word);

	return 1 | SUPPORTED;
}
