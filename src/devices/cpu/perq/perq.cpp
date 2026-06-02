// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***************************************************************************

    Three Rivers PERQ microengine (CPU device)

    This is a port of PERQemu by Josh Dersch (GPL-3.0+):
        https://github.com/skeezicsb/PERQemu
        https://github.com/jdersch/PERQemu

    Phase 1 implements the full microengine datapath: the 48-bit
    microinstruction decode, the Execute() micro-cycle, DispatchFunction
    and DispatchJump (the Am2910-style sequencer), the boot-ROM overlay
    and the DDS power-up diagnostic counter.  The cycle-accurate memory
    state machine and the RasterOp pipeline are the Phase 2 boundary, so
    memory fetches are functional/immediate and stores are deferred; the
    first DDS tick (proof of life) happens at the very first instruction,
    before any memory access.

***************************************************************************/

#include "emu.h"
#include "perq.h"
#include "perqdasm.h"

#include <algorithm>

DEFINE_DEVICE_TYPE(PERQ, perq_cpu_device, "perq", "Three Rivers PERQ")

namespace {

// device_state_interface indices
enum { PERQ_PC = 1, PERQ_S, PERQ_INT, PERQ_DDS };

// AField (microword A field) - the AMUX source
enum { AMUX_SHIFTER = 0, AMUX_NEXTOP, AMUX_IOD, AMUX_MDI, AMUX_MDX, AMUX_USTATE, AMUX_XYREG, AMUX_TOS };

// Condition (microword CND field)
enum
{
	CND_TRUE = 0, CND_FALSE, CND_INTRPEND, CND_SPARE, CND_BPC3, CND_CARRY19, CND_ODD, CND_BYTESIGN,
	CND_NEQ, CND_LEQ, CND_LSS, CND_OVF, CND_CARRY15, CND_EQL, CND_GTR, CND_GEQ
};

// JumpOperation (microword JMP field)
enum
{
	JMP_JUMPZERO = 0, JMP_CALL, JMP_NEXTINST, JMP_GOTO, JMP_PUSHLOAD, JMP_CALLS, JMP_VECTOR, JMP_GOTOS,
	JMP_REPEATLOOP, JMP_REPEAT, JMP_RETURN, JMP_JUMPPOP, JMP_LOADS, JMP_LOOP, JMP_NEXT, JMP_THREEWAY
};

// ControlStoreWord (which third of the 48-bit word a WCS write targets)
enum { WCS_LOW = 0, WCS_MIDDLE, WCS_HIGH };

// MulDivCommand (WidRasterOp bits <7:6>)
enum { MDC_OFF = 0, MDC_UDIV, MDC_UMUL, MDC_SMUL };

inline u16 zop_fill(u8 z)
{
	return u16((z & 0x3) | ((z & 0xc0) << 4));
}

} // anonymous namespace


//**************************************************************************
//  Boot ROM
//**************************************************************************

ROM_START( perq_cpu )
	ROM_REGION( 0x1000, "boot", 0 )
	ROM_LOAD( "boot.bin", 0x0000, 0x0d98, CRC(a2b9b7ea) SHA1(75b4b7743e4e65fc14b9f3dbb08421c16cde0f11) )

	// memory-board state-machine lookup ROM (bkm16emu)
	ROM_REGION( 0x0100, "bkm", 0 )
	ROM_LOAD( "bkm16emu.rom", 0x0000, 0x0100, CRC(88c33732) SHA1(70fb0ce24f78dd8b5078b19394443de30ef6c4bf) )
ROM_END

const tiny_rom_entry *perq_cpu_device::device_rom_region() const
{
	return ROM_NAME( perq_cpu );
}


//**************************************************************************
//  Construction / configuration
//**************************************************************************

