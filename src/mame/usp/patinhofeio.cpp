// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/*
    Patinho Feio - University of São Paulo, 1972
*/

#include "emu.h"

#include "patinho_terminals.h"

#include "bus/patinho/iobus.h"
#include "bus/patinho/ptreader.h"
#include "bus/patinho/duplex.h"
#include "bus/patinho/tbgen.h"
#include "cpu/patinhofeio/patinhofeio_cpu.h"

#include "patinho.lh"


namespace {

/* THE MEMORY IS MAGNETIC CORE, AND CORE REMEMBERS.

   The Patinho Feio had 4096 words of ferrite core memory.  Core is neither ROM
   nor volatile RAM: a core plane stores a bit as the remanent magnetisation of
   a little ring, and a ring stays magnetised with the power off.  A real
   Patinho Feio switched on in the morning held whatever had been left in it the
   evening before.  That is not a curiosity, it is the whole reason the machine
   could be used at all: the July 1977 assembler manual, page 16.11, says the
   absolute loader lived in a "area protegida na memoria do Patinho Feio, QUE
   NAO E DESTRUIDA AO DESLIGAR-SE O COMPUTADOR", created exactly so that nobody
   would have to key the loader in through the key register "sempre que o
   Patinho Feio fosse ligado".  And the operating procedure on page 16.12 proves
   it was used that way: step a) is "ligar o Patinho Feio e a leitora de fita"
   and step d) is already "colocar no Registrador de Chaves o numero /F80".
   There is no step that keys in a loader.

   So the faithful model of this memory is NVRAM, not RAM: it is saved on exit
   and restored on the next run.  What the machine comes up with is what the
   previous session left in it.

   The micro pre-loader was the RECOVERY procedure, not the morning routine --
   page 16.11 again: it was needed "caso contrario", that is, when a program run
   with the memory unprotected had wrecked the protected area.  (A 2016
   recollection document in the archive claims the opposite, that core was
   volatile and the pre-loader had to be keyed in at every power-up.  The 1977
   manual is contemporary and says otherwise; core physics agrees with 1977.) */

class patinho_feio_state : public driver_device, public device_nvram_interface
{
public:
	patinho_feio_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, device_nvram_interface(mconfig, *this)
		, m_conf(*this, "CONF")
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
	virtual void machine_reset() override ATTR_COLD;

	// device_nvram_interface -- the core memory (see the note above the class)
	virtual void nvram_default() override;
	virtual bool nvram_read(util::read_stream &file) override;
	virtual bool nvram_write(util::write_stream &file) override;

	static constexpr unsigned CORE_SIZE = 0x1000; // 4096 words of ferrite core

	uint8_t *core_memory() { return (uint8_t *) memshare("maincpu:internalram")->ptr(); }

