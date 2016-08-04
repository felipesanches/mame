// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/*
    Emulation of the Another World Virtual Machine
*/

#include "emu.h"
#include "debugger.h"
#include "anotherworld.h"
#include "sound/anotherw.h"
#include "includes/anotherworld.h"

Stack::Stack(uint8_t* sp)
    : m_sp(sp),
      m_overflow(false) { }

void Stack::push(uint16_t value){
    if (*m_sp == 0xFF){
        m_overflow = true;
    } else if (*m_sp == 0x00 && m_overflow){
        printf("ERROR: stack overflow\n");
        //TODO: reset();
    }
    m_stacked_values[(*m_sp)++] = value;
}

uint16_t Stack::pop(){
    if (*m_sp == 0x00 && !m_overflow){
        printf("ERROR: stack underflow\n");
        //TODO: reset();
    } else {
        m_overflow = false;
    }
    return m_stacked_values[--(*m_sp)];
}

void Stack::clean(){
    *m_sp = 0;
    m_overflow = false;
}

const device_type ANOTHER_WORLD  = &device_creator<another_world_cpu_device>;

another_world_cpu_device::another_world_cpu_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
    : cpu_device(mconfig, ANOTHER_WORLD, "ANOTHER WORLD", tag, owner, clock, "another_world_cpu", __FILE__),
      m_program_config("program", ENDIANNESS_LITTLE, 8, 16, 0),
      m_data_config("data", ENDIANNESS_LITTLE, 16, 9, 0),
      m_icount(0)
{
    m_stack = new Stack(&m_sp);
    m_requestedNextPart = 0;
}

another_world_cpu_device::~another_world_cpu_device() {
    delete m_stack;
}

void another_world_cpu_device::nextThread(){
    do {
        m_currentThread = (m_currentThread+1) % NUM_THREADS;

        if (m_currentThread == 0){
            //Check if a part switch has been requested.
            if (m_requestedNextPart != 0) {
                initForPart(m_requestedNextPart);
                m_requestedNextPart = 0;
            }

            for (int i=0; i<NUM_THREADS; i++){
                m_thread_state[i] = m_requested_state[i];

                uint16_t req = m_requested_PC[i];
                if (req != NO_REQUEST){
                    m_requested_PC[i] = NO_REQUEST;
                    if (req == DELETE_THIS_THREAD){
                        m_thread_PC[i] = INACTIVE_THREAD;
                    } else {
                        m_thread_PC[i] = req;
                    }
                }
            }
            ((another_world_state*) owner())->updateDisplay(0xFE);
        }
    } while(m_thread_state[m_currentThread] == FROZEN ||
            m_thread_PC[m_currentThread] == INACTIVE_THREAD ||
            m_thread_PC[m_currentThread] == DELETE_THIS_THREAD);

    PC = m_thread_PC[m_currentThread];
}

uint16_t another_world_cpu_device::read_vm_variable(uint8_t i){
    return m_data->read_word(2*i);
}

void another_world_cpu_device::write_vm_variable(uint8_t i, uint16_t value){
    m_data->write_word(2*i, value);
}

uint8_t another_world_cpu_device::fetch_byte(){
    uint8_t value = READ_BYTE_AW(PC);
    INCREMENT_PC_64K;
    return value;
}

uint16_t another_world_cpu_device::fetch_word(){
    uint16_t value = READ_WORD_AW(PC);
    INCREMENT_PC_64K;
    INCREMENT_PC_64K;
    return value;
}

void another_world_cpu_device::device_start()
{
    m_program = &space(AS_PROGRAM);
    m_data = &space(AS_DATA);

    save_item(NAME(m_pc));
    save_item(NAME(m_sp));

    // Register state for debugger
    state_add( ANOTHER_WORLD_PC,         "PC",          m_pc            ).mask(0xFFF);
    state_add( ANOTHER_WORLD_SP,         "SP",          m_sp            ).mask(0xFF);
    state_add( ANOTHER_WORLD_CUR_THREAD, "CUR_THREAD",  m_currentThread ).mask(0xFF);
    state_add( STATE_GENPC,              "GENPC",       m_pc ).formatstr("0%06O").noshow();

    m_icountptr = &m_icount;
}

