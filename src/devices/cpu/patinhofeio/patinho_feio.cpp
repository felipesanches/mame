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

/* THE PROTECTED AREA, AND WHAT "PROTECTED" MEANS HERE.

   Chapter 16 of the July 1977 assembler manual, page 16.12: "A area protegida
   comeca na posicao /F80 e vai ate o fim da memoria (/FFF)."  It holds the
   absolute loader, which is why the machine could be switched on and used
   without anybody keying a bootstrap in first.

   IT BLOCKS READS AS WELL AS WRITES.  Page 16.11, on the protected area:
   "Alem disso, normalmente, nada pode ser gravado ou lido nesta area, e
   consequentemente, o programa nela armazenado nao pode ser executado."  The
   "consequentemente" is the load-bearing word: it is the blocked READ, not the
   blocked write, that keeps the loader from running.

   And the operating procedure proves it independently of that sentence.  Page
   16.12 runs: c) enderecamento, d) put /F80 in the key register, e) partida,
   f) DESPROTEGER A MEMORIA, g) normal, h) partida, and only then does the
   loader run.  The loader writes into /004 to /F7F and never into the protected
   area.  If protection stopped writes alone, step f) would have nothing to do.
   It is there because otherwise the instruction fetch at /F80 is refused.

   IT DOES NOT REACH THE PANEL.  The only sentence in either manual that names
   who is being kept out is chapter 3: "Nesta area nada pode ser armazenado por
   PROGRAMAS NORMAIS EM EXECUCAO."  Nothing anywhere says the operator's own
   hands are kept out, and the recovery procedure argues the other way: when the
   protected area is wrecked, page 16.11 says it is put back with the
   "micro-pre-carregador", whose instructions were "afixadas no proprio painel"
   -- a panel procedure that has to be able to write there.  So ARMAZENAMENTO
   and EXPOSICAO go straight to core, and the keying-in of the loader through
   the panel works with the lever in either position.

   WHAT A REFUSED READ RETURNS IS AN INFERENCE.  The manual says nothing is
   read; it does not say what appears instead.  Zero is this emulator's choice.
   The visible consequence is the documented one: a jump into the protected area
   with the lever at PROTEGIDA fetches /00, which is PLA /000, so the program
   stored there does not run. */
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
	save_item(NAME(m_not_in_interrupt));
	save_item(NAME(m_panel_interrupt));
	save_item(NAME(m_memory_protected));
	save_item(NAME(m_pul_delay));
	save_item(NAME(m_int_line));
	save_item(NAME(m_scheduled_IND_bit_reset));
	save_item(NAME(m_indirect_addressing));
	save_item(NAME(m_mode));
	save_item(NAME(m_prev_buttons));

	/* WHAT IS NOT REGISTERED ABOVE, so that nobody has to work it out again:

	     the 4096 words of core   the internal RAM is a memory share, and
	                              memory_manager::allocate_memory() puts every
	                              allocated block in the save state by itself
	                              (save_memory() in src/emu/emumem.cpp).  Adding
	                              a save_pointer() here would register it twice.

	     m_icount                 a time-slice counter.  It only has a meaning
	                              inside execute_run(), and a save state is
	                              taken between time slices, never inside one.
	                              What does survive a slice --  the suspend
	                              flags, the total cycle count, the local time
	                              and the state of the input lines -- is
	                              registered by device_execute_interface
	                              (src/emu/diexec.cpp).

	     m_address_mask           declared and never used: not one read and not
	                              one write in this file.  Saving a dead member
	                              would freeze it into the state file format.

	     m_program                resolved in device_start() from the address
	                              space; m_program_config is configuration.

	     m_update_panel_cb and    delegates, rebuilt when the machine is put
	     the seven devcb_*        together.

	   The front panel switches are not here either, and cannot be: MAME does
	   not save ioports at all (there is no save_item anywhere in
	   src/emu/ioport.cpp).  They are live input from the host, which is why
	   m_prev_buttons -- the EDGE DETECTOR over them, which is emulated state --
	   has to be saved even though the buttons themselves are not. */

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