	required_ioport m_conf;

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
	uint8_t *RAM = core_memory();
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
	uint8_t *RAM = core_memory();
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
}

/* A BRAND NEW SET OF CORE PLANES IS BLANK.

   This is what the machine looks like the very first time it is ever run, or
   after the nvram file is thrown away: nothing in memory, which is what a
   computer with no permanent storage really looked like before anybody had
   keyed anything into it.  From here the only way in is the front panel, which
   is exactly how it was in 1972. */
void patinho_feio_state::nvram_default(){
	std::fill_n(core_memory(), CORE_SIZE, 0x00);
}

bool patinho_feio_state::nvram_read(util::read_stream &file){
	auto const [err, actual] = util::read(file, core_memory(), CORE_SIZE);
	return !err && (actual == CORE_SIZE);
}

bool patinho_feio_state::nvram_write(util::write_stream &file){
	auto const [err, actual] = util::write(file, core_memory(), CORE_SIZE);
	return !err;
}

/* THE CONVENIENCE SETTING -- IT IS NOT FAITHFUL, AND THE DEFAULT IS THE FAITHFUL ONE.

   Until 2026 this driver copied three programs straight into core inside
   machine_start(), and its own comment called it what it was: "This is a hack
   for setting up the computer while we don't support loading programs from
   punched tape rolls".  That excuse has expired.  The optical reader on channel
   /E works, the reconstructed absolute loader works (445 of 445 bytes verified
   against a real tape), and the panel procedure is documented and tested, so
   tapes really do load now.

   The Patinho Feio had no permanent storage: no disc, no drum, no ROM.  A
   machine that comes up with programs already in memory is therefore not the
   machine -- it is a convenience.  So the hack is now OFF by default and lives
   behind a configuration setting the user has to ask for:

       Machine Configuration -> "Preloaded programs in core"

   With it ON, three images are copied into core on the first reset, exactly as
   the old hack did:

     - the "absolute program example" of page 16.7, which prints PATINHO FEIO;
     - HEXAM, from appendix G, the console memory examine/deposit utility;
     - the reconstructed absolute loader at /F80.

   NONE of that ever appeared by itself on a real Patinho Feio.  Somebody had to
   put it there, through the panel or from a tape.  Anyone wanting the machine
   as it was should leave this switch alone.

   Why on the first machine_reset() and not in machine_start(): configuration
   ports CANNOT be read at init time.  ioport_manager::m_safe_to_read only goes
   true in load_config(config_type::FINAL), which runs after start_all_devices()
   -- reading a port from machine_start() is a hard fatal error, not a wrong
   value.  The order is ioport init -> machine_start -> load_settings ->
   nvram_load -> machine_reset, so reset is the earliest moment the setting can
   be honoured, and it also lands after nvram_load, which is what lets the copy
   override whatever core was holding.  Measured, not assumed:
   PatinhoFeio/scripts/mame/sondar_precarga.sh and lacunas_da_sonda_precarga.sh.

   It is applied on EVERY reset, not only the first one, so that flipping the
   setting in the UI and pressing F3 does what the user just asked for.  A real
   reset does not touch core, of course -- but neither does a real machine copy
   three programs into itself, and this switch is the one place in the driver
   that is admittedly not the machine. */

void patinho_feio_state::machine_reset(){
	if (m_conf->read() & 0x01)
	{
		//"absolute program example" from page 16.7
		//    Prints "PATINHO FEIO" on the DECWRITER:
		load_tape("exemplo_16.7");

		//"absolute program example" from appendix G:
		//    Allows users to load programs from the
		//    console into the computer memory.
		load_raw_data("hexam", 0xE00, 0x0D5);

		load_raw_data("loader", 0xF80, 0x080);
	}
}

static INPUT_PORTS_START( patinho_feio )
	/* NOT A SWITCH ON THE REAL PANEL.  See the long comment on machine_reset():
	   the machine had no permanent storage, so coming up with programs already
	   in core is a convenience for the emulator user, not history.  Default OFF
	   is the faithful one. */
	PORT_START("CONF")
	PORT_CONFNAME(0x01, 0x00, "Preloaded programs in core")
	PORT_CONFSETTING(   0x00, DEF_STR( Off ))   // as the real machine
	PORT_CONFSETTING(   0x01, DEF_STR( On ))    // convenience: 16.7, HEXAM, loader

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
	                                                   // (not a default: see below)
	device.option_add("tty",       PATINHO_TTY);       // Teletype ASR33, historically /B
	device.option_add("ptreader",  PATINHO_PTREADER);  // HP-2737-A, historically /E
	device.option_add("tbgen",     PATINHO_TBGEN);     // synthesiser time base, /4
	device.option_add("duplex",    PATINHO_DUPLEX);    // 8-bit duplex, historically /6 and /7
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
	   them -- with ONE deliberate omission, /A.  Any of these can be moved,
	   removed or replaced from the MAME UI or from the command line:
	   "-ioa decwriter", "-ioe \"\"", and so on.

	   /A COMES UP EMPTY, AND THAT IS NOT WHAT THE MACHINE LOOKED LIKE.

	   Chapter 12 lists BOTH printing terminals: a DECwriter on /A and a
	   Teletype ASR33 on /B.  The real Patinho Feio had the two of them.  What
	   follows is a convenience of operation, not a claim about history:
	   driving the machine with two printing terminals at once is redundant for
	   anyone who is not reproducing the 1977 installation, and every surviving
	   program prints on /B.  Guido Stolfi's synthesiser executor is the case we
	   can check byte by byte -- it addresses channels /1 /3 /4 /5 /6 /7 /9 /B
	   /D /E /F and never /A, and its ".ERRO" diagnostic goes out of "SAI /B0"
	   at /0D9, on the Teletype.  Removing the DECwriter changes nothing it
	   prints; that was measured, not assumed, with
	   PatinhoFeio/scripts/executor/saida_do_teleimpressor.lua.

	   So whoever wants the machine as it was asks for it, in one word:

	       ./mame patinho -ioa decwriter

