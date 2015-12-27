// license:GPL-2.0+
// copyright-holders:Felipe Sanches
#pragma once

#ifndef __ANOTHERWORLD_H__
#define __ANOTHERWORLD_H__

/* register IDs */
enum
{
    ANOTHER_WORLD_PC=1, ANOTHER_WORLD_ACC
};

class another_world_cpu_device : public cpu_device
{
public:
    // construction/destruction
    another_world_cpu_device(const machine_config &mconfig, const char *_tag, device_t *_owner, UINT32 _clock);
protected:
    
    virtual void execute_run() override;
    virtual offs_t disasm_disassemble(char *buffer, offs_t pc, const UINT8 *oprom, const UINT8 *opram, UINT32 options) override;

    address_space_config m_program_config;

    /* processor registers */
    unsigned char m_acc;
    unsigned int m_pc;
    int m_icount;

    address_space *m_program;

    // device-level overrides
    virtual void device_start() override;
    virtual void device_reset() override;

    // device_execute_interface overrides
    virtual UINT32 execute_min_cycles() const override { return 1; }
    virtual UINT32 execute_max_cycles() const override { return 2; }

    // device_memory_interface overrides
    virtual const address_space_config *memory_space_config(address_spacenum spacenum = AS_0) const override { return (spacenum == AS_PROGRAM) ? &m_program_config : NULL; }

    // device_disasm_interface overrides
    virtual UINT32 disasm_min_opcode_bytes() const override { return 1; }
    virtual UINT32 disasm_max_opcode_bytes() const override { return 2; }

private:
    void execute_instruction();
};


extern const device_type ANOTHER_WORLD;

#endif /* __ANOTHERWORLD_H__ */
