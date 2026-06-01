// license:GPL-3.0+
// copyright-holders:Josh Dersch, Felipe Sanches
/***********************************************************************************************************************************

    Three Rivers Computer Corporation PERQ 1A

    The PERQ is a microcoded bit-slice workstation: a discrete AM2910 microsequencer drives a 48-bit horizontal microword out of
    a 16K writable control store, with 74S181 ALUs forming a 20-bit ALU.  A Z80-based I/O board (IOB) handles most peripheral I/O
    (floppy, serial, GPIB, keyboard) and the hard-disk seek logic.  The display is a portrait 768x1024 black-on-white bitmap.

    This driver is a port of PERQemu by Josh Dersch (GPL-3.0+):
        https://github.com/skeezicsb/PERQemu
        https://github.com/jdersch/PERQemu

    The PERQ microengine, main memory, RasterOp, video controller and Shugart hard-disk controller live in the perq_cpu_device
    (src/devices/cpu/perq), following MAME's Xerox Alto precedent.  This file wires that CPU device to the display and to the
    real Z80 I/O board (a Z80 running the dumped pz80.bin firmware with Z80 SIO/CTC/DMA peripherals and a uPD765 floppy
    controller).  Phase 0: everything is wired and the machine launches; the subsystems are progressively filled in.

************************************************************************************************************************************/

#include "emu.h"

#include "cpu/perq/perq.h"

#include "cpu/z80/z80.h"
#include "machine/z80daisy.h"
#include "machine/z80ctc.h"
#include "machine/z80sio.h"
#include "machine/z80dma.h"
#include "machine/upd765.h"
#include "imagedev/floppy.h"
#include "imagedev/harddriv.h"

#include "emupal.h"
#include "screen.h"

#include "perq1a.lh"

#include <queue>


namespace {

class perq_state : public driver_device
{
public:
	perq_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_iob(*this, "iob")
		, m_ctc(*this, "ctc")
		, m_sio(*this, "sio")
		, m_dma(*this, "dma")
		, m_fdc(*this, "fdc")
		, m_dds_digits(*this, "digit%u", 0U)
	{ }

