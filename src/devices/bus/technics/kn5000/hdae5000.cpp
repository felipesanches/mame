// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, Felipe Sanches
//
// HD-AE5000, Hard Disk & Audio Extension for Technics KN5000 emulation
//
// The HD-AE5000 was an extension board for the Technics KN5000 musical keyboard.
// It provided a hard-disk, additional audio outputs and a serial port to interface
// with a computer to transfer files to/from the hard-drive.
//
// ATA register mapping (from ROM disassembly):
//   0x130010  Data (16-bit)          CS0 register 0
//   0x130012  Error / Features       CS0 register 1
//   0x130014  Sector Count           CS0 register 2
//   0x130016  Sector Number          CS0 register 3
//   0x130018  Cylinder Low           CS0 register 4
//   0x13001A  Cylinder High          CS0 register 5
//   0x13001C  Device / Head          CS0 register 6
//   0x13001E  Status / Command       CS0 register 7
//   0x130020  Alt Status / Dev Ctrl  CS1 register 6

#include "emu.h"
#include "hdae5000.h"

#include "bus/ata/ataintf.h"
#include "bus/ata/hdd.h"
#include "machine/i8255.h"

#define LOG_ATA  (1U << 1)

#define VERBOSE (LOG_ATA)
#include "logmacro.h"

namespace {

class hdae5000_device : public device_t, public device_kn5000_extension_interface
{
public:
	static constexpr feature_type unemulated_features() { return feature::SOUND; }

	hdae5000_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	virtual void program_map(address_space_installer &space) override;

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;

	virtual const tiny_rom_entry *device_rom_region() const override ATTR_COLD;

private:
	required_device<ata_interface_device> m_ata;
	required_device<i8255_device> m_ppi;
	required_memory_region m_rom;
	memory_share_creator<uint16_t> m_ram;

	void card_map(address_map &map) ATTR_COLD;

	uint16_t ata_cs0_r(offs_t offset, uint16_t mem_mask = 0xffff);
	void ata_cs0_w(offs_t offset, uint16_t data, uint16_t mem_mask = 0xffff);
	uint16_t ata_cs1_r(offs_t offset, uint16_t mem_mask = 0xffff);
	void ata_cs1_w(offs_t offset, uint16_t data, uint16_t mem_mask = 0xffff);

	void ata_irq_w(int state);
};

hdae5000_device::hdae5000_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, HDAE5000, tag, owner, clock),
	device_kn5000_extension_interface(mconfig, *this),
	m_ata(*this, "ata"),
	m_ppi(*this, "ppi"),
	m_rom(*this, "rom"),
	m_ram(*this, "ram", 0x80000, ENDIANNESS_LITTLE)
{
}

void hdae5000_device::program_map(address_space_installer &space)
{
	space.install_device(0x000000, 0x2fffff, *this, &hdae5000_device::card_map);
}

void hdae5000_device::card_map(address_map &map)
{
	// ATA CS0 registers at 0x130010-0x13001F (data through status/command)
	map(0x130010, 0x13001f).rw(FUNC(hdae5000_device::ata_cs0_r), FUNC(hdae5000_device::ata_cs0_w));
	// ATA CS1 register at 0x130020-0x130021 (alternate status / device control)
	map(0x130020, 0x130021).rw(FUNC(hdae5000_device::ata_cs1_r), FUNC(hdae5000_device::ata_cs1_w));
	map(0x160000, 0x160007).umask16(0x00ff).rw(m_ppi, FUNC(i8255_device::read), FUNC(i8255_device::write)); // parallel port interface (NEC uPD71055) IC9
	map(0x200000, 0x27ffff).ram().share("ram"); // hsram: 2 * 256k bytes Static RAM @ IC5, IC6 (CS5)
	map(0x280000, 0x2fffff).rom().region(m_rom, 0);
}

/*
PPI pin 2 /CS = CN6 pin 59 PPIFCS
ATA pin 31 INTRQ = CN6 pin 58 HDINT
*/

uint16_t hdae5000_device::ata_cs0_r(offs_t offset, uint16_t mem_mask)
{
	uint16_t data = m_ata->cs0_r(offset, mem_mask);
	LOGMASKED(LOG_ATA, "ATA CS0 read  reg %u: 0x%04X\n", offset, data);
	return data;
}

