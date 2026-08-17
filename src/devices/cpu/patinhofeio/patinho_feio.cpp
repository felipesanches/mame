// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/*
    CPU emulation for Patinho Feio, the first computer designed and manufactured in Brazil
*/

#include "emu.h"
#include "patinhofeio_cpu.h"
#include "patinho_feio_dasm.h"

#define PC       m_pc //The program counter is called "contador de instrucoes" (IC) in portuguese
#define ACC      m_acc
#define EXT      m_ext
#define RC       read_panel_keys_register()
#define FLAGS    m_flags

#define V 0x01 // V = "Vai um" (Carry)
#define T 0x02 // T = "Transbordo" (Overflow)

#define READ_BYTE_PATINHO(A) (m_program->read_byte(A))
#define WRITE_BYTE_PATINHO(A,V) (m_program->write_byte(A,V))

#define READ_WORD_PATINHO(A) (READ_BYTE_PATINHO(A+1)*256 + READ_BYTE_PATINHO(A))

#define READ_INDEX_REG() READ_BYTE_PATINHO(0x000)
#define WRITE_INDEX_REG(V) { WRITE_BYTE_PATINHO(0x000, V); m_idx = V; }

#define READ_ACC_EXTENSION_REG() READ_BYTE_PATINHO(0x001)
#define WRITE_ACC_EXTENSION_REG(V) { WRITE_BYTE_PATINHO(0x001, V); m_ext = V; }

#define ADDRESS_MASK_4K    0xFFF
#define INCREMENT_PC_4K    (PC = (PC+1) & ADDRESS_MASK_4K)

/* The "instrucoes curtas do grupo 2" (0x90-0x97) skip the *whole* next
   instruction, which may well be a two-word long instruction. The July 1977
   assembler manual is explicit about it, both in the chapter opening
   ("Podem resultar em saltos (CI <- CI + 2)") and in a note on the sample
   program ("Note que apos ST 0 e SV 1 ha instrucoes longas PLA FTP e PLA ROT
   que ocupam duas palavras -- estas duas palavras serao saltadas quando a
   condicao do salto for satisfeita").
   Skipping a single word lands in the middle of a long instruction and
   executes its operand as an opcode. */
#define SKIP_NEXT_INSTRUCTION  { INCREMENT_PC_4K; INCREMENT_PC_4K; }

/* Chapter 13 describes the I/O skips as "salta duas palavras se ...". That is
   a different rule from the group-2 one above, even though on this machine
   the two happen to move the counter by the same amount: one skips a fixed
   number of words, the other skips whatever the next instruction is. Keeping
   them apart means a future long instruction cannot silently break one of
   them. */
#define SKIP_TWO_WORDS  { INCREMENT_PC_4K; INCREMENT_PC_4K; }

/* Both flags are defined in chapter 2 of the July 1977 assembler manual.

   V ("vai-um", carry): "o vai-um na ultima soma realizada (bit mais
   significativo)" -- the carry out of bit 7.

   T ("transbordo", overflow): chapter 3 says it is "modificado, por exemplo,
   cada vez que e realizada uma adicao. Se houver transbordo na adicao, entao
   e feito T = 1 e isto indica que o resultado (contido no ACC), esta errado."
   Chapter 2 illustrates it with 60 + 70 = 130, which does not fit in the
   representable range of a signed byte. That is signed overflow: it happens
   when both operands share a sign and the result has the opposite one. */
void patinho_feio_cpu_device::update_addition_flags(uint8_t operand_a, uint8_t operand_b){
	uint16_t const result = operand_a + operand_b;

	set_flag(V, result > 0xFF);
	set_flag(T, BIT((operand_a ^ result) & (operand_b ^ result), 7));
}

void patinho_feio_cpu_device::set_flag(uint8_t flag, bool state){
	if (state){
		FLAGS |= flag;
	} else {
		FLAGS &= ~flag;
	}
}