	void perq1a(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

private:
	// front-panel DDS readout: decode the 0..999 value to three 7-seg digits
	void dds_w(u16 data);

	// PERQ main-CPU I/O bus (ports not handled inside the CPU device land here)
	u16  iobus_r(offs_t port);
	void iobus_w(offs_t port, u16 data);

	// Z80 I/O board ports
	u8   perq_r_fifo();              // 0xA0: PERQ -> Z80 input FIFO
	void perq_w_fifo(u8 data);       // 0xD0: Z80 -> PERQ output FIFO
	u8   kbd_r();                    // 0x80: keyboard
	u8   ioreg1_r();                 // 0x88: Z80 -> PERQ FIFO status
	void ioreg3_w(u8 data);          // 0xC8: DMA select + interrupt enables
	void disk_seek_w(u8 data);       // 0xD8: hard-disk seek pulse
	u8   gpib_r(offs_t offset);      // 0xB8-0xBF: GPIB (TMS9914A), stubbed
	void gpib_w(offs_t offset, u8 data);
	void z80ctl_w(u8 data);          // 0xC0: Z80 control-bus reset latch
	void fdc_irq_w(int state);       // uPD765 INT line

	// drive the Z80 /INT from the IOB's soft interrupt sources (the FIFO and
	// the uPD765), picking the highest-priority active one's IM2 vector
	void update_z80_int();

	void iob_mem_map(address_map &map) ATTR_COLD;
	void iob_io_map(address_map &map) ATTR_COLD;

	static void floppy_formats(format_registration &fr);

	required_device<perq_cpu_device> m_maincpu;
	required_device<z80_device>      m_iob;
	required_device<z80ctc_device>   m_ctc;
	required_device<z80sio_device>   m_sio;
	required_device<z80dma_device>   m_dma;
	required_device<upd765a_device>  m_fdc;
	output_finder<3>                 m_dds_digits;

	// PERQ <-> Z80 communication FIFOs and their handshake/interrupt state
	std::queue<u8> m_z80_to_perq;      // Z80 writes 0xD0 -> PERQ reads 0x46 (IRQ_Z80_DATA_OUT)
	std::queue<u8> m_perq_to_z80;      // PERQ writes 0xC7 -> Z80 reads 0xA0 (Z80 INT, vector 0x20)

	bool m_z80_running = false;        // Z80 held in reset until the PERQ turns it on (port 0xC1)
	bool m_z80_int_enabled = false;    // IOREG3 bit 2 (PRQENB): gate the PERQ->Z80 FIFO IRQ to the Z80
	bool m_z80_data_in_req = false;    // 0xC7 bit 8: raise IRQ_Z80_DATA_IN once the Z80 drains the FIFO
	bool m_flp_int_enabled = false;    // IOREG3 bit 0 (FLPENB): gate the uPD765 IRQ to the Z80
	bool m_fdc_irq = false;            // latched uPD765 INT line
	u8   m_dma_select = 0;             // IOREG3 bits 7:5 (DMA device select; used in the floppy phase)
};


void perq_state::machine_start()
{
	m_dds_digits.resolve();
	dds_w(0);   // the DDS reads 000 out of reset
}

void perq_state::machine_reset()
{
	std::queue<u8>().swap(m_z80_to_perq);
	std::queue<u8>().swap(m_perq_to_z80);

	m_z80_running     = false;
	m_z80_int_enabled = false;
	m_z80_data_in_req = false;
	m_flp_int_enabled = false;
	m_fdc_irq         = false;
	m_dma_select      = 0;

	// the IOB Z80 stays in reset until the boot microcode turns it on (port 0xC1)
	m_iob->set_input_line(INPUT_LINE_RESET, ASSERT_LINE);
	update_z80_int();
}

void perq_state::dds_w(u16 data)
{
	static const u8 led_map[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };

	data %= 1000;
	m_dds_digits[0] = led_map[data % 10];          // units
	m_dds_digits[1] = led_map[(data / 10) % 10];   // tens
	m_dds_digits[2] = led_map[(data / 100) % 10];  // hundreds
}


//**************************************************************************
//  PERQ main-CPU I/O bus
//**************************************************************************

u16 perq_state::iobus_r(offs_t port)
{
	switch (port)
	{
	case 0x46:  // read Z80 -> PERQ FIFO; clear the data-ready IRQ once drained
		if (!m_z80_to_perq.empty())
		{
			const u8 v = m_z80_to_perq.front();
			m_z80_to_perq.pop();
			if (m_z80_to_perq.empty())
				m_maincpu->clear_interrupt(perq_cpu_device::IRQ_Z80_DATA_OUT);
			return v;
		}
		return 0;

	// OIO board PERQLink: with no microcode-debugger link attached the status
	// port reads 0xff ("nothing connected"), steering the boot to the normal
	// Z80/disk path.  A full OIO board is a later phase.
	case 0x20:  return 0x00ff;
	case 0x22:  return 0;

	default:
		return 0;
	}
}

void perq_state::iobus_w(offs_t port, u16 data)
{
	switch (port)
	{
	case 0xc1:  // Z80 on/off (low bits are Shugart disk control, handled in the CPU device)
		if (data == 0x80 && m_z80_running)
		{
			m_z80_running = false;
			m_iob->set_input_line(INPUT_LINE_RESET, ASSERT_LINE);
			update_z80_int();
		}
		else if (data == 0 && !m_z80_running)
		{
			m_z80_running = true;
			m_z80_int_enabled = false;   // reset clears the FIFO's interrupt-enable
			std::queue<u8>().swap(m_z80_to_perq);
			std::queue<u8>().swap(m_perq_to_z80);
			m_iob->set_input_line(INPUT_LINE_RESET, CLEAR_LINE);   // run pz80.bin from 0x0000
			update_z80_int();
		}
		break;

	case 0xc7:  // load PERQ -> Z80 FIFO; bit 8 latches the DataInReady IRQ request
		if (BIT(data, 8))
		{
			m_z80_data_in_req = true;
		}
		else
		{
			m_z80_data_in_req = false;
			m_maincpu->clear_interrupt(perq_cpu_device::IRQ_Z80_DATA_IN);
		}
		if (m_z80_running)
		{
			m_perq_to_z80.push(u8(data));
			update_z80_int();
		}
		break;

	default:
		break;
	}
}


//**************************************************************************
//  Z80 I/O board
//**************************************************************************

u8 perq_state::perq_r_fifo()
{
	if (m_perq_to_z80.empty())
		return 0;

	const u8 v = m_perq_to_z80.front();
	m_perq_to_z80.pop();
	if (m_perq_to_z80.empty())
	{
		// the PERQ asked to be told once the Z80 consumed everything it sent
		if (m_z80_data_in_req)
			m_maincpu->raise_interrupt(perq_cpu_device::IRQ_Z80_DATA_IN);
		update_z80_int();
	}
	return v;
}

void perq_state::perq_w_fifo(u8 data)
{
	m_z80_to_perq.push(data);
	m_maincpu->raise_interrupt(perq_cpu_device::IRQ_Z80_DATA_OUT);
}

u8 perq_state::kbd_r()
{
	return 0;
}

u8 perq_state::ioreg1_r()
{
	// bit 6 = Z80 -> PERQ FIFO not-ready (full); the FIFO is always ready to accept
	return 0x00;
}

void perq_state::ioreg3_w(u8 data)
{
	m_dma_select      = (data >> 5) & 0x07;   // DMA device select (floppy phase)
	m_z80_int_enabled = BIT(data, 2);         // PRQENB: PERQ -> Z80 FIFO interrupt enable
	m_flp_int_enabled = BIT(data, 0);         // FLPENB: uPD765 floppy interrupt enable
	// bit 1 (KBDENB) gates the keyboard IRQ (wired when the keyboard comes online)
	update_z80_int();
}

void perq_state::disk_seek_w(u8 data)
{
	// hard-disk single-step strobe; wired to the Shugart controller in a later phase
}

u8 perq_state::gpib_r(offs_t offset)
{
	return 0xff;
}

void perq_state::gpib_w(offs_t offset, u8 data)
{
}

void perq_state::z80ctl_w(u8 data)
{
	// Z80 control-bus reset latch; the firmware writes 0 here during init
}

void perq_state::fdc_irq_w(int state)
{
	m_fdc_irq = (state != 0);
	update_z80_int();
}

void perq_state::update_z80_int()
{
	// The IOB's "soft" interrupt sources (the PERQ->Z80 FIFO and the uPD765)
	// are not Z80 daisy devices; the board's priority logic puts a fixed IM2
	// vector on the bus.  Reproduce that here, highest priority first: the
	// FIFO (vector 0x20) then the floppy (0x24).  The SIO/CTC keep their own
	// daisy-chain vectors, used by the Z80 whenever one of them is requesting;
	// a dedicated daisy device for these soft sources is a later refinement.
	int vector = -1;
	if (m_z80_int_enabled && !m_perq_to_z80.empty())
		vector = 0x20;   // PERQ -> Z80 FIFO (PRQVEC)
	else if (m_flp_int_enabled && m_fdc_irq)
		vector = 0x24;   // uPD765 floppy controller (FLPVEC)

	if (m_z80_running && vector >= 0)
		m_iob->set_input_line_and_vector(INPUT_LINE_IRQ0, ASSERT_LINE, vector);
	else
		m_iob->set_input_line_and_vector(INPUT_LINE_IRQ0, CLEAR_LINE, 0);
}


void perq_state::iob_mem_map(address_map &map)
{
	map(0x0000, 0x1fff).rom().region("iob", 0);   // pz80.bin
	map(0x2c00, 0x2fff).ram();                     // 1K work RAM
}

void perq_state::iob_io_map(address_map &map)
{
	map.global_mask(0xff);
	map(0x80, 0x80).r(FUNC(perq_state::kbd_r));
	map(0x88, 0x88).r(FUNC(perq_state::ioreg1_r));
	map(0x90, 0x93).rw(m_ctc, FUNC(z80ctc_device::read), FUNC(z80ctc_device::write));
	map(0x98, 0x98).rw(m_dma, FUNC(z80dma_device::read), FUNC(z80dma_device::write));
	map(0xa0, 0xa0).r(FUNC(perq_state::perq_r_fifo));
	map(0xa8, 0xa9).m(m_fdc, FUNC(upd765a_device::map));
	map(0xb0, 0xb3).rw(m_sio, FUNC(z80sio_device::ba_cd_r), FUNC(z80sio_device::ba_cd_w));
	map(0xb8, 0xbf).rw(FUNC(perq_state::gpib_r), FUNC(perq_state::gpib_w));   // GPIB (TMS9914A) stub
	map(0xc0, 0xc0).w(FUNC(perq_state::z80ctl_w));
	map(0xc8, 0xc8).w(FUNC(perq_state::ioreg3_w));
	map(0xd0, 0xd0).w(FUNC(perq_state::perq_w_fifo));
	map(0xd8, 0xd8).w(FUNC(perq_state::disk_seek_w));
}


//**************************************************************************
//  Machine configuration
//**************************************************************************

void perq_state::floppy_formats(format_registration &fr)
{
	fr.add_pc_formats();   // placeholder; the PERQ POS/RT-11 formats are added in a later phase
}

static void perq_floppies(device_slot_interface &device)
{
	device.option_add("8dsdd", FLOPPY_8_DSDD);
}

static const z80_daisy_config iob_daisy_chain[] =
{
	{ "sio" },
	{ "ctc" },
	{ nullptr }
};

void perq_state::perq1a(machine_config &config)
{
	// ~5.88 MHz microcycle (170 ns), per PERQemu
	PERQ(config, m_maincpu, 5'882'353);
	m_maincpu->iobus_in_cb().set(FUNC(perq_state::iobus_r));
	m_maincpu->iobus_out_cb().set(FUNC(perq_state::iobus_w));
	m_maincpu->dds_update_cb().set(FUNC(perq_state::dds_w));

	// portrait 768x1024 black-on-white display, served from the CPU device
	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_RASTER));
	screen.set_refresh_hz(60);
	screen.set_size(perq_video::WIDTH, perq_video::HEIGHT);
	screen.set_visarea(0, perq_video::WIDTH - 1, 0, perq_video::HEIGHT - 1);
	screen.set_screen_update("maincpu", FUNC(perq_cpu_device::screen_update));
	screen.set_palette("palette");