void hdae5000_device::ata_cs0_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_ATA, "ATA CS0 write reg %u: 0x%04X\n", offset, data);
	m_ata->cs0_w(offset, data, mem_mask);
}

uint16_t hdae5000_device::ata_cs1_r(offs_t offset, uint16_t mem_mask)
{
	// CS1 register 6 = Alternate Status (offset 0 in our mapping corresponds to CS1 reg 6)
	uint16_t data = m_ata->cs1_r(6, mem_mask);
	LOGMASKED(LOG_ATA, "ATA CS1 read  (alt status): 0x%04X\n", data);
	return data;
}

void hdae5000_device::ata_cs1_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	// CS1 register 6 = Device Control
	LOGMASKED(LOG_ATA, "ATA CS1 write (dev ctrl): 0x%04X\n", data);
	m_ata->cs1_w(6, data, mem_mask);
}

void hdae5000_device::ata_irq_w(int state)
{
	LOGMASKED(LOG_ATA, "ATA IRQ: %d\n", state);
	// TODO: route to extension slot IRQ line (HDINT on CN6 pin 58)
}

void hdae5000_device::device_add_mconfig(machine_config &config)
{
	ATA_INTERFACE(config, m_ata).options(ata_devices, "hdd", nullptr, false);
	m_ata->irq_handler().set(FUNC(hdae5000_device::ata_irq_w));

	/* Optional Parallel Port */
	I8255(config, m_ppi); // actual chip is a NEC uPD71055 @ IC9

	// Port A: DB15 connector
	// m_ppi->in_pa_callback().set(FUNC(?_device::ppi_in_a));
	// m_ppi->out_pb_callback().set(FUNC(?_device::ppi_out_b));
	// m_ppi->in_pc_callback().set(FUNC(?_device::ppi_in_c));
	// m_ppi->out_pc_callback().set(FUNC(?_device::ppi_out_c));

	// We may later add this for the auxiliary audio output
	// provided by this extension board:
	// SPEAKER(config, "mono").front_center();
}

void hdae5000_device::device_start()
{
}

void hdae5000_device::device_reset()
{
}

ROM_START(hdae5000)
	ROM_REGION16_LE(0x80000, "rom" , 0)
	ROM_DEFAULT_BIOS("v2.06i")

	ROM_SYSTEM_BIOS(0, "v1.10i", "Version 1.10i - July 6th, 1998")
	ROMX_LOAD("hd-ae5000_v1_10i.ic4", 0x000000, 0x80000, CRC(7461374b) SHA1(6019f3c28b6277730418974dde4dc6893fced00e), ROM_BIOS(0))

	ROM_SYSTEM_BIOS(1, "v1.15i", "Version 1.15i - October 13th, 1998")
	ROMX_LOAD("hd-ae5000_v1_15i.ic4", 0x000000, 0x80000, CRC(e76d4b9f) SHA1(581fa58e2cd6fe381cfc312c73771d25ff2e662c), ROM_BIOS(1))

	// Version 2.01i is described as having "additions like lyrics display etc."
	ROM_SYSTEM_BIOS(2, "v2.01i", "Version 2.01i - January 15th, 1999") // installation file indicated "v2.0i" but signature inside the ROM is "v2.01i"
	ROMX_LOAD("hd-ae5000_v2_01i.ic4", 0x000000, 0x80000, CRC(961e6dcd) SHA1(0160c17baa7b026771872126d8146038a19ef53b), ROM_BIOS(2))

	ROM_SYSTEM_BIOS(3, "v2.06i", "Version 2.06i") // unknown release date
	ROMX_LOAD("hd-ae5000_v2_06i.ic4", 0x000000, 0x80000, CRC(836be80a) SHA1(c4da28f0ad16b1288774af761b3729142e8050b3), ROM_BIOS(3))
ROM_END

const tiny_rom_entry *hdae5000_device::device_rom_region() const
{
	return ROM_NAME(hdae5000);
}

} // anonymous namespace

DEFINE_DEVICE_TYPE_PRIVATE(HDAE5000, device_kn5000_extension_interface, hdae5000_device, "hdae5000", "HD-AE5000, Hard Disk & Audio Extension")