void another_world_cpu_device::device_reset()
{
    m_stack->clean();
    m_currentThread = 0;
    m_currentPartId = GAME_PART(0);
    m_pc = 0;
    m_sp = 0;

#ifdef DUMP_VM_EXECUTION_LOG
    m_address_log = fopen("address_log.txt", "w");
    fprintf(m_address_log, "begin log\n");
#endif

    //TODO: declare the stack as a RAM block so that
    //      we can inspect it in the Memory View Window.
    m_stack->clean();

    //all threads are initially disabled and not frozen
    for (int i=0; i<NUM_THREADS; i++){
        m_thread_PC[i] = INACTIVE_THREAD;
        m_requested_PC[i] = INACTIVE_THREAD;
        m_thread_state[i] = UNFROZEN;
        m_requested_state[i] = UNFROZEN;
    }

    write_vm_variable(0x54, 0x0081); //TODO: figure out why this is supposedly needed.
    write_vm_variable(VM_VARIABLE_RANDOM_SEED, time(0));
}

/* execute instructions on this CPU until icount expires */
void another_world_cpu_device::execute_run()
{
    do
    {
        execute_instruction();
    }
    while (m_icount > 0);
}

/* execute one instruction */
void another_world_cpu_device::execute_instruction()
{
    debugger_instruction_hook(this, PC);

#ifdef DUMP_VM_EXECUTION_LOG
    int pcounter = PC;
    unsigned char opcode = fetch_byte();
    fprintf(m_address_log, "[%X]: %X\n", pcounter, opcode);
#else
    unsigned char opcode = fetch_byte();
#endif

    if (opcode & 0x80) 
    {
        uint16_t offset = ((opcode << 8) | fetch_byte()) * 2;

        m_useVideo2 = false;
        int16_t x = fetch_byte();
        int16_t y = fetch_byte();
        int16_t h = y - 199;
        if (h > 0) {
            y = 199;
            x += h;
        }
//        printf("vid_opcd_0x80 : opcode=0x%X off=0x%X x=%d y=%d\n", opcode, offset, x, y);

        // This switch the polygon database to "cinematic" and probably draws a black polygon
        // over all the screen.
        ((another_world_state*) owner())->setDataBuffer(CINEMATIC, offset);
        ((another_world_state*) owner())->readAndDrawPolygon(COLOR_BLACK, DEFAULT_ZOOM, Point(x,y));

        return;
    } 

    if (opcode & 0x40) 
    {
        int16_t x, y;
        uint16_t offset = fetch_word() * 2;
        x = fetch_byte();

        m_useVideo2 = false;

        if (!(opcode & 0x20)) 
        {
            if (!(opcode & 0x10))
            {
                x = (x << 8) | fetch_byte();
            } else {
                x = read_vm_variable(x);
            }
        } 
        else 
        {
            if (opcode & 0x10) {
                x += 0x100;
            }
        }

        y = fetch_byte();

        if (!(opcode & 8))
        {
            if (!(opcode & 4)) {
                y = (y << 8) | fetch_byte();
            } else {
                y = read_vm_variable(y);
            }
        }

        uint16_t zoom = 0x40;
        if (opcode & 1)
        {
            if (opcode & 2)
                m_useVideo2 = true;
            else
                zoom = read_vm_variable(fetch_byte());
        }
//        printf("vid_opcd_0x40 : off=0x%X x=%d y=%d\n", offset, x, y);

        ((another_world_state*) owner())->setDataBuffer(m_useVideo2 ? VIDEO_2 : CINEMATIC, offset);
        ((another_world_state*) owner())->readAndDrawPolygon(0xFF, zoom, Point(x, y));
        return;
    }
    
    
    switch (opcode){
        case 0x00: /* movConst */
        {
            uint8_t variableId = fetch_byte();
            int16_t value = fetch_word();
            write_vm_variable(variableId, value);
            return;
        }
        case 0x01: /* mov */
        {
            uint8_t dstVariableId = fetch_byte();
            uint8_t srcVariableId = fetch_byte(); 
            uint16_t value = read_vm_variable(srcVariableId);
            write_vm_variable(dstVariableId, value);
            return;
        }
        case 0x02: /* add */
        {
            uint8_t dstVariableId = fetch_byte();
            uint8_t srcVariableId = fetch_byte();
            uint16_t result = read_vm_variable(dstVariableId);
            result += read_vm_variable(srcVariableId);
            write_vm_variable(dstVariableId, result);
            return;
        }
        case 0x03: /* addConst */
        {
            /* TODO: Investigate this:
            if (res->currentPartId == 0x3E86 && _scriptPtr.pc == res->segBytecode + 0x6D48) {
                warning("VirtualMachine::op_addConst() hack for non-stop looping gun sound bug");
                // the script 0x27 slot 0x17 doesn't stop the gun sound from looping, I 
                // don't really know why ; for now, let's play the 'stopping sound' like 
                // the other scripts do
                //  (0x6D43) jmp(0x6CE5)
                //  (0x6D46) break
                //  (0x6D47) VAR(6) += -50
                snd_playSound(0x5B, 1, 64, 1);
            }*/
            uint8_t variableId = fetch_byte();
            int16_t value = fetch_word();
            int16_t result = read_vm_variable(variableId) + value;
            write_vm_variable(variableId, result);
            return;
        }
        case 0x04: /* CALL subroutine instruction */
        {
            UINT16 addr = fetch_word();
            m_stack->push(PC);
            PC = addr;
            return;
        }
        case 0x05: /* ret: return from subroutine */
        {
            PC = m_stack->pop();
            return;
        }
        case 0x06: /* pauseThread instruction (a.k.a. "break") */
        {
            if (m_requested_PC[m_currentThread] == NO_REQUEST)
                m_requested_PC[m_currentThread] = PC;

            nextThread();
            return;
        }
        case 0x07: /* jmp to address */
        {
            PC = fetch_word();
            return;
        }
        case 0x08: /* setVect instruction */
        {
            uint8_t threadId = fetch_byte();
            uint16_t request = fetch_word();
            m_requested_PC[threadId] = request;
            return;
        }
        case 0x09: /* DJNZ instrucion:
                      'D'ecrement variable value and 'J'ump if 'N'ot 'Z'ero  */
        {
            uint8_t i = fetch_byte();
            uint16_t value = read_vm_variable(i);
            value--;
            write_vm_variable(i, value);
            uint16_t address = fetch_word();
            if (value != 0) {
                PC = address;
            }
            return;
        }
        case 0x0A: /* condJmp */
        {

//TODO: Check the validity of the following bytecode hack:
#if 0 //BYPASS_PROTECTION
            //FCS Whoever wrote this is patching the bytecode on the fly. This is ballzy !!
                    
            if (res->currentPartId == GAME_PART_FIRST && _scriptPtr.pc == res->segBytecode + 0xCB9) {
                
                // (0x0CB8) condJmp(0x80, VAR(41), VAR(30), 0xCD3)
                *(_scriptPtr.pc + 0x00) = 0x81;
                *(_scriptPtr.pc + 0x03) = 0x0D;
                *(_scriptPtr.pc + 0x04) = 0x24;
                // (0x0D4E) condJmp(0x4, VAR(50), 6, 0xDBC)     
                *(_scriptPtr.pc + 0x99) = 0x0D;
                *(_scriptPtr.pc + 0x9A) = 0x5A;
                printf("VirtualMachine::op_condJmp() bypassing protection");
                printf("bytecode has been patched/n");
                
                //this->bypassProtection() ;
            }    
#endif

            uint8_t subopcode = fetch_byte();
            int16_t b = read_vm_variable(fetch_byte());
            uint8_t c = fetch_byte();
            int16_t a;

            if (subopcode & 0x80) {
                a = read_vm_variable(c);
            } else if (subopcode & 0x40) {
                a = c << 8 | fetch_byte();
            } else {
                a = c;
            }

            // Check if the conditional value is met.
            bool expr = false;
            switch (subopcode & 7) {
            case 0: // jz
                expr = (b == a);
                break;
            case 1: // jnz
                expr = (b != a);
                break;
            case 2: // jg
                expr = (b > a);
                break;
            case 3: // jge
                expr = (b >= a);
                break;
            case 4: // jl
                expr = (b < a);
                break;
            case 5: // jle
                expr = (b <= a);
                break;
            default:
                printf("ERROR: conditional jump: invalid condition (%d)\n", (subopcode & 7));
                break;
            }

            //printf("subopcode:0x%02X b:0x%04X c:0x%02X a:0x%04X\n", subopcode, b, c, a);

            uint16_t offset = fetch_word();
            //printf("offset: 0x%04X\n", offset);
            if (expr) {
                PC = offset;
            }
            return;
        }
        case 0x0B: /* setPalette */
        {
            uint16_t paletteId = fetch_word();
            ((another_world_state*) owner())->changePalette((uint8_t ) (paletteId >> 8));
            return;
        }
        case 0x0C: /* resetThread */
        {
            uint8_t first = fetch_byte();
            uint8_t last = fetch_byte();
            uint8_t type = fetch_byte();

            //Make sure last is within [0, NUM_THREADS-1]
            last = last % NUM_THREADS ;
            int8_t n = last - first;
            
            if (n < 0) {
                printf("ERROR: resetThread with n < 0.\n");
                return;
            }

            enum {
                RESET_TYPE__FREEZE_CHANNELS=0,
                RESET_TYPE__UNFREEZE_CHANNELS,
                RESET_TYPE__DELETE_CHANNELS,
            };

            switch(type){
                case RESET_TYPE__FREEZE_CHANNELS:
                    //printf("freezeChannels (first:%d, last:%d)\n", first, last);
                    for (int i=first; i<=last; i++){
                        m_requested_state[i] = FROZEN;
                    }
                    break;
                case RESET_TYPE__UNFREEZE_CHANNELS:
                    //printf("unfreezeChannels (first:%d, last:%d)\n", first, last);
                    for (int i=first; i<=last; i++){
                        m_requested_state[i] = UNFROZEN;
                    }
                    break;
                case RESET_TYPE__DELETE_CHANNELS:
                    //printf("deleteChannels (first:%d, last:%d)\n", first, last);
                    for (int i=first; i<=last; i++){
                        m_requested_PC[i] = DELETE_THIS_THREAD;
                    }
                    break;
                default:
                    printf("ERROR: invalid resetThread operation type (%d).\n", type);
            }
            return;
        }
        case 0x0D: /* selectVideoPage */
        {
            uint8_t frameBufferId = fetch_byte();
            ((another_world_state*) owner())->selectVideoPage(frameBufferId);
            return;
        }
        case 0x0E: /* fillVideoPage */
        {
            uint8_t pageId = fetch_byte();
            uint8_t color = fetch_byte();
            ((another_world_state*) owner())->fillPage(pageId, color);
            return;
        }
        case 0x0F: /* copyVideoPage */
        {
            uint8_t srcPageId = fetch_byte();
            uint8_t dstPageId = fetch_byte();
            ((another_world_state*) owner())->copyVideoPage(srcPageId, dstPageId, read_vm_variable(VM_VARIABLE_SCROLL_Y));
            return;
        }
        case 0x10: /* blitFramebuffer */
        {
            uint8_t pageId = fetch_byte();
            //printf("blitFramebuffer(pageId:%d, PAUSE_SLICES:%d)\n", pageId, read_vm_variable(VM_VARIABLE_PAUSE_SLICES));
#if 1
            //Nasty hack....was this present in the original assembly  ??!!
            //This seems to be necessary for switching from the intro sequence to the lake stage
            if (m_currentPartId == GAME_PART(0) && read_vm_variable(0x67) == 1)
                write_vm_variable(0xDC, 0x21);
#endif
            // The bytecode will set vmVariables[VM_VARIABLE_PAUSE_SLICES] from 1 to 5
            // The virtual machine hence indicate how long the image should be displayed.

#ifdef SPEEDUP_VM_EXECUTION
            m_icount --;
#else
            m_icount -= (int) read_vm_variable(VM_VARIABLE_PAUSE_SLICES);
#endif

            //Why ?
            write_vm_variable(0xF7, 0x0000);

            ((another_world_state*) owner())->updateDisplay(pageId);
            return;
        }
        case 0x11: /* killThread */
        {
            m_thread_PC[m_currentThread] = INACTIVE_THREAD;
            nextThread();
            return;
        }
        case 0x12: /* drawString */
        {
            uint16_t stringId = fetch_word();
            uint16_t x = fetch_byte();
            uint16_t y = fetch_byte();
            uint16_t color = fetch_byte();

            ((another_world_state*) owner())->draw_string(stringId, x, y, color);
            return;
        }
        case 0x13: /* sub */
        {
            uint8_t i = fetch_byte();
            uint8_t j = fetch_byte();
            uint16_t value = read_vm_variable(i);
            value -= read_vm_variable(j);
            write_vm_variable(i, value);
            return;
        }
        case 0x14: /* and */
        {
            uint8_t variableId = fetch_byte();
            uint16_t value = fetch_word();
            uint16_t result = read_vm_variable(variableId) & value;
            write_vm_variable(variableId, result);
            return;
        }
        case 0x15: /* or */
        {
            uint8_t variableId = fetch_byte();
            uint16_t value = fetch_word();
            uint16_t result = read_vm_variable(variableId) | value;
            write_vm_variable(variableId, result);
            return;
        }
        case 0x16: /* shl */
        {
            uint8_t variableId = fetch_byte();
            uint16_t leftShiftValue = fetch_word();
            uint16_t result = read_vm_variable(variableId) << leftShiftValue;
            write_vm_variable(variableId, result);
            return;
        }
        case 0x17: /* shr */
        {
            uint8_t variableId = fetch_byte();
            uint16_t rightShiftValue = fetch_word();
            uint16_t result = read_vm_variable(variableId) >> rightShiftValue;
            write_vm_variable(variableId, result);
            return;
        }
        case 0x18: /* play (a.k.a. "playSound") */
        {
            uint16_t resourceId = fetch_word();
            uint8_t freq = fetch_byte();
            uint8_t vol = fetch_byte();
            uint8_t channel = fetch_byte();
            ((another_world_state*) owner())->m_mixer->playSound(channel, resourceId, freq, vol);
            return;
        }
        case 0x19: /* load (a.k.a. "updateMemList") */
        {
            uint16_t resourceId = fetch_word();
            printf("load (a.k.a. \"updateMemList\") (0x%02X)\n", resourceId);

            if (resourceId == 0) {
                ((another_world_state*) owner())->m_mixer->m_player->stop();
                ((another_world_state*) owner())->m_mixer->stopAll();
                //res->invalidateRes();
                return;
            }

            #define NUM_MEM_LIST 0x91 /* TODO: check this! */
            if (resourceId > NUM_MEM_LIST) {
                m_requestedNextPart = resourceId;
            }
            return;
        }
        case 0x1A: /* playMusic */
        {
            uint16_t resNum = fetch_word();
            uint16_t delay = fetch_word();
            uint8_t pos = fetch_byte();
            
            if (resNum != 0) {
                ((another_world_state*) owner())->m_mixer->m_player->loadSfxModule(resNum, delay, pos);
                ((another_world_state*) owner())->m_mixer->m_player->start();
            } else if (delay != 0) {
                ((another_world_state*) owner())->m_mixer->m_player->setEventsDelay(delay);
            } else {
                ((another_world_state*) owner())->m_mixer->m_player->stop();
            }
            return;
        }
    }
    printf("unimplemented opcode: 0x%02X\n", opcode);
}

void another_world_cpu_device::initForPart(uint16_t partId){
    ((another_world_state*) owner())->m_mixer->m_player->stop();
    ((another_world_state*) owner())->m_mixer->stopAll();

    write_vm_variable(0xE4, 0x14); //why?

    ((another_world_state*) owner())->setupPart(partId);
    m_currentPartId = partId;

#ifdef DUMP_VM_EXECUTION_LOG
    fprintf(m_address_log, "initForPart %d\n", partId);
#endif

    for (int i=0; i<NUM_THREADS; i++){
        m_thread_PC[i] = INACTIVE_THREAD;
        m_thread_state[i] = UNFROZEN;
    }

    m_thread_PC[0] = 0x0000;
}

offs_t another_world_cpu_device::disasm_disassemble(char *buffer, offs_t pc, const UINT8 *oprom, const UINT8 *opram, UINT32 options)
{
    extern CPU_DISASSEMBLE( another_world );
    return CPU_DISASSEMBLE_NAME(another_world)(this, buffer, pc, oprom, opram, options);
}

