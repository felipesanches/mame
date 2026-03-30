// license:GPL2+
// copyright-holders:Felipe Sanches
/******************************************************************************

    Technics SX-KN5000 + PC (AT 486) combined driver

    Emulates a KN5000 keyboard with HDAE5000 extension connected to a PC
    running Windows 95 + HD-TechManager5000 via a DB15-to-DB25 parallel
    port cable.  Both machines run in a single MAME instance with the
    parallel ports cross-wired through a null cable device.

******************************************************************************/

#include "emu.h"
#include "kn5000.h"

// PC (AT 486) components
#include "bus/isa/isa_cards.h"
#include "bus/pc_kbd/keyboards.h"
#include "bus/pc_kbd/pc_kbdc.h"
#include "cpu/i386/i386.h"
#include "machine/at.h"
#include "machine/nvram.h"
#include "machine/ram.h"

#include "softlist_dev.h"


namespace {

class kn5000_techmanager_state : public driver_device
{
public:
	kn5000_techmanager_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_kn5000(*this, "kn5000")
		, m_pc_cpu(*this, "pc_cpu")
		, m_pc_mb(*this, "pc_mb")
		, m_pc_ram(*this, "pc_ram")
	{ }

	void kn5000tm(machine_config &config);

private:
	required_device<kn5000_state> m_kn5000;
	required_device<cpu_device> m_pc_cpu;
	required_device<at_mb_device> m_pc_mb;
	required_device<ram_device> m_pc_ram;

	void pc_at32_map(address_map &map) ATTR_COLD;
	void pc_at32_io(address_map &map) ATTR_COLD;
};


// --- PC address maps (same as at486 in at.cpp) ---

void kn5000_techmanager_state::pc_at32_map(address_map &map)
{
	map.unmap_value_high();
	map(0x00000000, 0x0009ffff).bankrw("bank10");
	map(0x000e0000, 0x000fffff).rom().region("pc_bios", 0);
	map(0x00800000, 0x00800bff).ram().share("pc_nvram_data");
	map(0xfffe0000, 0xffffffff).rom().region("pc_bios", 0);
}

void kn5000_techmanager_state::pc_at32_io(address_map &map)
{
	map.unmap_value_high();
	map(0x0000, 0x00ff).m(m_pc_mb, FUNC(at_mb_device::map));
}


// --- Machine configuration ---

void kn5000_techmanager_state::kn5000tm(machine_config &config)
{
	// ===================== KN5000 (Technics keyboard) =====================
	KN5000(config, m_kn5000, 0);

	// ===================== PC (AT 486, for Windows 95 + TechManager5000) ==
	i486_device &pc_cpu(I486(config, m_pc_cpu, 25'000'000));
	pc_cpu.set_addrmap(AS_PROGRAM, &kn5000_techmanager_state::pc_at32_map);
	pc_cpu.set_addrmap(AS_IO, &kn5000_techmanager_state::pc_at32_io);
	pc_cpu.set_irq_acknowledge_callback("pc_mb:pic8259_master", FUNC(pic8259_device::inta_cb));

	AT_MB(config, m_pc_mb).at_softlists(config);
	m_pc_mb->kbd_clk().set("pc_kbd", FUNC(pc_kbdc_device::clock_write_from_mb));
	m_pc_mb->kbd_data().set("pc_kbd", FUNC(pc_kbdc_device::data_write_from_mb));

	NVRAM(config, "pc_nvram", nvram_device::DEFAULT_ALL_0);

	// On-board ISA devices
	ISA16_SLOT(config, "pc_board1", 0, "pc_mb:isabus", pc_isa16_cards, "fdc_smc", true);
	ISA16_SLOT(config, "pc_board2", 0, "pc_mb:isabus", pc_isa16_cards, "comat", true);
	ISA16_SLOT(config, "pc_board3", 0, "pc_mb:isabus", pc_isa16_cards, "ide", true);
	ISA16_SLOT(config, "pc_board4", 0, "pc_mb:isabus", pc_isa16_cards, "lpt", true);

	// ISA expansion cards
	ISA16_SLOT(config, "pc_isa1", 0, "pc_mb:isabus", pc_isa16_cards, "svga_et4k", false);
	ISA16_SLOT(config, "pc_isa2", 0, "pc_mb:isabus", pc_isa16_cards, nullptr, false);
	ISA16_SLOT(config, "pc_isa3", 0, "pc_mb:isabus", pc_isa16_cards, nullptr, false);

	// Keyboard
	pc_kbdc_device &pc_kbdc(PC_KBDC(config, "pc_kbd", pc_at_keyboards, STR_KBD_MICROSOFT_NATURAL));
	pc_kbdc.out_clock_cb().set(m_pc_mb, FUNC(at_mb_device::kbd_clk_w));
	pc_kbdc.out_data_cb().set(m_pc_mb, FUNC(at_mb_device::kbd_data_w));

	// RAM
	RAM(config, m_pc_ram).set_default_size("32M").set_extra_options("4M,8M,16M,64M,128M");

	// TODO: Cross-wire the PC's LPT parallel port (pc_board4:lpt:centronics)
	// to the HDAE5000's centronics port (kn5000:extension:hdae5000:parport)
	// via the kn5000_parport_cable device.
}

ROM_START(kn5000tm)
	// KN5000 ROMs are provided by the kn5000_state sub-device

	// PC BIOS
	ROM_REGION32_LE(0x20000, "pc_bios", 0)
	ROM_SYSTEM_BIOS(0, "at486", "PC/AT 486")
	ROMX_LOAD("at486.bin", 0x10000, 0x10000, CRC(31214616) SHA1(51b41fa44d92151025fc9ad06e518e906935e689), ROM_BIOS(0))
ROM_END

} // anonymous namespace


//   YEAR  NAME      PARENT  COMPAT  MACHINE   INPUT                     STATE                     INIT        COMPANY      FULLNAME                                  FLAGS
CONS(1998, kn5000tm,    0,       0, kn5000tm, kn5000_techmanager_state, kn5000_techmanager_state, empty_init, "Technics", "SX-KN5000 + PC (TechManager5000 Setup)", MACHINE_NOT_WORKING|MACHINE_NO_SOUND)