	PALETTE(config, "palette", palette_device::MONOCHROME);

	config.set_default_layout(layout_perq1a);

	// the PERQ and the Z80 hand-shake through tight cross-CPU spin loops, so
	// they have to interleave finely
	config.set_perfect_quantum(m_maincpu);

	// Z80 I/O board: 2.4576 MHz Z80 with SIO/CTC/DMA and a uPD765 floppy controller
	Z80(config, m_iob, 2'457'600);
	m_iob->set_addrmap(AS_PROGRAM, &perq_state::iob_mem_map);
	m_iob->set_addrmap(AS_IO, &perq_state::iob_io_map);
	m_iob->set_daisy_config(iob_daisy_chain);

	Z80CTC(config, m_ctc, 2'457'600);
	m_ctc->intr_callback().set_inputline(m_iob, INPUT_LINE_IRQ0);
	m_ctc->zc_callback<0>().set(m_sio, FUNC(z80sio_device::rxca_w));   // ch0 -> SIO ch A baud
	m_ctc->zc_callback<0>().append(m_sio, FUNC(z80sio_device::txca_w));

	Z80SIO(config, m_sio, 2'457'600);
	m_sio->out_int_callback().set_inputline(m_iob, INPUT_LINE_IRQ0);
	// channel A = RS232, channel B = Kriz tablet (serial devices attached in a later phase)