/* The acceptance rule, drawn on page 12.16 and listed in words on page 12.14:

       aceito = ( OR(PEDIDO de /0 a /F)  OR  botao do painel )
                AND  PERMITE/INIBE
                AND  NAO ESTA/ESTA

   The per-device PERMITE/IMPEDE is deliberately NOT in this expression.  It
   sits upstream, inside the interface board, gating whether that device's
   PEDIDO can be set at all -- page A.12 draws it there, with a dashed arrow
   into the PEDIDO flip-flop, and iobus.cpp implements it there.

   There is no priority encoder to write: the sixteen requests reach one OR
   gate and lose their identity, and the program is what decides who gets
   served, by the order in which it tests "SAL /n4" (page 12.12). */
bool patinho_feio_cpu_device::interrupt_accepted() const
{
	return (m_int_line || m_panel_interrupt)
			&& m_interrupts_enabled && m_not_in_interrupt && !m_pul_delay;
}

/* ONE INSTRUCTION OF GRACE AFTER PUL -- an inference, flagged as one.

   Chapter 11 is explicit that PUL is "equivalente a PLA 2 + termina
   interrupcao": it jumps TO /002 and the two bytes sitting there, which are
   the return address in "PLA <retorno>" layout, are then executed as an
   instruction.  So the machine really does pass through /002.

   Now put that next to the second interrupt discovery method of page 12.12:
   treat one device, clear its ESTADO and PEDIDO, "e encerrar a interrupcao com
   o PUL.  Se houver mais algum equipamento pedindo interrupcao, havera nova
   interrupcao logo em seguida e outro dispositivo sera tratado."

   A new interrupt immediately after PUL is therefore expected to work.  But if
   the machine could accept it while the program counter still points at /002,
   the hardware would write /002 into ERI, and every subsequent return would
   land on a "PLA /002" pointing at itself.  The machine would hang, and the
   method the manual recommends as the easier of the two would be unusable.

   So acceptance must be held off for one instruction after PUL -- long enough
   for the return jump at /002 to execute.  This is the same shape as the
   6502's RTI/CLI delay, and it is the only model that keeps chapter 11 and
   page 12.12 both true at once.

   WHAT WOULD SETTLE IT: the control unit itself, in Fregni (1972), doc 02 in
   this project.  Until someone reads the interrupt sequencing there, this is a
   reasoned guess and not a transcription.  scripts/sintetizador/teste_interrupcao.lua
   case F is what fails if the guess is wrong. */