void patinho_feio_cpu_device::compute_effective_address(unsigned int addr){
	m_addr = addr;
	if (m_indirect_addressing){
		m_addr = READ_WORD_PATINHO(m_addr);
		if (m_addr & 0x1000)
			compute_effective_address(m_addr & 0xFFF);
	}
}

DEFINE_DEVICE_TYPE(PATO_FEIO_CPU, patinho_feio_cpu_device, "pato_feio_cpu", "Patinho Feio CPU")

//Internal 4kbytes of RAM
void patinho_feio_cpu_device::prog_8bit(address_map &map)
{
	map(0x0000, 0x0fff).ram().share("internalram");
}

patinho_feio_cpu_device::patinho_feio_cpu_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: cpu_device(mconfig, PATO_FEIO_CPU, tag, owner, clock)
	, m_program_config("program", ENDIANNESS_LITTLE, 8, 12, 0, address_map_constructor(FUNC(patinho_feio_cpu_device::prog_8bit), this))
	, m_update_panel_cb(*this)
	, m_icount(0)
	, m_rc_read_cb(*this, 0)
	, m_buttons_read_cb(*this, 0)
	, m_io_func_cb(*this)
	, m_io_data_r_cb(*this, 0)
	, m_io_data_w_cb(*this)
	, m_io_skip_cb(*this, 0)
{
}

device_memory_interface::space_config_vector patinho_feio_cpu_device::memory_space_config() const
{
	return space_config_vector {
		std::make_pair(AS_PROGRAM, &m_program_config)
	};
}

uint16_t patinho_feio_cpu_device::read_panel_keys_register(){
	m_rc = m_rc_read_cb(0);

	return m_rc;
}

void patinho_feio_cpu_device::device_start()
{
	m_program = &space(AS_PROGRAM);
	m_update_panel_cb.resolve_safe();

//TODO: implement handling of these special purpose registers
//      which are also mapped to the first few main memory positions:
//
//      ERI: "Endereco de Retorno de Interrupcao"
//           "Interrupt Return Address"
//           stored at addresses 002 and 003
//
//      ETI: "inicio de uma rotina de tratamento de interrupcao (se houver)"
//           "start of an interrupt service routine (if any)"
//           stored at address 004 (and 005 as well?)
//
// It seems that the general purpose memory starts at address 006.

	save_item(NAME(m_pc));
	save_item(NAME(m_acc));
	save_item(NAME(m_ext));
	save_item(NAME(m_rc));
	save_item(NAME(m_idx));
	save_item(NAME(m_flags));
	save_item(NAME(m_addr));
	save_item(NAME(m_opcode));
	save_item(NAME(m_run));
	save_item(NAME(m_wait_for_interrupt));
	save_item(NAME(m_interrupts_enabled));
	save_item(NAME(m_scheduled_IND_bit_reset));
	save_item(NAME(m_indirect_addressing));
	save_item(NAME(m_mode));

	// Register state for debugger
	state_add( PATINHO_FEIO_CI,         "CI",       m_pc         ).mask(0xFFF);
	state_add( PATINHO_FEIO_RC,         "RC",       m_rc         ).mask(0xFFF);
	state_add( PATINHO_FEIO_ACC,        "ACC",      m_acc        ).mask(0xFF);
	state_add( PATINHO_FEIO_EXT,        "EXT",      m_ext        ).mask(0xFF);
	state_add( PATINHO_FEIO_IDX,        "IDX",      m_idx        ).mask(0xFF);
	state_add(STATE_GENPC, "GENPC", m_pc).formatstr("0%06O").noshow();
	state_add(STATE_GENPCBASE, "CURPC", m_pc).formatstr("0%06O").noshow();
	state_add(STATE_GENFLAGS,  "GENFLAGS",  m_flags).noshow().formatstr("%8s");

	set_icountptr(m_icount);
}