	Z80DMA(config, m_dma, 2'457'600);
	// the DMA data path (memory/IO routing via IOREG3) is wired in the floppy phase

	UPD765A(config, m_fdc, 8'000'000, true, true);
	m_fdc->intrq_wr_callback().set(FUNC(perq_state::fdc_irq_w));
	FLOPPY_CONNECTOR(config, "fdc:0", perq_floppies, "8dsdd", perq_state::floppy_formats);

	// Shugart SA4000-series hard disk (connected to the main CPU's controller)
	HARDDISK(config, "harddisk");
}


//**************************************************************************
//  ROM definitions
//**************************************************************************

static INPUT_PORTS_START( perq1a )
INPUT_PORTS_END

ROM_START( perq1a )
	// boot.bin (the boot microcode) is supplied by the perq_cpu_device's own
	// device_rom_region as the "boot" region.

	// Z80 I/O board firmware
	ROM_REGION( 0x2000, "iob", 0 )
	ROM_LOAD( "pz80.bin",      0x0000, 0x2000, CRC(ba86047a) SHA1(51a0e100e8b0553fd321808b1b1a37d8fc338e9d) )

	// physical bootstrap / control-store PROM dumps (wired up in a later phase)
	ROM_REGION( 0x0200, "rti02", 0 )
	ROM_LOAD( "rti02.rom",     0x0000, 0x0200, CRC(cad04f75) SHA1(7a789c6ab1f7627f14953b28231aec0ea49f7945) )

	ROM_REGION( 0x0c00, "t1boot", 0 )
	ROM_LOAD( "t1bootrom.rom", 0x0000, 0x0c00, CRC(cd75f925) SHA1(eb8d5381d87c366052ba1f01f803403e313dc29a) )

	ROM_REGION( 0x0400, "rds00", 0 )
	ROM_LOAD( "rds00.rom",     0x0000, 0x0400, CRC(77650e0a) SHA1(e507cbc0a1fa56054ce178f7600004d3669961ce) )

	ROM_REGION( 0x0100, "rsc03", 0 )
	ROM_LOAD( "rsc03.rom",     0x0000, 0x0100, CRC(d66f1f1f) SHA1(5ccccb68dc59dbcabab99adf8a57af0af545bfc5) )

	ROM_REGION( 0x0400, "rsh00", 0 )
	ROM_LOAD( "rsh00.rom",     0x0000, 0x0400, CRC(815d92bf) SHA1(b87bdea13de391e5615c474ba96af4b28b7f8f38) )
ROM_END

} // anonymous namespace


//    YEAR  NAME    PARENT  COMPAT  MACHINE  INPUT   CLASS       INIT        COMPANY                              FULLNAME   FLAGS
COMP( 1979, perq1a, 0,      0,      perq1a,  perq1a, perq_state, empty_init, "Three Rivers Computer Corporation", "PERQ 1A", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
