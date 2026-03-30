// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, Felipe Sanches
//
// HD-AE5000, Hard Disk & Audio Extension for Technics KN5000 emulation
//
// The HD-AE5000 was an extension board for the Technics KN5000 musical keyboard.
// It provided a hard-disk, additional audio outputs and a serial port to interface
// with a computer to transfer files to/from the hard-drive.
//
// HD FORMAT: The firmware requires a safety code to format the hard disk.
// Write protection must be OFF. The code is "0 5 0 3 5 4", entered via
// the soft buttons along the LCD edges which act as a numeric keypad:
//
//   LEFT side:     RIGHT side:
//    1  3  5  7  9
//    0  2  4  6  8
//
// (Source: HD-AE5000 owner's manual)

#include "emu.h"
#include "hdae5000.h"

#include "bus/ata/atadev.h"
#include "bus/ata/ataintf.h"
#include "bus/centronics/ctronics.h"
#include "machine/i8255.h"
#include "machine/input_merger.h"

#define LOG_PPI (1U << 1)

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
	required_device<centronics_device> m_centronics;
	required_device<input_buffer_device> m_cent_status_in;
	required_device<output_latch_device> m_cent_data_out;
	required_memory_region m_rom;
	memory_share_creator<uint16_t> m_ram;

	void card_map(address_map &map) ATTR_COLD;

	void ata_intrq_w(int state);

	// PPI Port B output → centronics control signals
	void ppi_pb_w(uint8_t data);
};

hdae5000_device::hdae5000_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, HDAE5000, tag, owner, clock),
	device_kn5000_extension_interface(mconfig, *this),
	m_ata(*this, "ata"),
	m_ppi(*this, "ppi"),
	m_centronics(*this, "parport"),
	m_cent_status_in(*this, "parport_status"),
	m_cent_data_out(*this, "parport_data_out"),
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
	// ATA IDE at CN2 — 16-bit bus, registers at byte offsets (register N at base + N*2)
	// CS0: registers 0-7 (data, error/features, sector count, LBA, status/command)
	map(0x130010, 0x13001f).rw(m_ata, FUNC(ata_interface_device::cs0_r), FUNC(ata_interface_device::cs0_w));
	// CS1: registers 0-7 (alt status/device control at offset 6 = address 0x13002C)
	map(0x130020, 0x13002f).rw(m_ata, FUNC(ata_interface_device::cs1_r), FUNC(ata_interface_device::cs1_w));
	// PPI (NEC uPD71055 @ IC9) — parallel port interface for PC communication
	map(0x160000, 0x160007).umask16(0x00ff).rw(m_ppi, FUNC(i8255_device::read), FUNC(i8255_device::write));
	// Static RAM: 2 × 256KB (IC5, IC6) — workspace for HD file operations
	map(0x200000, 0x27ffff).ram().share("ram");
	// Extension ROM: 512KB (IC4)
	map(0x280000, 0x2fffff).rom().region(m_rom, 0);
}

/*
PPI pin 2 /CS = CN6 pin 59 PPIFCS
ATA pin 31 INTRQ = CN6 pin 58 HDINT → routed to extension slot IRQ → TLCS900 INT9
*/

void hdae5000_device::ata_intrq_w(int state)
{
	// Forward ATA INTRQ (active high) through extension slot connector to main CPU INT9
	kn5000_extension_connector *connector = downcast<kn5000_extension_connector *>(owner());
	if (connector)
		connector->irq_w(state);
}

// --- PPI Port B → centronics control signals ---
//
// The firmware uses two PPI modes:
//   0x90 = Mode 0, all ports output (initialization)
//   0x89 = Mode 0, Port A output, Port B input, Port C lower input (communication)
//
// During PC communication (PPORT mode), the firmware polls Port C bit 2
// for a connection signal from the PC.  Without a PC connected, this bit
// stays low (0) and the firmware remains in its polling loop until the
// user exits PPORT mode via the menu — it does NOT block boot.

void hdae5000_device::ppi_pb_w(uint8_t data)
{
	LOGMASKED(LOG_PPI, "PPI Port B write: 0x%02X (STROBE=%d AUTOFD=%d INIT=%d SELECT=%d)\n",
		data, BIT(data, 0), BIT(data, 1), BIT(data, 2), BIT(data, 3));
	m_centronics->write_strobe(BIT(data, 0));
	m_centronics->write_autofd(BIT(data, 1));
	m_centronics->write_init(BIT(data, 2));
	m_centronics->write_select_in(BIT(data, 3));
}

void hdae5000_device::device_add_mconfig(machine_config &config)
{
	// No default ATA device — only instantiate HDD when user provides a disk image
	// via -hard. With an empty slot, CS0/CS1 reads return 0xFF (no device),
	// the firmware's DRDY polling times out after ~4M iterations, and boot
	// proceeds to show "Hard disk reset error" (correct for no-disk case).
	// With "hdd" as default, the ATA HLE device's reset/diagnostic sequence
	// makes each status read expensive, turning the 4M-iteration timeout
	// into a multi-minute wall-clock hang at the splash screen.
	ATA_INTERFACE(config, m_ata).options(ata_devices, nullptr, nullptr, false);
	m_ata->irq_handler().set(FUNC(hdae5000_device::ata_intrq_w));

	// PPI (NEC uPD71055, Intel 8255-compatible) @ IC9
	// Wired to a centronics parallel port (DB15 connector on the rear of the board)
	// for communication with a PC running HD-TechManager5000 (ppkn50.dll).
	I8255(config, m_ppi);
	// Port A output → centronics data lines (to PC)
	m_ppi->out_pa_callback().set(m_cent_data_out, FUNC(output_latch_device::write));
	// Port A input ← centronics data lines (from PC, bidirectional)
	m_ppi->in_pa_callback().set("parport_data_in", FUNC(input_buffer_device::read));
	// Port B output → centronics control signals (strobe, autofeed, init, select_in)
	m_ppi->out_pb_callback().set(FUNC(hdae5000_device::ppi_pb_w));
	// Port C input ← centronics status signals (busy, ack, select, fault)
	m_ppi->in_pc_callback().set(m_cent_status_in, FUNC(input_buffer_device::read));

	// DB15 parallel port connector (external, rear of HDAE5000 board)
	// Default: no device connected. For TechManager5000 communication,
	// a "null cable" centronics peripheral bridges to a PC's LPT port.
	CENTRONICS(config, m_centronics, centronics_devices, nullptr);

	// Status signals from connected device → PPI Port C input
	m_centronics->busy_handler().set(m_cent_status_in, FUNC(input_buffer_device::write_bit7));
	m_centronics->ack_handler().set(m_cent_status_in, FUNC(input_buffer_device::write_bit6));
	m_centronics->select_handler().set(m_cent_status_in, FUNC(input_buffer_device::write_bit4));
	m_centronics->fault_handler().set(m_cent_status_in, FUNC(input_buffer_device::write_bit3));

	// Data output latch (PPI Port A → centronics data pins)
	output_latch_device &data_out(OUTPUT_LATCH(config, m_cent_data_out));
	m_centronics->set_output_latch(data_out);

	// Status input buffer (centronics status → PPI Port C)
	INPUT_BUFFER(config, m_cent_status_in);

	// Data input buffer (centronics data → PPI Port A, for bidirectional transfers)
	INPUT_BUFFER(config, "parport_data_in");

	// TODO: auxiliary audio output DAC (extension board has its own audio output)
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
