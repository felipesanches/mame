// license:GPL-2.0+
// copyright-holders:Felipe Sanches
#pragma once

#ifndef __ANOTHERWORLD_H__
#define __ANOTHERWORLD_H__

#define PC       m_pc
#define SP       m_sp
#define READ_BYTE_AW(A) (m_program->read_byte(A))
#define WRITE_BYTE_AW(A,V) (m_program->write_byte(A,V))
#define READ_WORD_AW(A) (READ_BYTE_AW(A) << 8 | READ_BYTE_AW(A+1))

#define ADDRESS_MASK_64K    0xFFFF
#define INCREMENT_PC_64K    (PC = (PC+1) & ADDRESS_MASK_64K)
#define DECREMENT_PC_64K    (PC = (PC-1) & ADDRESS_MASK_64K)

#define VM_NO_SETVEC_REQUESTED 0xFFFF
#define VM_INACTIVE_THREAD 0xFFFF
#define NUM_THREADS 64
#define GAME_PART(n) (0x3E80 + n)

//a couple optional features for easing VM debugging:
//#define DUMP_VM_EXECUTION_LOG
//#define SPEEDUP_VM_EXECUTION

enum ScriptVars {
    VM_VARIABLE_RANDOM_SEED          = 0x3C,
    VM_VARIABLE_LAST_KEYCHAR         = 0xDA,
    VM_VARIABLE_HERO_POS_UP_DOWN     = 0xE5,
    VM_VARIABLE_MUS_MARK             = 0xF4,
    VM_VARIABLE_SCROLL_Y             = 0xF9,
    VM_VARIABLE_HERO_ACTION          = 0xFA,
    VM_VARIABLE_HERO_POS_JUMP_DOWN   = 0xFB,
    VM_VARIABLE_HERO_POS_LEFT_RIGHT  = 0xFC,
    VM_VARIABLE_HERO_POS_MASK        = 0xFD,
    VM_VARIABLE_HERO_ACTION_POS_MASK = 0xFE,
    VM_VARIABLE_PAUSE_SLICES         = 0xFF
};

/* register IDs */
enum
{
    ANOTHER_WORLD_PC=1, ANOTHER_WORLD_SP, ANOTHER_WORLD_CUR_THREAD
};

class another_world_cpu_device : public cpu_device
{
public:
    // construction/destruction
    another_world_cpu_device(const machine_config &mconfig, const char *_tag, device_t *_owner, UINT32 _clock);
    //void set_video_pointers(tilemap_t *tm);
    void write_vm_variable(uint8_t i, uint16_t value);

protected:
    
    virtual void execute_run() override;
    virtual offs_t disasm_disassemble(char *buffer, offs_t pc, const UINT8 *oprom, const UINT8 *opram, UINT32 options) override;

    address_space_config m_program_config;
    address_space_config m_data_config;

    /* processor registers */
    unsigned int m_pc;
    uint8_t m_sp;
    int m_icount;

    address_space *m_program;
    address_space *m_data;

    // device-level overrides
    virtual void device_start() override;
    virtual void device_reset() override;

    // device_execute_interface overrides
    virtual UINT32 execute_min_cycles() const override { return 1; }
    virtual UINT32 execute_max_cycles() const override { return 2; }

    // device_memory_interface overrides
    virtual const address_space_config *memory_space_config(address_spacenum spacenum = AS_0) const override {
        return (spacenum == AS_PROGRAM) ? &m_program_config :
               (spacenum == AS_DATA) ? &m_data_config : NULL;
    }

    // device_disasm_interface overrides
    virtual UINT32 disasm_min_opcode_bytes() const override { return 1; }
    virtual UINT32 disasm_max_opcode_bytes() const override { return 8; }

    uint16_t vmThreads[NUM_THREADS];
    uint16_t requested_PC[NUM_THREADS];
    bool vmThreadIsFrozen[NUM_THREADS];
    bool requested_state[NUM_THREADS];
    uint16_t vmStackCalls[256];
    int m_currentThread;
    uint16_t m_currentPartId;

    //video-related:
    bool m_useVideo2;

#ifdef DUMP_VM_EXECUTION_LOG
    FILE* m_address_log;
#endif

private:
    void nextThread();
    void execute_instruction();
    uint8_t fetch_byte();
    uint16_t fetch_word();
    uint16_t read_vm_variable(uint8_t i);
    void write_videoram(uint16_t x, uint16_t y, uint8_t c);
    //tilemap_t *m_char_tilemap;
};


extern const device_type ANOTHER_WORLD;

#endif /* __ANOTHERWORLD_H__ */
