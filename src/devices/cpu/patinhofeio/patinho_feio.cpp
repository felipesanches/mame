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

/* Two ways in to the same core, and the difference between them is the
   MEMORIA lever.  PATINHO is what the running program uses and the protection
   applies to it; PANEL is what the operator's own hands do and the protection
   does not.  Which is which is argued over memory_is_protected() below. */
#define READ_BYTE_PATINHO(A) (program_read_byte(A))
#define WRITE_BYTE_PATINHO(A,V) (program_write_byte(A,V))

#define READ_BYTE_PANEL(A) (m_program->read_byte(A))
#define WRITE_BYTE_PANEL(A,V) (m_program->write_byte(A,V))

#define READ_WORD_PATINHO(A) (READ_BYTE_PATINHO(A+1)*256 + READ_BYTE_PATINHO(A))

#define READ_INDEX_REG() READ_BYTE_PATINHO(0x000)
#define WRITE_INDEX_REG(V) { WRITE_BYTE_PATINHO(0x000, V); m_idx = V; }

#define READ_ACC_EXTENSION_REG() READ_BYTE_PATINHO(0x001)
#define WRITE_ACC_EXTENSION_REG(V) { WRITE_BYTE_PATINHO(0x001, V); m_ext = V; }

#define ADDRESS_MASK_4K    0xFFF
#define INCREMENT_PC_4K    (PC = (PC+1) & ADDRESS_MASK_4K)

/* The "instrucoes curtas do grupo 2" (0x90-0x97) skip the whole next
   instruction, which may be two words long (July 1977 assembler manual:
   "Podem resultar em saltos (CI <- CI + 2)").  Skipping a single word
   lands inside a long instruction and executes its operand as an opcode. */
#define SKIP_NEXT_INSTRUCTION  { INCREMENT_PC_4K; INCREMENT_PC_4K; }

/* Chapter 13 describes the I/O skips as "salta duas palavras se ...": a fixed
   number of words, not "whatever the next instruction is" as in the group-2
   rule above.  Same distance on this machine, distinct rules. */
#define SKIP_TWO_WORDS  { INCREMENT_PC_4K; INCREMENT_PC_4K; }

/* Both flags are defined in chapter 2 of the July 1977 assembler manual: V
   ("vai-um") is "o vai-um na ultima soma realizada (bit mais significativo)",
   the carry out of bit 7; T ("transbordo") is signed overflow, illustrated
   there with 60 + 70 = 130, outside the range of a signed byte. */
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

/* Page 16.12 of the July 1977 assembler manual: "A area protegida comeca na
   posicao /F80 e vai ate o fim da memoria (/FFF)."  It holds the absolute
   loader.  Protection blocks reads as well as writes -- page 16.11: "nada
   pode ser gravado ou lido nesta area, e consequentemente, o programa nela
   armazenado nao pode ser executado" -- and the load procedure on page 16.12
   must DESPROTEGER before the fetch at /F80 will run, although the loader
   itself never writes above /F7F.  Protection binds only the running program
   (chapter 3: "nada pode ser armazenado por PROGRAMAS NORMAIS EM EXECUCAO"),
   not the panel, which page 16.11 uses to key the "micro-pre-carregador"
   back in; ARMAZENAMENTO and EXPOSICAO therefore reach core with the lever
   in either position.  Returning zero for a refused read is an inference; it
   yields the documented behaviour, a jump into the protected area fetching
   /00, which is PLA /000. */
bool patinho_feio_cpu_device::memory_is_protected(offs_t addr) const {
	return m_memory_protected && (addr >= 0xF80);
}

uint8_t patinho_feio_cpu_device::program_read_byte(offs_t addr) {
	if (memory_is_protected(addr))
		return 0x00;
	return m_program->read_byte(addr);
}

void patinho_feio_cpu_device::program_write_byte(offs_t addr, uint8_t data) {
	if (memory_is_protected(addr))
		return;
	m_program->write_byte(addr, data);
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
	, m_preparacao_cb(*this)
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

//TODO: handle the special purpose registers also mapped to the first main
//      memory positions: ERI ("Endereco de Retorno de Interrupcao") at 002
//      and 003, ETI ("inicio de uma rotina de tratamento de interrupcao")
//      at 004 (and 005?).  General purpose memory appears to start at 006.

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
	save_item(NAME(m_not_in_interrupt));
	save_item(NAME(m_panel_interrupt));
	save_item(NAME(m_memory_protected));
	save_item(NAME(m_pul_delay));
	save_item(NAME(m_int_line));
	save_item(NAME(m_scheduled_IND_bit_reset));
	save_item(NAME(m_indirect_addressing));
	save_item(NAME(m_mode));
	save_item(NAME(m_prev_buttons));

	/* The internal RAM is not registered here: it is a memory share, and
	   memory_manager::allocate_memory() already puts every allocated block in
	   the save state, so a save_pointer() would register it twice.  MAME does
	   not save ioports, so m_prev_buttons -- the edge detector over the panel
	   buttons -- is emulated state that must be saved even though the buttons
	   themselves cannot be. */

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
	// Page 12.17 and page A.11, on what the PREPARACAO button leaves behind:
	// "PERMITE/INIBE: ligado (permite)" and "NAO ESTA/ESTA: ligado (nao esta)".
	// This used to come up inhibited, which no page of the manual asks for.
	m_interrupts_enabled = true;
	m_not_in_interrupt = true;
	m_panel_interrupt = false;
	m_pul_delay = false;
	m_scheduled_IND_bit_reset = false;
	m_indirect_addressing = false;
	m_addr = 0;
	m_opcode = 0;
	m_mode = ADDRESSING_MODE;
	m_prev_buttons = 0;

	m_update_panel_cb(ACC, m_opcode, READ_BYTE_PANEL(m_addr), m_addr, PC, FLAGS, RC, m_mode, !m_run);
}

