// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/*
    Patinho Feio - University of São Paulo, 1972
*/

#include "emu.h"

#include "patinho_terminals.h"

#include "bus/patinho/iobus.h"
#include "bus/patinho/ptreader.h"
#include "cpu/patinhofeio/patinhofeio_cpu.h"

#include "patinho.lh"


namespace {

class patinho_feio_state : public driver_device
{
public:
	patinho_feio_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_iobus(*this, "iobus")
		, m_ioslot(*this, "io%x", 1U)
		, m_mode_button(*this, "MODE_BUTTON%u", 0U)
		, m_output_acc(*this, "acc%u", 0U)
		, m_output_opcode(*this, "opcode%u", 0U)
		, m_output_mem_data(*this, "mem_data%u", 0U)
		, m_output_mem_addr(*this, "mem_addr%u", 0U)
		, m_output_pc(*this, "pc%u", 0U)
		, m_output_rc(*this, "rc%u", 0U)
		, m_output_flags(*this, "flags%u", 0U)
	{ }

	void init_patinho_feio() ATTR_COLD;

	void patinho_feio(machine_config &config) ATTR_COLD;

protected:
	virtual void machine_start() override ATTR_COLD;

	void load_tape(const char* name);
	void load_raw_data(const char* name, unsigned int start_address, unsigned int data_length);



	void update_panel(uint8_t ACC, uint8_t opcode, uint8_t mem_data, uint16_t mem_addr, uint16_t PC, uint8_t FLAGS, uint16_t RC, uint8_t mode);

	// The PREPARACAO button also clears the flip-flops of every interface
	// board (page 12.17 and page A.11), and those live on the bus.
	void preparacao_w(int state) { if (state) m_iobus->reset_bus(); }

	required_device<patinho_feio_cpu_device> m_maincpu;
	required_device<patinho_io_bus_device> m_iobus;
	optional_device_array<patinho_io_slot_device, 15> m_ioslot; // channels /1 to /F

private:
	output_finder<6> m_mode_button;
	output_finder<8> m_output_acc;
	output_finder<8> m_output_opcode;
	output_finder<8> m_output_mem_data;
	output_finder<12> m_output_mem_addr;
	output_finder<12> m_output_pc;
	output_finder<12> m_output_rc;
	output_finder<2> m_output_flags;


