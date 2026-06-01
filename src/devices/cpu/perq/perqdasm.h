// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    Three Rivers PERQ microengine disassembler

    Ported from PERQemu (https://github.com/skeezicsb/PERQemu, originally
    https://github.com/jdersch/PERQemu) by Josh Dersch, which is licensed
    under the GNU General Public License v3 or later.  This driver
    therefore inherits the GPL-3.0+ license.

***************************************************************************/

#ifndef MAME_CPU_PERQ_PERQDASM_H
#define MAME_CPU_PERQ_PERQDASM_H

#pragma once

class perq_disassembler : public util::disasm_interface
{
public:
	perq_disassembler() = default;
	virtual ~perq_disassembler() = default;

	virtual u32 opcode_alignment() const override;
	virtual offs_t disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params) override;
};

#endif // MAME_CPU_PERQ_PERQDASM_H