/* The I/O bus drives this: the OR of the sixteen PEDIDO flip-flops.  There is
   exactly one line, because chapter 11 gives the machine one interrupt level. */
void patinho_feio_cpu_device::execute_set_input(int inputnum, int state)
{
	if (inputnum == IRQ_LINE)
		m_int_line = (state != CLEAR_LINE);
}

/* Acceptance rule, drawn on page 12.16 and in words on page 12.14: aceito =
   (OR of the sixteen PEDIDO flip-flops OR panel button) AND PERMITE/INIBE AND
   NAO ESTA/ESTA.  The per-device PERMITE/IMPEDE is not part of it: page A.12
   puts it inside the interface board, gating whether that device's PEDIDO can
   be set at all.  There is no priority encoder; the program chooses the
   service order through its "SAL /n4" tests (page 12.12). */
bool patinho_feio_cpu_device::interrupt_accepted() const
{
	return (m_int_line || m_panel_interrupt)
			&& m_interrupts_enabled && m_not_in_interrupt && !m_pul_delay;
}

/* Interrupt acceptance is held off for one instruction after PUL.  This is an
   inference, not documented.  Chapter 11 makes PUL "equivalente a PLA 2 +
   termina interrupcao", so the machine passes through /002, while page 12.12
   expects a new interrupt to be accepted immediately after PUL.  Both hold
   only if acceptance waits for the return jump at /002 to execute: otherwise
   the hardware writes /002 into ERI and every later return lands on a
   "PLA /002" pointing at itself.  The interrupt sequencing of the control
   unit in Fregni (1972) would confirm or refute it. */
void patinho_feio_cpu_device::take_interrupt()
{
	/* Page 12.9: "havera um desvio para a posicao 4 da memoria, onde deve
	   haver uma rotina para tratamento da interrupcao".  ERI at /002-/003
	   holds the 12-bit return address, so the high nibble of /002 is zero --
	   the PLA opcode -- and the pair reads back as "PLA <return>". */
	WRITE_BYTE_PATINHO(0x002, (PC >> 8) & 0x0F);
	WRITE_BYTE_PATINHO(0x003, PC & 0xFF);

	m_not_in_interrupt = false;   // page 12.17: the CPU clears it on accepting
	m_panel_interrupt = false;    // page A.11, item 3, for the panel button
	PC = 0x004;

	// An ESP is over.  PARE is not: it never gets here, because the caller
	// only asks while running or while waiting.
	m_run = true;
	m_wait_for_interrupt = false;
}