perq_cpu_device::perq_cpu_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: cpu_device(mconfig, PERQ, tag, owner, clock)
	, m_ucode_config("ucode", ENDIANNESS_BIG, 64, 14, -3, address_map_constructor(FUNC(perq_cpu_device::ucode_map), this))
	, m_ucode(nullptr)
	, m_iobus_in(*this, 0)
	, m_iobus_out(*this)
	, m_dds_cb(*this)
	, m_mem_state()
	, m_video(*this)
	, m_disk(*this)
	, m_stack_pointer(0)
	, m_bpc(0)
	, m_dds(0)
	, m_iod(0)
	, m_victim(0xffff)
	, m_register_base(0)
	, m_mq(0)
	, m_mq_enabled(false)
	, m_last_bmux(0)
	, m_increment_bpc(false)
	, m_wcs_hold(false)
	, m_rom_enabled(true)
	, m_interrupt(0)
	, m_icount(0)
{
	std::fill(std::begin(m_r), std::end(m_r), 0);
	std::fill(std::begin(m_estack), std::end(m_estack), 0);
	std::fill(std::begin(m_rom), std::end(m_rom), 0);
	m_old_alu = perq_alu::regs{ 0, 0, false, false, false, false, false, false, false, false };
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


//**************************************************************************
//  device_t
//**************************************************************************

void perq_cpu_device::device_start()
{
	m_ucode = &space(AS_PROGRAM);

	load_boot_rom();

	if (memory_region *const r = memregion("bkm"))
		m_mem_state.load_bookmark_rom(r->base(), r->bytes());
	else
		logerror("PERQ: memory bookmark ROM region missing\n");

	set_icountptr(m_icount);

	state_add(STATE_GENPC,     "GENPC", m_genpc).noshow();
	state_add(STATE_GENPCBASE, "CURPC", m_genpc).noshow();
	state_add(PERQ_PC,  "PC",  m_genpc);
	state_add(PERQ_S,   "S",   m_gens);
	state_add(PERQ_INT, "INT", m_interrupt).formatstr("%02X");
	state_add(PERQ_DDS, "DDS", m_dds);

	save_item(NAME(m_r));
	save_item(NAME(m_estack));
	save_item(NAME(m_stack_pointer));
	save_item(NAME(m_bpc));
	save_item(NAME(m_dds));
	save_item(NAME(m_iod));
	save_item(NAME(m_victim));
	save_item(NAME(m_register_base));
	save_item(NAME(m_mq));
	save_item(NAME(m_mq_enabled));
	save_item(NAME(m_last_bmux));
	save_item(NAME(m_increment_bpc));
	save_item(NAME(m_wcs_hold));
	save_item(NAME(m_rom_enabled));
	save_item(NAME(m_muldiv_inst));
	save_item(NAME(m_interrupt));

	// Shugart hard-disk controller state + timers
	save_item(NAME(m_disk.m_controller_status));
	save_item(NAME(m_disk.m_track_zero));
	save_item(NAME(m_disk.m_drive_fault));
	save_item(NAME(m_disk.m_seek_complete));
	save_item(NAME(m_disk.m_unit_ready));
	save_item(NAME(m_disk.m_index));
	save_item(NAME(m_disk.m_cylinder));
	save_item(NAME(m_disk.m_phys_cylinder));
	save_item(NAME(m_disk.m_head));
	save_item(NAME(m_disk.m_sector));
	save_item(NAME(m_disk.m_serial_low));
	save_item(NAME(m_disk.m_serial_high));
	save_item(NAME(m_disk.m_block_number));
	save_item(NAME(m_disk.m_header_addr_low));
	save_item(NAME(m_disk.m_header_addr_high));
	save_item(NAME(m_disk.m_data_buffer_low));
	save_item(NAME(m_disk.m_data_buffer_high));
	save_item(NAME(m_disk.m_seek_state));
	save_item(NAME(m_disk.m_seek_data));

	// video controller state
	save_item(NAME(m_video.m_display_addr));
	save_item(NAME(m_video.m_cursor_addr));
	save_item(NAME(m_video.m_cursor_x));
	save_item(NAME(m_video.m_cursor_y));
	save_item(NAME(m_video.m_cursor_func));
	save_item(NAME(m_video.m_video_status));
	save_item(NAME(m_video.m_line_counter));
	save_item(NAME(m_video.m_line_counter_init));
	save_item(NAME(m_video.m_line_count_overflow));
	save_item(NAME(m_video.m_scanline));

	m_disk_busy_timer  = timer_alloc(FUNC(perq_cpu_device::disk_busy_done), this);
	m_disk_index_timer = timer_alloc(FUNC(perq_cpu_device::disk_index_edge), this);
	m_disk_index_timer->adjust(attotime::from_usec(20000));   // first index pulse after one revolution

	// free-running scanline timer driving the video line counter
	m_video_line_timer = timer_alloc(FUNC(perq_cpu_device::video_line_tick), this);
	m_video_line_timer->adjust(attotime::from_ticks(perq_video::CPU_CLOCKS_PER_LINE, clock()));
}

TIMER_CALLBACK_MEMBER(perq_cpu_device::disk_busy_done)  { m_disk.on_busy_done(); }
TIMER_CALLBACK_MEMBER(perq_cpu_device::disk_index_edge) { m_disk.on_index_edge(); }

TIMER_CALLBACK_MEMBER(perq_cpu_device::video_line_tick)
{
	m_video.line_tick();
	m_video_line_timer->adjust(attotime::from_ticks(perq_video::CPU_CLOCKS_PER_LINE, clock()));
}

void perq_cpu_device::device_reset()
{
	m_rom_enabled  = true;
	m_interrupt    = 0;
	m_dds          = 0;
	m_iod          = 0;
	m_bpc          = 0;
	m_last_bmux    = 0;
	m_wcs_hold     = false;
	m_increment_bpc = false;
	m_victim       = 0xffff;
	m_stack_pointer = 0;
	m_mq           = 0;
	m_register_base = 0;
	m_mq_enabled   = false;
	m_muldiv_inst  = MDC_OFF;
	m_genpc        = 0;
	m_gens         = 0;

	m_pc.reset();
	m_s.reset();

	m_alu.reset();
	m_old_alu = perq_alu::regs{ 0, 0, false, false, false, false, false, false, false, false };
	m_shifter.reset();
	m_mq_shifter.reset();
	m_cstack.clear();
	m_cstack.reset();

	std::fill(std::begin(m_r), std::end(m_r), 0);
	std::fill(std::begin(m_estack), std::end(m_estack), 0);

	m_mem_state.reset();
	m_video.reset();
	m_disk.reset();

	m_dds_cb(0);   // the boot/reset switch clears the front-panel display to 000
}

void perq_cpu_device::load_boot_rom()
{
	std::fill(std::begin(m_rom), std::end(m_rom), 0);

	memory_region *const rgn = memregion("boot");
	if (rgn == nullptr)
	{
		logerror("PERQ: boot ROM region missing\n");
		return;
	}

	// boot.bin is a list of 8-byte records: 2 little-endian address bytes
	// followed by a 6-byte little-endian 48-bit microword
	const u8 *const base = rgn->base();
	const u32 len = rgn->bytes();
	for (u32 ofs = 0; ofs + 8 <= len; ofs += 8)
	{
		const u16 addr = base[ofs] | (base[ofs + 1] << 8);
		if (addr == 0xffff)
			break;      // end-of-data terminator: everything past here is region zero-fill
		if (addr >= 512)
			continue;   // out-of-range record (defensive)

		m_rom[addr] = u64(base[ofs + 2]) | (u64(base[ofs + 3]) << 8) | (u64(base[ofs + 4]) << 16)
				| (u64(base[ofs + 5]) << 24) | (u64(base[ofs + 6]) << 32) | (u64(base[ofs + 7]) << 40);
	}

	// mirror the boot ROM into the low control store so the debugger can
	// disassemble it (execution still fetches from m_rom while the overlay
	// is enabled)
	for (int i = 0; i < 512; i++)
		m_ucode->write_qword(i, m_rom[i]);
}


//**************************************************************************
//  Instruction fetch / decode
//**************************************************************************

u64 perq_cpu_device::fetch_microword(u16 addr)
{
	addr &= 0x3fff;
	if (m_rom_enabled && addr < 0x200)
		return m_rom[addr];

	// The control store is a qword-granular space (data width 64, addr-shift -3):
	// read_qword()/write_qword() take the NATIVE control-store word address and apply
	// the shift internally.  address_to_byte() must NOT be used here -- it would
	// double-apply the shift (addr<<3), overflowing the 14-bit space and wrapping every
	// address >= 0x800 onto a low one (e.g. 0xff1 aliasing 0x7f1), which silently
	// corrupted CkMic's own NextData word during the interpreter load.
	return m_ucode->read_qword(addr) & 0x0000'ffff'ffff'ffffULL;
}

perq_cpu_device::microinstruction perq_cpu_device::decode(u16 addr)
{
	microinstruction u;
	u.pc = addr;
	u.ucode = fetch_microword(addr);

	const u64 w = u.ucode;
	u.x   = (w >> 40) & 0xff;
	u.y   = (w >> 32) & 0xff;
	u.a   = (w >> 29) & 0x07;
	u.b   = (w >> 28) & 0x01;
	u.w   = (w >> 27) & 0x01;
	u.h   = (w >> 26) & 0x01;
	u.alu = (w >> 22) & 0x0f;
	u.f   = (w >> 20) & 0x03;
	u.sf  = (w >> 16) & 0x0f;
	u.z   = (w >> 8)  & 0xff;
	u.cnd = (w >> 4)  & 0x0f;
	u.jmp =  w        & 0x0f;

	u.not_z         = (~u.z) & 0xff;
	u.long_constant = (u.z << 8) | u.y;
	u.is_io_input   = (u.z & 0x80) == 0;
	u.io_port       = (u.z & 0x80) | (u.not_z & 0x7f);
	u.vector_dispatch_address = zop_fill(u.not_z) | ((u.not_z & 0x3c) << 4);
	u.is_special_function = (u.f == 0 || u.f == 2);
	u.bmux_input    = (u.is_special_function && u.sf == 0) ? u.long_constant : u.y;
	u.want_mdi      = (u.a == AMUX_MDI || u.a == AMUX_MDX);
	u.memory_request = (u.f == 1 && u.sf >= 0x8) ? u.sf : 0;

	switch (u.f)
	{
	case 0:
		u.next_address = (addr & 0xf00) | u.not_z;
		break;
	case 1:
		if (u.sf == 0x7)   // Leap (16K): full 14-bit target
			u.next_address = (u.not_z | ((0xff & (~u.y)) << 8)) & 0x3fff;
		else
			u.next_address = (addr & 0xf00) | u.not_z;
		break;
	case 3:
		u.next_address = 0xfff & (~(u.z | (u.sf << 8)));
		break;
	default:
		u.next_address = 0;
		break;
	}

	return u;
}


//**************************************************************************
//  Datapath helpers
//**************************************************************************

u16 perq_cpu_device::microstate_register()
{
	const perq_alu::regs &r = m_alu.registers();
	return u16(bpc()
			| (r.ovf ? 0x0010 : 0)
			| (r.eql ? 0x0020 : 0)
			| (r.cry ? 0x0040 : 0)
			| (r.lss ? 0x0080 : 0)
			| (m_stack_pointer != 0 ? 0x0200 : 0)
			| ((((~m_last_bmux) >> 16) & 0xf) << 12));
}

u32 perq_cpu_device::get_amux(microinstruction &uop)
{
	switch (uop.a)
	{
	case AMUX_SHIFTER:
		m_shifter.shift(m_old_alu.r);
		return m_shifter.output();

	case AMUX_NEXTOP:
		if (op_file_empty() && m_victim == 0xffff)
			m_victim = m_pc.value();
		m_increment_bpc = true;
		return m_mem_state.op_file(bpc());

	case AMUX_IOD:
		return m_iod;

	case AMUX_MDI:
		return m_mem_state.mdi();

	case AMUX_MDX:
		return (m_mem_state.mdi() & 0xf) << 16;

	case AMUX_USTATE:
		return microstate_register();

	case AMUX_XYREG:
		return (uop.x < 0x40) ? m_r[uop.x | m_register_base] : m_r[uop.x];

	case AMUX_TOS:
		return m_estack[m_stack_pointer];
	}

	return 0;
}

void perq_cpu_device::do_writeback(const microinstruction &uop)
{
	if (uop.w != 1)
		return;

	if (uop.x < 0x40)
		m_r[uop.x | m_register_base] = m_alu.registers().r;
	else
		m_r[uop.x] = m_alu.registers().r;
}

void perq_cpu_device::do_muldiv_alu_op(u32 amux, u32 bmux, u16 mq, u8 op)
{
	u8 mod_op = op;

	if (op == perq_alu::OP_APLUSB || op == perq_alu::OP_AMINUSB)
	{
		switch (m_muldiv_inst)
		{
		case MDC_UDIV:
		{
			const u32 bit = (mq & 0x8000) >> 15;   // MQ<15> from last cycle
			amux = (amux & 0xffffe) | bit;
			mod_op = (m_old_alu.r & 0x8000) ? perq_alu::OP_APLUSB : perq_alu::OP_AMINUSB;
			break;
		}
		case MDC_UMUL:
		case MDC_SMUL:
			mod_op = (mq & 0x1) ? perq_alu::OP_APLUSB : perq_alu::OP_A;
			break;
		default:
			break;
		}
	}

	m_alu.do_op(amux, bmux, mod_op);
}


//**************************************************************************
//  Expression stack + DDS
//**************************************************************************

void perq_cpu_device::stack_reset()
{
	m_stack_pointer = 0;
	increment_dds();   // resetting the EStack bumps the diagnostic display
}

void perq_cpu_device::stack_push(u32 value)
{
	m_stack_pointer++;
	if (m_stack_pointer > 15)
		m_stack_pointer = 0;   // wraps (microcode depends on it)
	m_estack[m_stack_pointer] = value;
}

void perq_cpu_device::stack_pop()
{
	m_stack_pointer--;
	if (m_stack_pointer < 0)
		m_stack_pointer = 15;
}

void perq_cpu_device::increment_dds()
{
	m_dds++;
	m_dds_cb(m_dds % 1000);   // update the front-panel diagnostic display
}


//**************************************************************************
//  Writable control store
//**************************************************************************

u64 perq_cpu_device::unscramble_control_store_word(u64 current, int which, u16 data)
{
	// the control-store outputs are active-low
	data = u16(~data);

	static const int low_bits[16]    = { 24, 25, 22, 27, 23, 29, 30, 31,  8,  9, 10, 11, 12, 13, 14, 15 };
	static const int middle_bits[16] = { 16, 17, 18, 19, 20, 21, 26, 28,  0,  1,  2,  3,  4,  5,  6,  7 };

	const int *dest = nullptr;
	switch (which)
	{
	case WCS_LOW:    dest = low_bits; break;
	case WCS_MIDDLE: dest = middle_bits; break;
	case WCS_HIGH:
		return (current & 0x0000'ffff'ffffULL) | (u64(data) << 32);
	}

	for (int i = 0; i < 16; i++)
	{
		const u64 bit = (data >> i) & 1;
		current = (current & ~(u64(1) << dest[i])) | (bit << dest[i]);
	}
	return current;
}

void perq_cpu_device::write_control_store(int which, u16 data)
{
	const u16 addr = m_s.value();
	u64 current = m_ucode->read_qword(addr) & 0x0000'ffff'ffff'ffffULL;
	current = unscramble_control_store_word(current, which, data);
	m_ucode->write_qword(addr, current);
	m_wcs_hold = true;   // one-cycle wait state
}


//**************************************************************************
//  DispatchFunction
//**************************************************************************

void perq_cpu_device::dispatch_function(microinstruction &uop)
{
	switch (uop.f)
	{
	case 0:
	case 2:
		if (uop.f == 2)
			m_shifter.set_command(uop.z);   // ShiftOnZ

		switch (uop.sf)
		{
		case 0x0:   // LongConstant - injected on the BMUX, nothing to do here
			break;

		case 0x1:   // ShiftOnR (only under F==0)
			if (uop.f == 0)
				m_shifter.set_command(~int(m_alu.registers().r));
			break;

		case 0x2:   // StackReset (bumps DDS)
			stack_reset();
			break;

		case 0x3:   // TOS := R
			m_estack[m_stack_pointer] = m_alu.registers().r;
			break;

		case 0x4:   // Push
			stack_push(m_alu.registers().r);
			break;

		case 0x5:   // Pop
			stack_pop();
			break;

		case 0x6:   // CntlRasterOp := Z   (Phase 2)
		case 0x7:   // SrcRasterOp := R    (Phase 2)
		case 0x8:   // DstRasterOp := R    (Phase 2)
			break;

		case 0x9:   // WidRasterOp := R (RasterOp Phase 2; bits <7:6> drive MulDiv)
		{
			m_muldiv_inst = (m_alu.registers().r & 0xc0) >> 6;
			switch (m_muldiv_inst)
			{
			case MDC_OFF:
				m_mq_enabled = false;
				break;
			case MDC_UDIV:
				m_mq_shifter.set_command(u8(perq_shifter::LEFT_SHIFT), 1, 0);
				m_mq_enabled = true;
				break;
			case MDC_UMUL:
			case MDC_SMUL:
				m_mq_shifter.set_command(u8(perq_shifter::RIGHT_SHIFT), 1, 0);
				m_mq_enabled = true;
				break;
			}
			break;
		}

		case 0xa:   // LoadOp - refill the OpFile and disable the boot ROM overlay
			m_mem_state.load_op_file();
			if (m_rom_enabled)
				m_rom_enabled = false;
			break;

		case 0xb:   // BPC := R
			m_bpc = m_alu.registers().r & 0xf;
			break;

		case 0xc:   // WCSL
			write_control_store(WCS_LOW, u16(m_old_alu.r));
			break;
		case 0xd:   // WCSM
			write_control_store(WCS_MIDDLE, u16(m_old_alu.r));
			break;
		case 0xe:   // WCSH
			write_control_store(WCS_HIGH, u16(m_old_alu.r));
			break;

		case 0xf:   // IOB function (only under F==0)
			if (uop.f == 0)
			{
				if (uop.is_io_input)
					m_iod = iobus_read(uop.io_port);
				else
					iobus_write(uop.io_port, m_alu.registers().r & 0xffff);
			}
			break;
		}
		break;

	case 1:   // Store / extended functions
		switch (uop.sf)
		{
		case 0x0:   // (R) := Victim Latch
			m_alu.set_r(m_victim);
			do_writeback(uop);
			m_victim = 0xffff;
			break;

		case 0x1:   // Multiply / DivideStep (low/MQ half)
			if (m_muldiv_inst == MDC_UDIV)
			{
				if (uop.alu == perq_alu::OP_APLUSB || uop.alu == perq_alu::OP_AMINUSB)
				{
					m_mq_shifter.shift(m_mq);
					m_mq = m_mq_shifter.output() | ((~((m_alu.registers().r & 0x8000) >> 15)) & 0x1);
				}
			}
			else if (m_muldiv_inst == MDC_UMUL || m_muldiv_inst == MDC_SMUL)
			{
				m_mq_shifter.shift(m_mq);
				m_mq = m_mq_shifter.output() | ((m_alu.registers().r & 0x1) << 15);
			}
			break;

		case 0x2:   // Load multiplier / dividend
			m_mq = m_alu.registers().r & 0xffff;
			break;

		case 0x3:   // Load base register
			m_register_base = u8(~int(m_alu.registers().r));
			break;

		case 0x4:   // (R) := product / quotient
			m_alu.set_r(m_mq & 0xffff);
			do_writeback(uop);
			break;

		case 0x5:   // Push long constant
			stack_push(uop.long_constant);
			break;

		case 0x6:   // (MA) := Shift  - compute next micro-address from the shifter
			m_shifter.shift(m_old_alu.r);
			uop.next_address = m_shifter.output() & 0x3fff;
			break;

		case 0x7:   // Leap address generation - target precomputed in next_address
			break;

		case 0x8: case 0x9: case 0xa: case 0xb:
		case 0xc: case 0xd: case 0xe: case 0xf:
			// Fetch/Store: issue the cycle at MA := R.  The memory state
			// machine routes fetch vs store and drives MDI/MDO over the
			// following T-states.
			m_mem_state.request_cycle(int(m_alu.registers().r & 0xfffff), uop.memory_request);
			break;
		}
		break;

	case 3:   // long jump - handled entirely in dispatch_jump
		break;
	}
}


//**************************************************************************
//  DispatchJump (the Am2910-style sequencer)
//**************************************************************************

bool perq_cpu_device::condition_satisfied(u8 cnd)
{
	switch (cnd)
	{
	case CND_TRUE:     return true;
	case CND_FALSE:    return false;
	case CND_INTRPEND: return m_interrupt != 0;
	case CND_BPC3:     return op_file_empty();
	case CND_CARRY19:  return m_old_alu.carry19 == 0;
	case CND_ODD:      return (m_old_alu.r & 0x1) != 0;
	case CND_BYTESIGN: return (m_old_alu.r & 0x80) != 0;
	case CND_NEQ:      return m_old_alu.neq;
	case CND_LEQ:      return m_old_alu.leq;
	case CND_LSS:      return m_old_alu.lss;
	case CND_OVF:      return m_old_alu.ovf;
	case CND_CARRY15:  return m_old_alu.cry;
	case CND_EQL:      return m_old_alu.eql;
	case CND_GTR:      return m_old_alu.gtr;
	case CND_GEQ:      return m_old_alu.geq;
	default:           return false;   // Spare (0x3): unimplemented
	}
}

int perq_cpu_device::interrupt_priority()
{
	for (int i = 7; i >= 0; i--)
		if (BIT(m_interrupt, i))
			return i;
	return 0;
}

void perq_cpu_device::dispatch_jump(const microinstruction &uop)
{
	const bool cond = condition_satisfied(uop.cnd);
	const u16 na = uop.next_address;

	switch (uop.jmp)
	{
	case JMP_JUMPZERO:
		m_pc.set_value(0);
		m_cstack.reset();
		break;

	case JMP_CALL:
		if (cond)
		{
			m_cstack.push_full(u16(m_pc.value() + 1));
			m_pc.set_value(na);
		}
		else
			m_pc.inc_lo();
		break;

	case JMP_NEXTINST:
		if (uop.h == 0)
		{
			// DoNextInst: q-code dispatch from the OpFile
			const u8 next = m_mem_state.op_file(bpc());
			m_pc.set_lo(zop_fill(uop.not_z) | ((~next & 0xff) << 2));
			m_increment_bpc = true;
		}
		else
		{
			if (m_victim != 0xffff)
			{
				m_pc.set_value(m_victim);
				m_victim = 0xffff;
			}
			else
				logerror("PERQ: revive from unset victim latch at %04x\n", uop.pc);
		}
		break;

	case JMP_GOTO:
		if (cond) m_pc.set_value(na); else m_pc.inc_lo();
		break;

	case JMP_PUSHLOAD:
		if (cond)
			m_s.set_value(na);
		m_pc.inc_lo();
		m_cstack.push_lo(m_pc.lo());
		break;

	case JMP_CALLS:
		m_cstack.push_full(u16(m_pc.value() + 1));
		m_pc.set_value(cond ? na : m_s.value());
		break;

	case JMP_VECTOR:
		if (cond)
		{
			if (uop.h == 0)   // Vector
				m_pc.set_lo((uop.vector_dispatch_address & 0xffc3) | (interrupt_priority() << 2));
			else              // Dispatch
			{
				m_shifter.shift(m_old_alu.r);
				m_pc.set_lo((uop.vector_dispatch_address & 0xffc3) | (((~m_shifter.output()) & 0xf) << 2));
			}
		}
		else
			m_pc.inc_lo();
		break;

	case JMP_GOTOS:
		m_pc.set_value(cond ? na : m_s.value());
		break;

	case JMP_REPEATLOOP:
		if (m_s.lo() != 0) { m_pc.set_lo(m_cstack.top_lo()); m_s.dec_lo(); }
		else               { m_pc.inc_lo(); m_cstack.pop_lo(); }
		break;

	case JMP_REPEAT:
		if (m_s.lo() != 0) { m_pc.set_lo(na); m_s.dec_lo(); }
		else                 m_pc.inc_lo();
		break;

	case JMP_RETURN:
		if (cond) m_pc.set_value(m_cstack.pop_full()); else m_pc.inc_lo();
		break;

	case JMP_JUMPPOP:
		if (cond)
		{
			if (uop.h != 0) m_cstack.pop_full();   // LeapPop
			else            m_cstack.pop_lo();
			m_pc.set_value(na);
		}
		else
			m_pc.inc_lo();
		break;

	case JMP_LOADS:
		m_s.set_value(na);
		m_pc.inc_lo();
		break;

	case JMP_LOOP:
		if (cond) { m_pc.inc_lo(); m_cstack.pop_lo(); }
		else        m_pc.set_lo(m_cstack.top_lo());
		break;

	case JMP_NEXT:
		m_pc.inc_lo();
		break;

	case JMP_THREEWAY:
		if (cond)
		{
			m_pc.inc_lo();
			m_cstack.pop_lo();
			if (m_s.lo() != 0) m_s.dec_lo();
		}
		else
		{
			if (m_s.lo() != 0) { m_pc.set_lo(m_cstack.top_lo()); m_s.dec_lo(); }
			else               { m_pc.set_value(na); m_cstack.pop_lo(); }
		}
		break;
	}
}


//**************************************************************************
//  Execution
//**************************************************************************

void perq_cpu_device::execute_run()
{
	do
	{
		const u16 pc = m_pc.value();
		m_genpc = pc;
		m_gens = m_s.value();

		debugger_instruction_hook(pc);

		microinstruction uop = decode(pc);

		// clock the memory state machine at the top of the cycle, before the
		// abort test - aborts still advance the T-state so a pending request
		// lands in its correct slot
		m_mem_state.tick(uop.memory_request);

		const bool abort = m_wcs_hold
				|| m_mem_state.wait()
				|| (uop.want_mdi && !m_mem_state.mdi_valid());
		if (abort)
		{
			m_wcs_hold = false;
			m_icount--;
			continue;
		}

		if (m_increment_bpc)
		{
			m_bpc++;
			m_increment_bpc = false;
		}

		// latch the previous micro-op's ALU flags (conditional jumps read these)
		m_old_alu = m_alu.registers();

		// BMUX
		u32 bmux;
		if (uop.b == 0)
			bmux = (uop.y < 0x40) ? m_r[uop.y | m_register_base] : m_r[uop.y];
		else
			bmux = uop.bmux_input;
		m_last_bmux = bmux;

		// AMUX (has side effects: shifter, victim latch)
		const u32 amux = get_amux(uop);

		// ALU
		if (m_mq_enabled)
			do_muldiv_alu_op(amux, bmux, m_mq, uop.alu);
		else
			m_alu.do_op(amux, bmux, uop.alu);

		do_writeback(uop);

		// store half-cycle: commit a pending store (a RasterOp result would
		// supersede the ALU here, but RasterOp is not yet wired)
		if (m_mem_state.mdo_needed())
			m_mem_state.tock(u16(m_alu.registers().r));

		dispatch_function(uop);
		dispatch_jump(uop);

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


//**************************************************************************
//  Display + I/O bus
//**************************************************************************

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
	else if (port == 0xc1)
	{
		// 0xC1 is shared: the Shugart command register AND the Z80 on/off
		// control register, so both consumers see the write
		m_disk.io_write(port, data);
		m_iobus_out(port, data);        // driver: Z80 I/O board on/off
	}
	else if (port == 0xc2 || (port >= 0xc8 && port <= 0xcb)
			|| port == 0xd0 || port == 0xd1 || port == 0xd8 || port == 0xd9)
		m_disk.io_write(port, data);    // Shugart register loads
	else
		m_iobus_out(port, data);        // Z80 FIFO, option I/O board, etc.
}
