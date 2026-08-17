// license:GPL-2.0+
// copyright-holders:Felipe Sanches
#ifndef MAME_CPU_PATINHOFEIO_PATINHOFEIO_CPU_H
#define MAME_CPU_PATINHOFEIO_PATINHOFEIO_CPU_H

#pragma once

/* register IDs */
enum
{
	PATINHO_FEIO_CI=1, PATINHO_FEIO_ACC, PATINHO_FEIO_EXT, PATINHO_FEIO_IDX, PATINHO_FEIO_RC
};

enum
{
	NORMAL_MODE,
	CYCLE_STEP_MODE,
	INSTRUCTION_STEP_MODE,
	ADDRESSING_MODE,
	DATA_STORE_MODE,
	DATA_VIEW_MODE
};

#define BUTTON_NORMAL                (1 << 0)  /* normal CPU execution */
#define BUTTON_CICLO_UNICO           (1 << 1)  /* single-cycle step */
#define BUTTON_INSTRUCAO_UNICA       (1 << 2)  /* single-instruction step */
#define BUTTON_ENDERECAMENTO         (1 << 3)  /* addressing action */
#define BUTTON_ARMAZENAMENTO         (1 << 4)  /* storage action */
#define BUTTON_EXPOSICAO             (1 << 5)  /* memory viewing action */
#define BUTTON_ESPERA                (1 << 6)  /* wait */
#define BUTTON_INTERRUPCAO           (1 << 7)  /* interrupt */
#define BUTTON_PARTIDA               (1 << 8)  /* startup */
#define BUTTON_PREPARACAO            (1 << 9)  /* reset */
/* The ENDERECAMENTO lever, 0: Fixo / 1: Sequencial.  Storing a byte from the
   panel leaves the address alone in Fixo and advances it in Sequencial, so the
   safe position -- the one that cannot walk over memory if the operator forgets
   about it -- is Fixo, and that is the one the bit rests at.  The layout draws
   the needle accordingly: element "rotary_switch_enderecamento" points it left,
   at the F I X O legend, for state 0.  No document fixes the electrical value;
   see the long comment over that element in src/mame/layout/patinho.lay. */
#define BUTTON_TIPO_DE_ENDERECAMENTO (1 << 10)
/* The MEMORIA lever, 0: Protegida / 1: Liberada.  It rests at Protegida: the
   assembly procedure of page 16.12 goes "Ligar o Patinho Feio" (a), then
   "Desproteger a memoria" (f), then "Proteger a memoria" (i), so the machine
   comes up protected and the operator is told to leave it that way.  The layout
   draws the needle to the right, at the PROTEGIDA legend, for state 0.  What
   the protection does is in the long comment over memory_is_protected() in
   patinho_feio.cpp. */
#define BUTTON_MEMORIA_LIBERADA      (1 << 11)

class patinho_feio_cpu_device : public cpu_device {
public:
	using update_panel_cb = device_delegate<void (uint8_t ACC, uint8_t opcode, uint8_t mem_data, uint16_t mem_addr, uint16_t PC, uint8_t FLAGS, uint16_t RC, uint8_t mode)>;

	// construction/destruction
	patinho_feio_cpu_device(const machine_config &mconfig, const char *_tag, device_t *_owner, uint32_t _clock);

	auto rc_read() { return m_rc_read_cb.bind(); }
	auto buttons_read() { return m_buttons_read_cb.bind(); }
	/* The whole I/O side is one bus now. In all four the offset is
	   (channel << 4) | command: the channel is the low nibble of the
	   instruction and the command the low nibble of its second word. Which
	   card answers is decided at run time by what the user plugged into the
	   slot, so nothing here can be a compile-time template any more. */
	auto io_func()   { return m_io_func_cb.bind(); }    // FNC  /nc
	auto io_data_r() { return m_io_data_r_cb.bind(); }  // ENTR /nc
	auto io_data_w() { return m_io_data_w_cb.bind(); }  // SAI  /nc
	auto io_skip()   { return m_io_skip_cb.bind(); }    // SAL  /nc

	// Pulsed when the PREPARACAO button is pressed, so that the rest of the
	// machine can clear what the button clears.  Page 12.17 and page A.11 list
	// the flip-flops it resets, and four of the six live in the interface
	// boards rather than in the processor.
	auto preparacao() { return m_preparacao_cb.bind(); }

	// The one interrupt line.  Chapter 11: the machine has a single level,
	// and page 12.16 shows the sixteen PEDIDO flip-flops reaching it through
	// one OR gate, with no priority encoder anywhere.
	static constexpr int IRQ_LINE = 0;
	template <typename... T> void set_update_panel_cb(T &&... args) { m_update_panel_cb.set(std::forward<T>(args)...); }