void patinho_feio_cpu_device::device_reset()
{
	m_pc = 0;
	//m_pc = 0x006; //"PATINHO FEIO" hello-world
	//m_pc = 0x010; //micro-pre-loader
	//m_pc = 0xE00; //HEXAM
	m_rc = 0;
	m_acc = 0;
	m_ext = READ_ACC_EXTENSION_REG();
	m_idx = READ_INDEX_REG();
	m_flags = 0;
	m_run = false;
	m_wait_for_interrupt = false;
	m_interrupts_enabled = false;
	m_scheduled_IND_bit_reset = false;
	m_indirect_addressing = false;
	m_addr = 0;
	m_opcode = 0;
	m_mode = ADDRESSING_MODE;

	m_update_panel_cb(ACC, m_opcode, READ_BYTE_PATINHO(m_addr), m_addr, PC, FLAGS, RC, m_mode);
}

/* execute instructions on this CPU until icount expires */
void patinho_feio_cpu_device::execute_run() {
	do {
		read_panel_keys_register();
		m_ext = READ_ACC_EXTENSION_REG();
		m_idx = READ_INDEX_REG();
		m_update_panel_cb(ACC, READ_BYTE_PATINHO(PC), READ_BYTE_PATINHO(m_addr), m_addr, PC, FLAGS, RC, m_mode);

		if (!m_run){
			debugger_wait_hook();
			if (!m_buttons_read_cb.isunset()){
				uint16_t buttons = m_buttons_read_cb(0);
				if (buttons & BUTTON_PARTIDA){
					/* "startup" button */
					switch (m_mode){
						case ADDRESSING_MODE: PC = RC; break;
						case NORMAL_MODE: m_run = true; break;
						case DATA_STORE_MODE: WRITE_BYTE_PATINHO(PC, RC & 0xFF); break; //TODO: we also need RE (address register, instead of using PC directly)
						/*TODO: case DATA_VIEW_MODE: RD = READ_BYTE_PATINHO(RC); break; //we need to implement RD (the 'data register') */
						default: break;
					}
				}
				if (buttons & BUTTON_NORMAL) m_mode = NORMAL_MODE;
				if (buttons & BUTTON_ENDERECAMENTO) m_mode = ADDRESSING_MODE;
				if (buttons & BUTTON_EXPOSICAO) m_mode = DATA_VIEW_MODE;
				if (buttons & BUTTON_ARMAZENAMENTO) m_mode = DATA_STORE_MODE;
				if (buttons & BUTTON_CICLO_UNICO) m_mode = CYCLE_STEP_MODE;
				if (buttons & BUTTON_INSTRUCAO_UNICA) m_mode = INSTRUCTION_STEP_MODE;
				if (buttons & BUTTON_PREPARACAO) device_reset();
			}
			m_icount = 0;   /* if processor is stopped, just burn cycles */
		} else {
			debugger_instruction_hook(PC);
			execute_instruction();
			m_icount --;
		}
	}
	while (m_icount > 0);
}