void patinho_feio_cpu_device::take_interrupt()
{
	/* Page 12.9: "havera um desvio para a posicao 4 da memoria, onde deve
	   haver uma rotina para tratamento da interrupcao".

	   The return address goes to ERI, at /002-/003, in the layout that makes
	   the two bytes read back as "PLA <return>": the address is 12 bits, so
	   the high nibble of /002 is zero, and zero is the opcode of PLA.  That is
	   what makes PUL a plain jump to /002, and it is what lets the ILO routine
	   of the synthesiser executor add 2 to /003 so that the interrupt returns
	   one instruction further on. */
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
		/* The lamps are the operator looking at the panel, so they show what is
		   really in core even in the protected area: same reason ARMAZENAMENTO
		   and EXPOSICAO are not blocked (see memory_is_protected()).

		   THE PARADO LAMP is the last argument, and it is lit whenever the
		   processor is not executing instructions.  That covers all three ways
		   of getting there, and chapter 11 puts the first two in the same
		   words: PARE "para o processamento, que so recomeca quando for
		   acionado o botao de partida", ESP "para o processamento ate acontecer
		   um pedido de interrupcao ou ser acionado o botao de partida", and the
		   panel modes stop it as well ("ou ate ser parado manualmente pelo
		   operador", chapter 3).

		   The panel has a second lamp next to this one, EXTERNO, and it is
		   TEMPTING to give ESP that one and leave PARADO for PARE alone.  No
		   document we have says what EXTERNO means, so it stays as it has
		   always been, unwired, rather than being wired to a guess. */
		m_update_panel_cb(ACC, READ_BYTE_PANEL(PC), READ_BYTE_PANEL(m_addr), m_addr, PC, FLAGS, RC, m_mode, !m_run);

		/* The panel INTERRUPCAO button is a flip-flop of its own (page A.11),
		   so it has to be sampled while the processor is running too, not only
		   in the stopped branch below where the other buttons are read. */
		if (!m_buttons_read_cb.isunset())
		{
			uint16_t const b = m_buttons_read_cb(0);

			/* Sample the MEMORIA lever once per pass, before anything can
			   touch core.  It is a physical knob: it holds whatever position
			   the operator left it in, across power cycles, so no bit value
			   is "the" power-on state.  Mapping 0 to PROTEGIDA is a choice --
			   see the comment in src/mame/layout/patinho.lay, which used to
			   claim documentary support for it and no longer does. */
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
							/* ADDRESSING: FIXED or SEQUENTIAL.
							   The panel carries a lever labelled "ENDERECAMENTO
							   (Fixo/Sequencial)", and it decides what happens to
							   the address after a byte is stored:

							     FIXED       the address stays put, so every
							                 PARTIDA rewrites the same word.
							     SEQUENTIAL  the address advances by one, so a
							                 block can be keyed in by setting the
							                 address once and then, for each byte,
							                 only touching the data levers and
							                 pressing ARMAZENAMENTO / PARTIDA.

							   That is the whole point of the lever, and it is
							   what makes hand-loading a program bearable: keying
							   the 128-byte absolute loader costs 1634 panel
							   gestures fixed against 467 sequential, measured by
							   scripts/bootstrap/contar_operacoes_painel.py in the
							   PatinhoFeio repository.

							   The wrap is deliberate: the address register is
							   twelve bits, so /FFF advances to /000 rather than
							   growing a thirteenth bit. */
							WRITE_BYTE_PANEL(PC, RC & 0xFF);
							if (buttons & BUTTON_TIPO_DE_ENDERECAMENTO)
								PC = (PC + 1) & 0xFFF;
							break; //TODO: we also need RE (address register, instead of using PC directly)
						case DATA_VIEW_MODE:
							/* EXPOSICAO: show the word the address register
							   points at, and in SEQUENTIAL walk forward.

							   The panel readout already displays
							   READ_BYTE_PATINHO(m_addr), so pointing m_addr at
							   PC is what puts the word on the lamps; there is no
							   separate RD register to load.

							   Reading from PC and not from RC is forced, not
							   chosen: in SEQUENTIAL something has to advance,
							   and RC is a row of physical levers that the
							   machine cannot move.  So the address register
							   walks and the switches stay where the operator
							   left them, which also matches ADDRESSING_MODE and
							   DATA_STORE_MODE, both of which treat PC as the
							   address and RC as the switches.

							   THAT EXPOSICAO ADVANCES AT ALL is documented, but
							   for the 1975 simulator rather than for this panel:
							   doc 03, chapter 3, command "X -- Exposicao" prints
							   YY words from CI and then, verbatim, "A execucao
							   deste comando altera o valor do CI para CI + YY".
							   One word per press gives +1.

							   What is INFERENCE here is the coupling to the
							   Fixo/Sequencial lever: the simulator has no such
							   lever and always advances.  Making FIXED hold
							   still is our reading of what the lever is for. */
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
			//     Ends an interrupt.  /002-/003 is ERI, the return address the
			//     hardware stored when it accepted the interrupt, laid out so
			//     that the two bytes read back as "PLA <return>" -- the address
			//     is 12 bits, so the high nibble of /002 is zero, which is the
			//     opcode of PLA.  Jumping to /002 therefore executes the
			//     return.
			//
			//     Page 12.17: PUL turns NAO ESTA/ESTA back on.  It does NOT
			//     touch PERMITE/INIBE, which only PERM and INIB move (page
			//     A.11).  This used to clear m_interrupts_enabled instead,
			//     which conflated the two flip-flops and left interrupts
			//     inhibited after every service routine.
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
