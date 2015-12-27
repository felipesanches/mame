// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

	anotherworld_dasm.h

	Another World VM bytecode disassembler.

***************************************************************************/

#ifndef MAME_CPU_ANOTHERWORLD_VM_DASM_H
#define MAME_CPU_ANOTHERWORLD_VM_DASM_H

#pragma once

class anotherworld_disassembler : public util::disasm_interface
{
public:
	anotherworld_disassembler() = default;
	virtual ~anotherworld_disassembler() = default;

	virtual u32 opcode_alignment() const override;
	virtual offs_t disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params) override;
};

#endif
