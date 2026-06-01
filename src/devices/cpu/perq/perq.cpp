// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    Three Rivers PERQ microengine (CPU device)

    This is a port of PERQemu by Josh Dersch (GPL-3.0+):
        https://github.com/skeezicsb/PERQemu
        https://github.com/jdersch/PERQemu

    Phase 0 establishes the device, its control-store address space, the
    I/O-bus dispatch and the on-board subsystems (memory, video, disk).
    The microengine datapath itself (ALU, shifter, call stack, micro-
    instruction decode/execute, from PERQemu's CPU sources) is ported in a later
    phase, at which point execute_run() starts executing microcode.

***************************************************************************/

#include "emu.h"
#include "perq.h"
#include "perqdasm.h"

DEFINE_DEVICE_TYPE(PERQ, perq_cpu_device, "perq", "Three Rivers PERQ")

namespace {

// device_state_interface indices
enum
{
	PERQ_PC = 1,
	PERQ_INT
};

} // anonymous namespace


perq_cpu_device::perq_cpu_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: cpu_device(mconfig, PERQ, tag, owner, clock)
	, m_ucode_config("ucode", ENDIANNESS_BIG, 64, 14, -3, address_map_constructor(FUNC(perq_cpu_device::ucode_map), this))
	, m_iobus_in(*this, 0)
	, m_iobus_out(*this)
	, m_mem_state()
	, m_video(*this)
	, m_disk(*this)
	, m_pc(0)
	, m_interrupt(0)
	, m_icount(0)
{
}

void perq_cpu_device::ucode_map(address_map &map)
{
	// 16K words x 48 bits of writable control store (PERQ 1A)
	map(0x0000, 0x3fff).ram();
}

device_memory_interface::space_config_vector perq_cpu_device::memory_space_config() const
{
	return space_config_vector{
		std::make_pair(AS_PROGRAM, &m_ucode_config)
	};
}

std::unique_ptr<util::disasm_interface> perq_cpu_device::create_disassembler()
{
	return std::make_unique<perq_disassembler>();
}

void perq_cpu_device::device_start()
{
	set_icountptr(m_icount);

	state_add(STATE_GENPC,     "GENPC", m_pc).noshow();
	state_add(STATE_GENPCBASE, "CURPC", m_pc).noshow();
	state_add(PERQ_PC,  "PC",  m_pc);
	state_add(PERQ_INT, "INT", m_interrupt).formatstr("%02X");

	save_item(NAME(m_pc));
	save_item(NAME(m_interrupt));
}

void perq_cpu_device::device_reset()
{
	m_pc = 0;
	m_interrupt = 0;

	m_mem_state.reset();
	m_video.reset();
	m_disk.reset();
}

void perq_cpu_device::execute_run()
{
	do
	{
		m_pc &= 0x3fff;
		debugger_instruction_hook(m_pc);

		// The microengine datapath is not yet ported; for now we simply
		// consume micro-cycles so the rest of the machine can run.
		m_icount--;
	} while (m_icount > 0);
}

void perq_cpu_device::execute_set_input(int inputnum, int state)
{
	if (state != CLEAR_LINE)
		m_interrupt |= (1 << inputnum);
	else
		m_interrupt &= ~(1 << inputnum);
}

u32 perq_cpu_device::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	return m_video.screen_update(screen, bitmap, cliprect);
}

u16 perq_cpu_device::iobus_read(u8 port)
{
	if (port >= 0x65 && port <= 0x67)   // video CRT signals / parity
		return m_video.io_read(port);

	if (port == 0x40)                   // Shugart hard-disk status
		return m_disk.io_read(port);

	// Z80 FIFO, option I/O board, etc. live in the driver
	return m_iobus_in(port);
}

void perq_cpu_device::iobus_write(u8 port, u16 data)
{
	if (port >= 0xe0 && port <= 0xe4)   // video registers
		m_video.io_write(port, data);
	else if (port == 0xc2 || (port >= 0xc8 && port <= 0xcb)
			|| port == 0xd0 || port == 0xd1 || port == 0xd8 || port == 0xd9)
		m_disk.io_write(port, data);    // Shugart register loads
	else
		m_iobus_out(port, data);        // Z80 FIFO, option I/O board, etc.
}