/* execute one instruction */
void patinho_feio_cpu_device::execute_instruction()
{
	unsigned int tmp;
	unsigned char value;
	m_opcode = READ_BYTE_PATINHO(PC);
	INCREMENT_PC_4K;

	if (m_scheduled_IND_bit_reset)
		m_indirect_addressing = false;

	if (m_indirect_addressing)
		m_scheduled_IND_bit_reset = true;

	switch (m_opcode){
		case 0xD2:
			//XOR: Computes the bitwise XOR of an immediate into the accumulator
			ACC ^= READ_BYTE_PATINHO(PC);
			INCREMENT_PC_4K;
			//TODO: update T and V flags
			return;
		case 0xD4:
			//NAND: Computes the bitwise XOR of an immediate into the accumulator
			ACC = ~(ACC & READ_BYTE_PATINHO(PC));
			INCREMENT_PC_4K;
			//TODO: update T and V flags
			return;
		case 0xD8:
			//SOMI="Soma Imediato":
			//     Add an immediate into the accumulator
			value = READ_BYTE_PATINHO(PC);
			update_addition_flags(ACC, value);
			ACC += value;
			INCREMENT_PC_4K;
			return;
		case 0xDA:
			//CARI="Carrega Imediato":
			//     Load an immediate into the accumulator
			ACC = READ_BYTE_PATINHO(PC);
			INCREMENT_PC_4K;
			return;
		case 0x80:
			//LIMPO:
			//    Clear accumulator and flags
			ACC = 0;
			FLAGS = 0;
			return;
		case 0x81:
			//UM="One":
			//    Load 1 into accumulator
			//    and clear the flags
			ACC = 1;
			FLAGS = 0;
			return;
		case 0x82:
			//CMP1:
			// Compute One's complement of the accumulator
			//    and clear the flags
			ACC = ~ACC;
			FLAGS = 0;
			return;
		case 0x83:
			//CMP2:
			// Compute Two's complement of the accumulator
			//    and updates flags according to the result of the operation
			ACC = ~ACC + 1;
			// FIXME: I'm not sure yet how to compute the flags here
			FLAGS = 0;
			return;
		case 0x84:
			//LIM="Limpa":
			// Clear flags
			FLAGS = 0;
			return;
		case 0x85:
			//INC:
			// Increment accumulator
			ACC++;
			// FIXME: I'm not sure yet how to compute the flags here
			FLAGS = 0;
			return;
		case 0x86:
			//UNEG="Um Negativo":
			// Load -1 into accumulator and clear flags
			ACC = -1;
			FLAGS = 0;
			return;
		case 0x87:
			//LIMP1:
			//    Clear accumulator, reset T and set V
			ACC = 0;
			FLAGS = V;
			return;
		case 0x88:
			//PNL 0:
			ACC = (RC & 0xFF);
			FLAGS = 0;
			return;
		case 0x89:
			//PNL 1:
			ACC = (RC & 0xFF) + 1;
			//TODO: FLAGS = ?;
			return;
		case 0x8A:
			//PNL 2:
			ACC = (RC & 0xFF) - ACC - 1;
			//TODO: FLAGS = ?;
			return;
		case 0x8B:
			//PNL 3:
			ACC = (RC & 0xFF) - ACC;
			//TODO: FLAGS = ?;
			return;
		case 0x8C:
			//PNL 4:
			ACC = (RC & 0xFF) + ACC;
			//TODO: FLAGS = ?;
			return;
		case 0x8D:
			//PNL 5:
			ACC = (RC & 0xFF) + ACC + 1;
			//TODO: FLAGS = ?;
			return;
		case 0x8E:
			//PNL 6:
			ACC = (RC & 0xFF) - 1;
			//TODO: FLAGS = ?;
			return;
		case 0x8F:
			//PNL 7:
			ACC = (RC & 0xFF);
			FLAGS = V;
			return;
		case 0x90:
			//ST 0 = "Se T=0, Pula"
			//       If T is zero, skip the next instruction
			if ((FLAGS & T) == 0)
				SKIP_NEXT_INSTRUCTION; //skip the whole next instruction
			return;
		case 0x91:
			//STM 0 = "Se T=0, Pula e muda"
			//        If T is zero, skip the next instruction
			//        and toggle T.
			if ((FLAGS & T) == 0){
				SKIP_NEXT_INSTRUCTION; //skip the whole next instruction
				FLAGS |= T; //set T=1
			}
			return;
		case 0x92:
			//ST 1 = "Se T=1, Pula"
			//       If T is one, skip the next instruction
			if ((FLAGS & T) == T)
				SKIP_NEXT_INSTRUCTION; //skip the whole next instruction
			return;
		case 0x93:
			//STM 1 = "Se T=1, Pula e muda"
			//        If T is one, skip the next instruction
			//        and toggle T.
			if ((FLAGS & T) == T){
				SKIP_NEXT_INSTRUCTION; //skip the whole next instruction
				FLAGS &= ~T; //set T=0
			}
			return;
		case 0x94:
			//SV 0 = "Se V=0, Pula"
			//       If V is zero, skip the next instruction
			if ((FLAGS & V) == 0)
				SKIP_NEXT_INSTRUCTION; //skip the whole next instruction
			return;
		case 0x95:
			//SVM 0 = "Se V=0, Pula e muda"
			//        If V is zero, skip the next instruction
			//        and toggle V.
			if ((FLAGS & V) == 0){
				SKIP_NEXT_INSTRUCTION; //skip the whole next instruction
				FLAGS |= V; //set V=1
			}
			return;
		case 0x96:
			//SV 1 = "Se V=1, Pula"
			//       If V is one, skip the next instruction
			if ((FLAGS & V) == V)
				SKIP_NEXT_INSTRUCTION; //skip the whole next instruction
			return;
		case 0x97:
			//SVM 1 = "Se V=1, Pula e muda"
			//        If V is one, skip the next instruction
			//        and toggle V.
			if ((FLAGS & V) == V){
				SKIP_NEXT_INSTRUCTION; //skip the whole next instruction
				FLAGS &= ~V; //set V=0
			}
			return;
		case 0x98:
			//PUL="Pula para /002 a limpa estado de interrupcao"
			//     Jump to address /002 and disables interrupts
			PC = 0x002;
			m_interrupts_enabled = false;
			return;
		case 0x99:
			//TRE="Troca conteudos de ACC e EXT"
			//     Exchange the value of the accumulator with the ACC extension register
			value = ACC;
			ACC = READ_ACC_EXTENSION_REG();
			WRITE_ACC_EXTENSION_REG(value);
			return;
		case 0x9A:
			//INIB="Inibe"
			//     disables interrupts
			m_interrupts_enabled = false;
			return;
		case 0x9B:
			//PERM="Permite"
			//     enables interrupts
			m_interrupts_enabled = true;
			return;
		case 0x9C:
			//ESP="Espera":
			//    Holds execution and waits for an interrupt to occur.
			m_run = false;
			m_wait_for_interrupt = true;
			return;
		case 0x9D:
			//PARE="Pare":
			//    Holds execution. This can only be recovered by
			//    manually triggering execution again by
			//    pressing the "Partida" (start) button in the panel
			m_run = false;
			m_wait_for_interrupt = false;
			return;
		case 0x9E:
			//TRI="Troca com Indexador":
			//     Exchange the value of the accumulator with the index register
			value = ACC;
			ACC = READ_INDEX_REG();
			WRITE_INDEX_REG(value);
			return;
		case 0x9F:
			//IND="Enderecamento indireto":
			//     Sets memory addressing for the next instruction to be indirect.
			m_indirect_addressing = true;
			m_scheduled_IND_bit_reset = false; //the next instruction execution will schedule it.
			return;
		case 0xD1:
			//Bit-Shift/Bit-Rotate instructions
			value = READ_BYTE_PATINHO(PC);
			INCREMENT_PC_4K;
			for (int i=0; i<4; i++){
				if (value & (1<<i)){
					/* The number of shifts or rotations is determined by the
					   ammount of 1 bits in the lower 4 bits of 'value' */
					switch(value & 0xF0)
					{
						case 0x00:
							//DD="Deslocamento para a Direita"
							//    Shift right
							FLAGS &= ~V;
							if (ACC & 1)
								FLAGS |= V;

							ACC >>= 1;
							break;
						case 0x20:
							//GD="Giro para a Direita"
							//    Rotate right
							FLAGS &= ~V;
							if (ACC & 1)
								FLAGS |= V;

							ACC = ((ACC & 1) << 7) | (ACC >> 1);
							break;
						case 0x10: //DDV="Deslocamento para a Direita com Vai-um"
								//     Shift right with Carry
						case 0x30: //GDV="Giro para a Direita com Vai-um"
								//     Rotate right with Carry

							//both instructions are equivalent
							if (FLAGS & V)
								tmp = 0x100 | ACC;
							else
								tmp = ACC;

							FLAGS &= ~V;
							if (ACC & 1)
								FLAGS |= V;

							ACC = tmp >> 1;
							break;
						case 0x40: //DE="Deslocamento para a Esquerda"
								//    Shift left
							FLAGS &= ~V;
							if (ACC & (1<<7))
								FLAGS |= V;

							ACC <<= 1;
							break;
						case 0x60: //GE="Giro para a Esquerda"
								//    Rotate left
							FLAGS &= ~V;
							if (ACC & (1<<7))
								FLAGS |= V;

							ACC = (ACC << 1) | ((ACC >> 7) & 1);
							break;
						case 0x50: //DEV="Deslocamento para a Esquerda com Vai-um"
								//     Shift left with Carry
						case 0x70: //GEV="Giro para a Esquerda com Vai-um"
								//     Rotate left with Carry

							//both instructions are equivalent
							if (FLAGS & V)
								tmp = (ACC << 1) | 1;
							else
								tmp = (ACC << 1);

							FLAGS &= ~V;
							if (tmp & (1<<8))
								FLAGS |= V;

							ACC = tmp & 0xFF;
							break;
						case 0x80: //DDS="Deslocamento para a Direita com duplicacao de Sinal"
								//     Rotate right with signal duplication
							FLAGS &= ~V;
							if (ACC & 1)
								FLAGS |= V;

							ACC = (ACC & (1 << 7)) | ACC >> 1;
							break;
						default:
							logerror("Illegal instruction: %02X %02X\n", m_opcode, value);
							return;
					}
				}
			}
			return;
	}

	switch (m_opcode & 0xF0){
		case 0x00:
			//PLA = "Pula": Jump to address
			compute_effective_address((m_opcode & 0x0F) << 8 | READ_BYTE_PATINHO(PC));
			INCREMENT_PC_4K;
			PC = m_addr;
			return;
		case 0x10:
			//PLAX = "Pula indexado": Jump to indexed address
			tmp = (m_opcode & 0x0F) << 8 | READ_BYTE_PATINHO(PC);
			INCREMENT_PC_4K;
			m_idx = READ_INDEX_REG();
			compute_effective_address(m_idx + tmp);
			PC = m_addr;
			return;
		case 0x20:
			//ARM = "Armazena": Store the value of the accumulator into a given memory position
			compute_effective_address((m_opcode & 0x0F) << 8 | READ_BYTE_PATINHO(PC));
			INCREMENT_PC_4K;
			WRITE_BYTE_PATINHO(m_addr, ACC);
			return;
		case 0x30:
			//ARMX = "Armazena indexado": Store the value of the accumulator into a given indexed memory position
			tmp = (m_opcode & 0x0F) << 8 | READ_BYTE_PATINHO(PC);
			INCREMENT_PC_4K;
			m_idx = READ_INDEX_REG();
			compute_effective_address(m_idx + tmp);
			WRITE_BYTE_PATINHO(m_addr, ACC);
			return;
		case 0x40:
			//CAR = "Carrega": Load a value from a given memory position into the accumulator
			compute_effective_address((m_opcode & 0x0F) << 8 | READ_BYTE_PATINHO(PC));
			INCREMENT_PC_4K;
			ACC = READ_BYTE_PATINHO(m_addr);
			return;
		case 0x50:
			//CARX = "Carga indexada": Load a value from a given indexed memory position into the accumulator
			tmp = (m_opcode & 0x0F) << 8 | READ_BYTE_PATINHO(PC);
			INCREMENT_PC_4K;
			m_idx = READ_INDEX_REG();
			compute_effective_address(m_idx + tmp);
			ACC = READ_BYTE_PATINHO(m_addr);
			return;
		case 0x60:
			//SOM = "Soma": Add a value from a given memory position into the accumulator
			compute_effective_address((m_opcode & 0x0F) << 8 | READ_BYTE_PATINHO(PC));
			INCREMENT_PC_4K;
			value = READ_BYTE_PATINHO(m_addr);
			update_addition_flags(ACC, value);
			ACC += value;
			return;
		case 0x70:
			//SOMX = "Soma indexada": Add a value from a given indexed memory position into the accumulator
			tmp = (m_opcode & 0x0F) << 8 | READ_BYTE_PATINHO(PC);
			INCREMENT_PC_4K;
			m_idx = READ_INDEX_REG();
			compute_effective_address(m_idx + tmp);
			value = READ_BYTE_PATINHO(m_addr);
			update_addition_flags(ACC, value);
			ACC += value;
			return;
		case 0xA0:
			//PLAN = "Pula se ACC negativo": Jump to a given address if ACC is negative
			compute_effective_address((m_opcode & 0x0F) << 8 | READ_BYTE_PATINHO(PC));
			INCREMENT_PC_4K;
			if ((signed char) ACC < 0)
				PC = m_addr;
			return;
		case 0xB0:
			//PLAZ = "Pula se ACC for zero": Jump to a given address if ACC is zero
			compute_effective_address((m_opcode & 0x0F) << 8 | READ_BYTE_PATINHO(PC));
			INCREMENT_PC_4K;
			if (ACC == 0)
				PC = m_addr;
			return;
		case 0xC0:
		{
			/* I/O instructions. The second word carries the type in the high
			   nibble and the command ("comando") in the low one; the channel is
			   the low nibble of the first word. Both fields are in Fregni
			   (1972), figure 4.4.

			   Everything below this point used to live in the CPU: the status
			   and control flip-flops of all sixteen channels, and the seven
			   standard functions. Chapter 12 of the assembler manual says they
			   belong to the interface boards, and that is where they are now.
			   The processor only forwards. */
			value = READ_BYTE_PATINHO(PC);
			INCREMENT_PC_4K;
			offs_t const sel = ((m_opcode & 0x0F) << 4) | (value & 0x0F);
			switch (value & 0xF0)
			{
			case 0x10: m_io_func_cb(sel, 0); break;              // FNC
			case 0x20:                                           // SAL
				if (m_io_skip_cb(sel))
					SKIP_TWO_WORDS;
				break;
			case 0x40: ACC = m_io_data_r_cb(sel); break;         // ENTR
			case 0x80: m_io_data_w_cb(sel, ACC); break;          // SAI
			default:
				logerror("%03X: malformed I/O instruction /C%X %02X\n",
						PC, m_opcode & 0x0F, value);
			}
			return;
		}
		case 0xE0:
			//SUS = "Subtrai um ou Salta": Subtract one from the data in the given address
			//                             or, if the data is zero, then simply skip a couple bytes.
			compute_effective_address((m_opcode & 0x0F) << 8 | READ_BYTE_PATINHO(PC));
			INCREMENT_PC_4K;
			value = READ_BYTE_PATINHO(m_addr);
			if (value > 0){
				WRITE_BYTE_PATINHO(m_addr, value-1);
			} else {
				INCREMENT_PC_4K;
				INCREMENT_PC_4K;
			}
			return;
		case 0xF0:
			//PUG = "Pula e guarda": Jump and store.
			//      It stores the return address to addr and addr+1
			//      And then jumps to addr+2
			compute_effective_address((m_opcode & 0x0F) << 8 | READ_BYTE_PATINHO(PC));
			INCREMENT_PC_4K;
			WRITE_BYTE_PATINHO(m_addr, (PC >> 8) & 0x0F);
			WRITE_BYTE_PATINHO(m_addr+1, PC & 0xFF);
			PC = m_addr+2;
			return;
	}
	logerror("unimplemented opcode: 0x%02X\n", m_opcode);
}

std::unique_ptr<util::disasm_interface> patinho_feio_cpu_device::create_disassembler()
{
	return std::make_unique<patinho_feio_disassembler>();
}