	void prog_8bit(address_map &map) ATTR_COLD;
protected:

	virtual void execute_run() override;
	virtual void execute_set_input(int inputnum, int state) override;
	virtual std::unique_ptr<util::disasm_interface> create_disassembler() override;

	address_space_config m_program_config;
	update_panel_cb m_update_panel_cb;

	offs_t m_addr;
	unsigned char m_opcode;

	/* processor registers */
	unsigned char m_acc; /* accumulator (8 bits) */
	unsigned int m_pc;   /* program counter (12 bits)
	                      * Actual register name is CI, which
	                      * stands for "Contador de Instrucao"
	                      * or "instructions counter".
	                      */
	unsigned int m_rc; /* RC = "Registrador de Chaves" (Keys Register)
	                    * It represents the 12 bits of input data
	                    * from toggle switches in the computer panel
	                    */
	unsigned char m_idx; /* IDX = Index Register */
	unsigned char m_ext; /* EXT = Accumulator Extension Register */

	/* processor state flip-flops */
	bool m_run; /* processor is running */
	bool m_wait_for_interrupt; /* stopped by ESP rather than by PARE */

	/* The two interrupt flip-flops the manual keeps apart, and which this
	 * code used to conflate into one.  Page A.11 gives what moves each:
	 *
	 *   PERMITE/INIBE   ligado por PERM, desligado por INIB.
	 *   NAO ESTA/ESTA   desligado pelo Patinho Feio ao aceitar uma
	 *                   interrupcao, religado por ele ao encerra-la (PUL).
	 *
	 * Both are "ligado" after PREPARACAO (page 12.17 and page A.11), so the
	 * machine comes up with interrupts permitted and not interrupted.
	 */
	bool m_interrupts_enabled; /* PERMITE/INIBE, true = permite */
	bool m_not_in_interrupt;   /* NAO ESTA/ESTA, true = nao esta */

	/* The panel INTERRUPCAO button is latched: page A.11 makes it a flip-flop
	 * of its own, set by the button and cleared by the Patinho Feio "ao
	 * aceitar uma interrupcao proveniente do painel". */
	bool m_panel_interrupt;

	/* The MEMORIA lever as it read on the last poll, sampled once per pass
	 * through execute_run() so that a memory access does not have to go back
	 * to the ioport.  It is a lever and not a flip-flop: nothing inside the
	 * machine ever moves it. */
	bool m_memory_protected = false;

	/* One instruction of grace after PUL.  INFERRED, not documented -- see the
	 * long comment over take_interrupt() in patinho_feio.cpp for the argument
	 * and for what would settle it. */
	bool m_pul_delay;

	/* OR of the PEDIDO flip-flops of the sixteen interfaces, as delivered by
	 * the I/O bus. */
	bool m_int_line;

	bool m_scheduled_IND_bit_reset;
	bool m_indirect_addressing;


	int m_flags;
	// V = "Vai um" (Carry flag)
	// T = "Transbordo" (Overflow flag)

	int m_address_mask;        /* address mask */
	int m_icount;

	address_space *m_program;

	// device-level overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// device_execute_interface overrides
	virtual uint32_t execute_min_cycles() const noexcept override { return 1; }
	virtual uint32_t execute_max_cycles() const noexcept override { return 3; }

	// device_memory_interface overrides
	virtual space_config_vector memory_space_config() const override;

private:
	void execute_instruction();
	bool interrupt_accepted() const;
	void take_interrupt();
	void compute_effective_address(unsigned int addr);
	bool memory_is_protected(offs_t addr) const;
	uint8_t program_read_byte(offs_t addr);
	void program_write_byte(offs_t addr, uint8_t data);
	void set_flag(uint8_t flag, bool state);
	void update_addition_flags(uint8_t operand_a, uint8_t operand_b);
	uint16_t read_panel_keys_register();
	devcb_read16 m_rc_read_cb;
	devcb_read16 m_buttons_read_cb;
	devcb_write_line m_preparacao_cb;
	devcb_write8 m_io_func_cb;
	devcb_read8  m_io_data_r_cb;
	devcb_write8 m_io_data_w_cb;
	devcb_read8  m_io_skip_cb;
	uint8_t m_mode;
	/* The panel buttons as they read on the previous poll.  PARTIDA has to act
	   ONCE PER PRESS, not once per frame the finger is down: the store and view
	   modes advance the address register in SEQUENTIAL, and advancing is not
	   idempotent the way rewriting the same byte was. */
	uint16_t m_prev_buttons;
};

DECLARE_DEVICE_TYPE(PATO_FEIO_CPU, patinho_feio_cpu_device)

#endif // MAME_CPU_PATINHOFEIO_PATINHOFEIO_CPU_H