	   and the card is still in the slot's option list for the UI to offer. */
	static char const *const dflt[16] =
	{
		nullptr, nullptr, nullptr, nullptr,       // /0 /1 /2 /3
		"tbgen", nullptr, "duplex", "duplex",     // /4 /5 /6 /7
		nullptr, nullptr, nullptr, "tty",         // /8 /9 /A(*) /B
		nullptr, nullptr, "ptreader", nullptr     // /C /D /E /F
	};                                            // (*) chapter 12: DECwriter

	for (unsigned ch = 1; ch < patinho_io_bus_device::CHANNELS; ch++)
		PATINHO_IO_SLOT(config, m_ioslot[ch - 1], ch, m_iobus, patinho_io_devices, dflt[ch]);

	/* AND NOTHING ELSE.  THE SYNTHESISER IS NOT WIRED HERE.

	   It never was part of this computer: chapter 12 lists what each channel
	   held, and /6 and /7 hold general-purpose 8-bit duplex boards -- the same
	   board type twice, described as being there "para possibilitar a ligacao
	   entre o Patinho Feio e outros computadores".  Guido Stolfi's executor
	   couples that pair and sends the instrument a (command, data) pair, which
	   is the command set of chapter 5 of the synthesiser manual.

	   So the connector belongs to the duplex board, not to the machine, and
	   the instrument is plugged into it like any other slot device:

	       ./mame patinho -io6:duplex:port synth

	   An earlier version of this driver had a "synthport" here, at machine
	   level.  It isolated the two halves correctly but invented an interface
	   the real computer did not have; the board's own connector is the one
	   that existed.  See src/devices/bus/patinho/duplex.h for the executor's
	   evidence and for what the documents do not decide, and iobus.h for the
	   two time base lines, which belong to the /4 board and reach the same
	   instrument. */

	config.set_default_layout(layout_patinho);

	// software lists
//  SOFTWARE_LIST(config, "tape_list").set_original("patinho");
}

ROM_START( patinho )
	ROM_REGION( 0x0d5, "hexam", 0 )
	ROM_LOAD( "apendice_g__hexam.bin", 0x000, 0x0d5, CRC(e608f6d3) SHA1(3f76b5f91d9b2573e70919539d47752e7623e40a) )

	ROM_REGION( 0x028, "exemplo_16.7", 0 )
	ROM_LOAD( "exemplo_16.7.bin", 0x000, 0x028, CRC(0a87ac8d) SHA1(7c35ac3eed9ed239f2ef56c26e6f0c59f635e1ac) )

	/* THE ABSOLUTE LOADER, RECONSTRUCTED -- not a dump.

	   The original never survived: the file that was here is 128 bytes of
	   ZEROS, which is why it carried BAD_DUMP and why nothing could be loaded
	   from a punched tape without writing memory from outside the machine.

	   What is here now is a working reconstruction, assembled from
	   scripts/carregador/carregador_absoluto.asm in the PatinhoFeio
	   repository.  It is NOT the historical program -- it is a program that
	   does the same job, written with the same instructions and with the same
	   reader handshake the executor of Guido Stolfi uses (routine LEOT of
	   FITA#011).  Verified by loading FITA#012D.BIN through the reader and
	   comparing all 445 bytes against the tape: they match.

	   BAD_DUMP stays on purpose.  It is the only flag MAME has that tells a
	   user "this is not the real thing", and that is exactly what needs
	   saying.  Whoever finds the true dump should replace it. */
	ROM_REGION( 0x080, "loader", 0 )
	ROM_LOAD( "loader_reconstruido.bin", 0x000, 0x080, BAD_DUMP CRC(33b2c552) SHA1(25488794ee85c7c9a8a02d3b237b9bc6aa88433f) )

	/* Micro pre-loader:
	   This was re-created by professor Joao Jose Neto based on his vague
	   recollection of sequences of opcode values from almost 40 years ago :-) */
	ROM_REGION( 0x02a, "micro_pre_loader", 0 )
	ROM_LOAD( "micro-pre-loader.bin", 0x000, 0x02a, CRC(1921feab) SHA1(bb063102e44e9ab963f95b45710141dc2c5046b0) )
ROM_END

} // anonymous namespace

//    YEAR  NAME     PARENT  COMPAT  MACHINE       INPUT         CLASS               INIT               COMPANY                                           FULLNAME         FLAGS
COMP( 1972, patinho, 0,      0,      patinho_feio, patinho_feio, patinho_feio_state, init_patinho_feio, "Escola Politecnica - Universidade de Sao Paulo", "Patinho Feio" , MACHINE_NO_SOUND_HW | MACHINE_NOT_WORKING )
