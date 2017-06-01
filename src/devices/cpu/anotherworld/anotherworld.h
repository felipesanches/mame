// license:GPL-2.0+
// copyright-holders:Felipe Sanches
#pragma once

#ifndef __ANOTHERWORLD_H__
#define __ANOTHERWORLD_H__

//VM irregularities (see: https://github.com/felipesanches/mame/issues/17)
#define VM_HACK_SWITCH_FROM_INTRO_TO_LAKE // Seems needed both on Amiga and MSDOS bytecode.
#define VM_HACK_BLITFRAMEBUFFER_VAR_F7    // Seems to be unnecessary.
#define VM_HACK_INIT_VAR_54_WITH_81       // Without this one, the Interplay logo does not show up with MSDOS bytecode

//a few optional features for easing VM debugging:
#define DUMP_VM_EXECUTION_LOG
//#define SPEEDUP_VM_EXECUTION
//#define BYPASS_PROTECTION

#include <fstream>
#include <iostream>

#define PC       m_pc
#define SP       m_sp
#define READ_BYTE_AW(A) (m_program->read_byte(A))
#define WRITE_BYTE_AW(A,V) (m_program->write_byte(A,V))
#define READ_WORD_AW(A) (READ_BYTE_AW(A) << 8 | READ_BYTE_AW(A+1))

#define ADDRESS_MASK_64K    0xFFFF
#define INCREMENT_PC_64K    (PC = (PC+1) & ADDRESS_MASK_64K)
#define DECREMENT_PC_64K    (PC = (PC-1) & ADDRESS_MASK_64K)

#define MCFG_ANOTHERW_READ_INPUT_CALLBACK(_devcb) \
    devcb = &another_world_cpu_device::set_read_input_callback(*device, DEVCB_##_devcb);

enum {
  RESET_TYPE__FREEZE_CHANNELS=0,
  RESET_TYPE__UNFREEZE_CHANNELS=1,
  RESET_TYPE__DELETE_CHANNELS=2,
};

enum {
  FROZEN=0,
  UNFROZEN=1,
};

#define INACTIVE_THREAD 0xFFFF
#define DELETE_THIS_THREAD 0xFFFE
#define NO_REQUEST 0xFFFF

#define NUM_THREADS 64
#define GAME_PART(n) (0x3E80 + n)

#define AS_PROGRAM AS_0
#define AS_DATA AS_1
#define AS_PALETTE AS_2
// skip AS_3, which is specific to decrypted opcodes
#define AS_VIDEO AS_4

enum ScriptVars {
    VM_VARIABLE_RANDOM_SEED          = 0x3C,
    VM_VARIABLE_HACK_VAR_54          = 0x54, // Interplay logo  TODO: verify this
    VM_VARIABLE_HACK_VAR_67          = 0x67, // switch from intro to lake  TODO: verify this
    VM_VARIABLE_LAST_KEYCHAR         = 0xDA,
    VM_VARIABLE_HACK_VAR_DC          = 0xDC, // switch from intro to lake  TODO: verify this
    VM_VARIABLE_HERO_POS_UP_DOWN     = 0xE5,
    VM_VARIABLE_MUS_MARK             = 0xF4,
    VM_VARIABLE_HACK_VAR_F7          = 0xF7, // Blitframebuffer (not needed?)  TODO: verify this
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

//TODO: Implement the stack memory as another block of RAM.
class Stack
{
public:
    Stack(uint8_t* sp);

    void push(uint16_t value);
    uint16_t pop();
    void clean();
private:
    uint16_t m_stacked_values[256];
    uint8_t* m_sp;
    bool m_overflow;
};

class Thread
{
public:
    uint16_t PC;
    uint16_t requested_PC;
    bool     state;
    bool     requested_state;
};

class another_world_cpu_device : public cpu_device
{
public:
    // construction/destruction
    another_world_cpu_device(const machine_config &mconfig, const char *_tag, device_t *_owner, uint32_t _clock);
    ~another_world_cpu_device();

    template<class _Object> static devcb_base &set_read_input_callback(device_t &device, _Object object) { return downcast<another_world_cpu_device &>(device).m_read_input.set_callback(object); }

    void write_vm_variable(uint8_t i, uint16_t value);

protected:
    
    virtual void execute_run() override;
    virtual offs_t disasm_disassemble(std::ostream &stream, offs_t pc, const uint8_t *oprom, const uint8_t *opram, uint32_t options) override;

    address_space_config m_program_config;
    address_space_config m_data_config;
    address_space_config m_palette_config;
    address_space_config m_video_config;

    /* processor registers */
    uint16_t m_pc;
    uint8_t m_sp;
    int m_icount;

    address_space *m_program;
    address_space *m_data;
    address_space *m_palette;
    address_space *m_video;

    // device-level overrides
    virtual void device_start() override;
    virtual void device_reset() override;

    // device_execute_interface overrides
    virtual uint32_t execute_min_cycles() const override { return 1; }
    virtual uint32_t execute_max_cycles() const override { return 2; }

    // device_memory_interface overrides
    virtual const address_space_config *memory_space_config(address_spacenum spacenum = AS_0) const override {
        return (spacenum == AS_PROGRAM) ? &m_program_config :
               (spacenum == AS_DATA) ? &m_data_config :
               (spacenum == AS_PALETTE) ? &m_palette_config :
               (spacenum == AS_VIDEO) ? &m_video_config : NULL;
    }

    // device_disasm_interface overrides
    virtual uint32_t disasm_min_opcode_bytes() const override { return 1; }
    virtual uint32_t disasm_max_opcode_bytes() const override { return 8; }

    Thread m_threads[NUM_THREADS];

    Stack*   m_stack;
    int      m_currentThread;
    uint16_t m_currentPartId;
    uint16_t m_requestedNextPart;

    //video-related:
    bool     m_useVideo2;

#ifdef DUMP_VM_EXECUTION_LOG
    std::filebuf m_log_filebuf;
#endif

private:
    void nextThread();
    void input_updatePlayer();
    void checkThreadRequests();
    void execute_instruction();
    void initForPart(uint16_t partId);
    uint8_t fetch_byte();
    uint16_t fetch_word();
    uint16_t read_vm_variable(uint8_t i);
    devcb_read8 m_read_input;
};

// device type definition
DECLARE_DEVICE_TYPE(ANOTHER_WORLD, another_world_cpu_device)

#endif /* __ANOTHERWORLD_H__ */