/* execute instructions on this CPU until icount expires */
void patinho_feio_cpu_device::execute_run() {
	do {
		read_panel_keys_register();
		m_ext = READ_ACC_EXTENSION_REG();
		m_idx = READ_INDEX_REG();
		/* The lamps show what is really in core even in the protected area,
		   for the same reason the panel is not blocked (see
		   memory_is_protected()).  The last argument is the PARADO lamp, lit
		   whenever the processor is not executing instructions -- PARE, ESP
		   and the panel modes alike (chapter 11).  The panel's other lamp,
		   EXTERNO, has no documented meaning and stays unwired. */
		m_update_panel_cb(ACC, READ_BYTE_PANEL(PC), READ_BYTE_PANEL(m_addr), m_addr, PC, FLAGS, RC, m_mode, !m_run);

		/* The panel INTERRUPCAO button is a flip-flop of its own (page A.11),
		   so it has to be sampled while the processor is running too, not only
		   in the stopped branch below where the other buttons are read. */
		if (!m_buttons_read_cb.isunset())
		{
			uint16_t const b = m_buttons_read_cb(0);

			/* The MEMORIA lever is a physical knob and keeps its position
			   across power cycles, so no bit value is the power-on state;
			   mapping 0 to PROTEGIDA is a declared choice (see
			   src/mame/layout/patinho.lay). */
			m_memory_protected = !(b & BUTTON_MEMORIA_LIBERADA);

			/* The panel INTERRUPCAO button is a flip-flop of its own (page
			   A.11), so it has to be sampled while the processor is running
			   too, not only in the stopped branch below. */
			if (b & BUTTON_INTERRUPCAO)
				m_panel_interrupt = true;

			/* PREPARACAO likewise: it is the reset button, and a reset button
			   that only works on an already-stopped machine would be of little
			   use.  It used to be read only in the stopped branch. */
			if (b & BUTTON_PREPARACAO)
			{
				device_reset();
				m_preparacao_cb(1);
				m_preparacao_cb(0);
			}
		}

		/* ESP waits for an interrupt; PARE does not accept one ("Para a
		   maquina (nao aceita interrupcao)"), and neither do the panel modes.
		   Hence the guard rather than a bare interrupt_accepted(). */
		if ((m_run || m_wait_for_interrupt) && interrupt_accepted())
			take_interrupt();

		if (!m_run){
			debugger_wait_hook();
			if (!m_buttons_read_cb.isunset()){
				uint16_t buttons = m_buttons_read_cb(0);
				/* Edge, not level: only the 0->1 transition counts as a press. */
				uint16_t pressed = buttons & ~m_prev_buttons;
				m_prev_buttons = buttons;
				if (pressed & BUTTON_PARTIDA){
					/* "startup" button */
					switch (m_mode){
						case ADDRESSING_MODE: PC = RC; break;
						case NORMAL_MODE: m_run = true; break;
						case DATA_STORE_MODE:
							/* The panel lever "ENDERECAMENTO
							   (Fixo/Sequencial)": FIXED leaves the address
							   put, so every PARTIDA rewrites the same word;
							   SEQUENTIAL advances it by one, so a block can
							   be keyed in without re-entering the address.
							   Twelve-bit register, so /FFF wraps to /000. */
							WRITE_BYTE_PANEL(PC, RC & 0xFF);
							if (buttons & BUTTON_TIPO_DE_ENDERECAMENTO)
								PC = (PC + 1) & 0xFFF;
							break; //TODO: we also need RE (address register, instead of using PC directly)
						case DATA_VIEW_MODE:
							/* EXPOSICAO shows the word the address register
							   points at.  The address comes from PC and not
							   from RC because RC is a row of physical levers
							   that the machine cannot move.  Advancing in
							   SEQUENTIAL is documented for the 1975
							   simulator, chapter 3, command "X --
							   Exposicao": "A execucao deste comando altera o
							   valor do CI para CI + YY"; one word per press
							   gives +1.  Coupling it to the Fixo/Sequencial
							   lever is an inference -- the simulator has no
							   such lever and always advances. */
							m_addr = PC;
							if (buttons & BUTTON_TIPO_DE_ENDERECAMENTO)
								PC = (PC + 1) & 0xFFF;
							break;
						default: break;
					}
				}
				if (pressed & BUTTON_NORMAL) m_mode = NORMAL_MODE;
				if (pressed & BUTTON_ENDERECAMENTO) m_mode = ADDRESSING_MODE;
				if (pressed & BUTTON_EXPOSICAO) m_mode = DATA_VIEW_MODE;
				if (pressed & BUTTON_ARMAZENAMENTO) m_mode = DATA_STORE_MODE;
				if (pressed & BUTTON_CICLO_UNICO) m_mode = CYCLE_STEP_MODE;
				if (pressed & BUTTON_INSTRUCAO_UNICA) m_mode = INSTRUCTION_STEP_MODE;
				// BUTTON_PREPARACAO is handled at the top of the loop, so that
				// it works whether the machine is running or stopped.
			}
			m_icount = 0;   /* if processor is stopped, just burn cycles */
		} else {
			debugger_instruction_hook(PC);
			bool const had_grace = m_pul_delay;
			execute_instruction();
			// The grace lasts exactly one instruction. Testing the flag saved
			// before the instruction ran is what keeps a PUL from clearing the
			// grace it has just asked for.
			if (had_grace)
				m_pul_delay = false;
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
			//PUL="Pula para /002 e limpa estado de interrupcao"
			//     Jumping to /002 executes the ERI return address, laid out
			//     as "PLA <retorno>" (see take_interrupt).
			//     Page 12.17: PUL restores NAO ESTA/ESTA only; PERMITE/INIBE
			//     is moved by PERM and INIB alone (page A.11).
			PC = 0x002;
			m_not_in_interrupt = true;
			m_pul_delay = true;   // see take_interrupt(), "ONE INSTRUCTION OF GRACE"
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
			/* I/O instructions.  The second word carries the type in the high
			   nibble and the command ("comando") in the low one; the channel
			   is the low nibble of the first word (Fregni (1972), figure 4.4).
			   The per-channel status and control flip-flops and the seven
			   standard functions belong to the interface boards, not to the
			   processor (chapter 12); this only forwards. */
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
