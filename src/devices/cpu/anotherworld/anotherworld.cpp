// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/*
    Emulation of the Another World Virtual Machine
*/

#include "emu.h"
#include "debugger.h"
#include "anotherworld.h"

#define PC       m_pc
#define ACC      m_acc

#define READ_BYTE_AW(A) (m_program->read_byte(A))
#define WRITE_BYTE_AW(A,V) (m_program->write_byte(A,V))

#define READ_WORD_AW(A) (READ_BYTE_AW(A+1)*256 + READ_BYTE_AW(A))

#define ADDRESS_MASK_64K    0xFFFF
#define INCREMENT_PC_64K    (PC = (PC+1) & ADDRESS_MASK_64K)

const device_type ANOTHER_WORLD  = &device_creator<another_world_cpu_device>;

another_world_cpu_device::another_world_cpu_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
    : cpu_device(mconfig, ANOTHER_WORLD, "ANOTHER WORLD", tag, owner, clock, "another_world_cpu", __FILE__),
      m_program_config("program", ENDIANNESS_LITTLE, 8, 16, 0),
      m_icount(0)
{
}

void another_world_cpu_device::device_start()
{
    save_item(NAME(m_pc));
    save_item(NAME(m_acc));

    // Register state for debugger
    state_add( ANOTHER_WORLD_PC,         "PC",       m_pc         ).mask(0xFFF);
    state_add( ANOTHER_WORLD_ACC,        "ACC",      m_acc        ).mask(0xFF);
    state_add(STATE_GENPC, "GENPC", m_pc).formatstr("0%06O").noshow();

    m_icountptr = &m_icount;
}

void another_world_cpu_device::device_reset()
{
    m_pc = 0;
    m_acc = 0;
}

/* execute instructions on this CPU until icount expires */
void another_world_cpu_device::execute_run()
{
    do
    {
        execute_instruction();
        m_icount --;
    }
    while (m_icount > 0);
}

/* execute one instruction */
void another_world_cpu_device::execute_instruction()
{
    debugger_instruction_hook(this, PC);
//    unsigned char value;
    unsigned char opcode = READ_BYTE_AW(PC);
    INCREMENT_PC_64K;

    switch (opcode & 0xF0){
        case 0x00:
            return;
    }
    printf("unimplemented opcode: 0x%02X\n", opcode);
}

offs_t another_world_cpu_device::disasm_disassemble(char *buffer, offs_t pc, const UINT8 *oprom, const UINT8 *opram, UINT32 options)
{
    extern CPU_DISASSEMBLE( another_world );
    return CPU_DISASSEMBLE_NAME(another_world)(this, buffer, pc, oprom, opram, options);
}