	uint8_t m_prev_ACC = 0;
	uint8_t m_prev_opcode = 0;
	uint8_t m_prev_mem_data = 0;
	uint16_t m_prev_mem_addr = 0;
	uint16_t m_prev_PC = 0;
	uint8_t m_prev_FLAGS = 0;
	uint16_t m_prev_RC = 0;
};


void patinho_feio_state::init_patinho_feio()
{
	m_prev_ACC = 0;
	m_prev_opcode = 0;
	m_prev_mem_data = 0;
	m_prev_mem_addr = 0;
	m_prev_PC = 0;
	m_prev_FLAGS = 0;
	m_prev_RC = 0;
}

void patinho_feio_state::update_panel(uint8_t ACC, uint8_t opcode, uint8_t mem_data, uint16_t mem_addr, uint16_t PC, uint8_t FLAGS, uint16_t RC, uint8_t mode){
	for (int i=0; i<6; i++){
		m_mode_button[i] = (mode == i) ? 1 : 0;
	}

	for (int i=0; i<8; i++){
		if ((m_prev_ACC ^ ACC) & (1 << i)){
			m_output_acc[i] = (ACC >> i) & 1;
		}
		if ((m_prev_opcode ^ opcode) & (1 << i)){
			m_output_opcode[i] = (opcode >> i) & 1;
		}
		if ((m_prev_mem_data ^ mem_data) & (1 << i)){
			m_output_mem_data[i] = (mem_data >> i) & 1;
		}
	}
	m_prev_ACC = ACC;
	m_prev_opcode = opcode;
	m_prev_mem_data = mem_data;

	for (int i=0; i<12; i++){
		if ((m_prev_mem_addr ^ mem_addr) & (1 << i)){
			m_output_mem_addr[i] = (mem_addr >> i) & 1;
		}
		if ((m_prev_PC ^ PC) & (1 << i)){
			m_output_pc[i] = (PC >> i) & 1;
		}
		if ((m_prev_RC ^ RC) & (1 << i)){
			m_output_rc[i] = (RC >> i) & 1;
		}
	}
	m_prev_mem_addr = mem_addr;
	m_prev_PC = PC;
	m_prev_RC = RC;

	if ((m_prev_FLAGS ^ FLAGS) & (1 << 0)) m_output_flags[0] = (FLAGS >> 0) & 1;
	if ((m_prev_FLAGS ^ FLAGS) & (1 << 1)) m_output_flags[1] = (FLAGS >> 1) & 1;
	m_prev_FLAGS = FLAGS;
}







/* The hardware does not perform this checking.
   This is implemented here only for debugging purposes.

   Also, proper punched paper tape emulation does
   not use this function at all.
*/
void patinho_feio_state::load_tape(const char* name){
	uint8_t *RAM = (uint8_t *) memshare("maincpu:internalram")->ptr();
	uint8_t *data = memregion(name)->base();
	unsigned int data_length = data[0];
	unsigned int start_address = data[1]*256 + data[2];
	int8_t expected_checksum = data[data_length + 3];
	int8_t checksum = 0;

	for (int i = 0; i < data_length + 3; i++){
		checksum -= (int8_t) data[i];
	}

	if (checksum != expected_checksum){
		printf("[WARNING] Tape \"%s\": checksum = 0x%02X (expected 0x%02X)\n",
			name, (unsigned char) checksum, (unsigned char) expected_checksum);
	}

	memcpy(&RAM[start_address], &data[3], data_length);
}

void patinho_feio_state::load_raw_data(const char* name, unsigned int start_address, unsigned int data_length){
	uint8_t *RAM = (uint8_t *) memshare("maincpu:internalram")->ptr();
	uint8_t *data = memregion(name)->base();

	memcpy(&RAM[start_address], data, data_length);
}


/* Optical punched tape reader, channel /E.

   Doc 03 (J. J. Neto, 1975), chapter 1, lists the machine's configuration as
   carrying one "Leitora Otica de Fita de Papel HP-2737-A, 300 caracteres por
   segundo (maximo)", and doc 01 (July 1977 assembler manual), chapter 12,
   lists channel /E as "Leitora de Fita de Papel", input only.

   The handshake is the standard one described in that same chapter: the
   program turns the CONTROL flip-flop on with "FNC /E6" (which also sets
   STATUS to busy, meaning "tape running"), the reader feeds one frame and
   reports STATUS ready, and "ENTR /E0" takes the byte and drops CONTROL.

   So the timer only advances the tape while CONTROL is asserted; with the
   reel stopped it costs nothing but a poll. */

void patinho_feio_state::machine_start(){
	// Copy some programs directly into RAM.
	// This is a hack for setting up the computer
	// while we don't support loading programs
	// from punched tape rolls...

	//"absolute program example" from page 16.7
	//    Prints "PATINHO FEIO" on the DECWRITER:
	load_tape("exemplo_16.7");

	//"absolute program example" from appendix G:
	//    Allows users to load programs from the
	//    console into the computer memory.
	load_raw_data("hexam", 0xE00, 0x0D5);

	load_raw_data("loader", 0xF80, 0x080);
	//load_raw_data("micro_pre_loader", 0x000, 0x02A); //this is still experimental
}

static INPUT_PORTS_START( patinho_feio )
	/* Address/Data input Switches */
	PORT_START("SWITCHES")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Switch #0") PORT_CODE(KEYCODE_EQUALS) PORT_TOGGLE
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Switch #1") PORT_CODE(KEYCODE_MINUS) PORT_TOGGLE
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Switch #2") PORT_CODE(KEYCODE_0) PORT_TOGGLE
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Switch #3") PORT_CODE(KEYCODE_9) PORT_TOGGLE
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Switch #4") PORT_CODE(KEYCODE_8) PORT_TOGGLE
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Switch #5") PORT_CODE(KEYCODE_7) PORT_TOGGLE
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Switch #6") PORT_CODE(KEYCODE_6) PORT_TOGGLE
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Switch #7") PORT_CODE(KEYCODE_5) PORT_TOGGLE
	PORT_BIT(0x100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Switch #8") PORT_CODE(KEYCODE_4) PORT_TOGGLE
	PORT_BIT(0x200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Switch #9") PORT_CODE(KEYCODE_3) PORT_TOGGLE
	PORT_BIT(0x400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Switch #10") PORT_CODE(KEYCODE_2) PORT_TOGGLE
	PORT_BIT(0x800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Switch #11") PORT_CODE(KEYCODE_1) PORT_TOGGLE

	PORT_START("BUTTONS")
	/* Modo de Operacao: EXECUCAO */
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("NORMAL") PORT_CODE(KEYCODE_A)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("CICLO UNICO") PORT_CODE(KEYCODE_S)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("INSTRUCAO UNICA") PORT_CODE(KEYCODE_D)

	/* Modo de Operacao: MEMORIA */
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("ENDERECAMENTO") PORT_CODE(KEYCODE_F)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("ARMAZENAMENTO") PORT_CODE(KEYCODE_G)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("EXPOSICAO") PORT_CODE(KEYCODE_H)

	/* Comando: */
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("ESPERA") PORT_CODE(KEYCODE_K)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("INTERRUPCAO") PORT_CODE(KEYCODE_L)
	PORT_BIT(0x100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("PARTIDA") PORT_CODE(KEYCODE_ENTER)
	PORT_BIT(0x200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("PREPARACAO") PORT_CODE(KEYCODE_M)

	/* Memory Toggle Switches */
	PORT_BIT(0x400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("ENDERECAMENTO (Fixo/Sequencial)") PORT_CODE(KEYCODE_I) PORT_TOGGLE
	PORT_BIT(0x800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("MEMORIA (Liberada/Protegida)") PORT_CODE(KEYCODE_O) PORT_TOGGLE
INPUT_PORTS_END

/* What could be plugged into a channel. Chapter 12 of the July 1977 manual
   lists the machine's equipment; the ones without a card yet are noted in the
   project plan rather than silently missing. */
static void patinho_io_devices(device_slot_interface &device)
{
	device.option_add("decwriter", PATINHO_DECWRITER); // DECwriter, historically /A
	device.option_add("tty",       PATINHO_TTY);       // Teletype ASR33, historically /B
	device.option_add("ptreader",  PATINHO_PTREADER);  // HP-2737-A, historically /E
}

void patinho_feio_state::patinho_feio(machine_config &config)
{
	/* basic machine hardware */
	/* CPU @ approx. 500 kHz (memory cycle time is 2usec) */
	PATO_FEIO_CPU(config, m_maincpu, 500000);
	m_maincpu->rc_read().set_ioport("SWITCHES");
	m_maincpu->buttons_read().set_ioport("BUTTONS");
	m_maincpu->set_update_panel_cb(FUNC(patinho_feio_state::update_panel));

	/* I/O bus: sixteen channels, /0 to /F (assembler manual, chapter 12).
	   Channel /0 is the front panel key register, which is wired in and input
	   only, so the sockets are /1 to /F. Every I/O instruction is handed to
	   the bus with the channel and the command packed into the offset. */
	PATINHO_IO_BUS(config, m_iobus);

	m_maincpu->io_func()  .set(m_iobus, FUNC(patinho_io_bus_device::func_w));
	m_maincpu->io_data_r().set(m_iobus, FUNC(patinho_io_bus_device::data_r));
	m_maincpu->io_data_w().set(m_iobus, FUNC(patinho_io_bus_device::data_w));
	m_maincpu->io_skip()  .set(m_iobus, FUNC(patinho_io_bus_device::skip_r));

	/* One interrupt line for the whole machine: chapter 11 gives it a single
	   level, and page 12.16 draws the sixteen PEDIDO flip-flops reaching it
	   through one OR gate. Nothing to arbitrate, so nothing to configure. */
	m_iobus->int_handler().set_inputline(m_maincpu, patinho_feio_cpu_device::IRQ_LINE);

	/* PREPARACAO clears flip-flops all over the machine, and four of the six
	   page A.11 lists live in the interface boards, not in the processor. */
	m_maincpu->preparacao().set(FUNC(patinho_feio_state::preparacao_w));

	/* The equipment the machine came with, in the channels chapter 12 gives
	   them. Any of these can be moved, removed or replaced from the MAME UI
	   or from the command line: "-ioa tty", "-ioe \"\"", and so on. */
	static char const *const dflt[16] =
	{
		nullptr, nullptr, nullptr, nullptr,       // /0 /1 /2 /3
		nullptr, nullptr, nullptr, nullptr,       // /4 /5 /6 /7
		nullptr, nullptr, "decwriter", "tty",     // /8 /9 /A /B
		nullptr, nullptr, "ptreader", nullptr     // /C /D /E /F
	};

	for (unsigned ch = 1; ch < patinho_io_bus_device::CHANNELS; ch++)
		PATINHO_IO_SLOT(config, m_ioslot[ch - 1], ch, m_iobus, patinho_io_devices, dflt[ch]);

	config.set_default_layout(layout_patinho);

	// software lists
//  SOFTWARE_LIST(config, "tape_list").set_original("patinho");
}

ROM_START( patinho )
	ROM_REGION( 0x0d5, "hexam", 0 )
	ROM_LOAD( "apendice_g__hexam.bin", 0x000, 0x0d5, CRC(e608f6d3) SHA1(3f76b5f91d9b2573e70919539d47752e7623e40a) )

	ROM_REGION( 0x028, "exemplo_16.7", 0 )
	ROM_LOAD( "exemplo_16.7.bin", 0x000, 0x028, CRC(0a87ac8d) SHA1(7c35ac3eed9ed239f2ef56c26e6f0c59f635e1ac) )

	ROM_REGION( 0x080, "loader", 0 )
	ROM_LOAD( "loader.bin", 0x000, 0x080, BAD_DUMP CRC(c2a8fa9d) SHA1(0ae4f711ef5d6e9d26c611fd2c8c8ac45ecbf9e7) )

	/* Micro pre-loader:
	   This was re-created by professor Joao Jose Neto based on his vague
	   recollection of sequences of opcode values from almost 40 years ago :-) */
	ROM_REGION( 0x02a, "micro_pre_loader", 0 )
	ROM_LOAD( "micro-pre-loader.bin", 0x000, 0x02a, CRC(1921feab) SHA1(bb063102e44e9ab963f95b45710141dc2c5046b0) )
ROM_END

} // anonymous namespace

//    YEAR  NAME     PARENT  COMPAT  MACHINE       INPUT         CLASS               INIT               COMPANY                                           FULLNAME         FLAGS
COMP( 1972, patinho, 0,      0,      patinho_feio, patinho_feio, patinho_feio_state, init_patinho_feio, "Escola Politecnica - Universidade de Sao Paulo", "Patinho Feio" , MACHINE_NO_SOUND_HW | MACHINE_NOT_WORKING )
