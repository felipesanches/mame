// license:BSD-3-Clause
// copyright-holders:FelipeSanches
/***************************************************************************

    Skeleton driver by Felipe Sanches

***************************************************************************/

#include "emu.h"
#include "cpu/m68000/m68000.h"
#include "machine/mcf5206e.h"


class cdj200_state : public driver_device
{
public:
	cdj200_state(const machine_config &mconfig, device_type type, const char *tag) :
		driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu")
	{ }

	void cdj200(machine_config &config);

private:
	virtual void machine_start() override;
	virtual void machine_reset() override;
	u32 mbar2_reg_r(offs_t offset);
	void mbar2_reg_w(offs_t offset, u32 data);
	void cdj200_map(address_map &map);

	required_device<cpu_device> m_maincpu;
	u32 m_PLLCR;
};

void cdj200_state::machine_start()
{
}

void cdj200_state::machine_reset()
{
}

void cdj200_state::cdj200_map(address_map &map)
{
	map(0x00000000, 0x0006eccf).rom().region("maincpu", 0);
	map(0x00100000, 0x00ffffff).ram(); //guessed based on elektronmono.cpp
	map(0x10000000, 0x10017fff).ram(); //stack

  // FIXME: hardware setup registers should be emulated inside the CPU
	map(0x80000000, 0x80000183).rw(FUNC(cdj200_state::mbar2_reg_r), FUNC(cdj200_state::mbar2_reg_w));
}

void cdj200_state::mbar2_reg_w(offs_t offset, u32 data)
{
	switch(offset*4){
		case 0x180:
			m_PLLCR = data;
			break;
		default:
			break;
	}
}

u32 cdj200_state::mbar2_reg_r(offs_t offset)
{
	switch(offset*4){
		case 0x180:
			// PLLCR register
			return m_PLLCR | 0x80000000; // PLL locked bit!
		default:
			return 0;
	}
}

void cdj200_state::cdj200(machine_config &config)
{
	MCF5206E(config, m_maincpu, XTAL(25'447'000)); //guessed clock!
	m_maincpu->set_addrmap(AS_PROGRAM, &cdj200_state::cdj200_map);
}

static INPUT_PORTS_START( cdj200 )
INPUT_PORTS_END


ROM_START( cdj200 )
	ROM_REGION(0x6ecd0, "maincpu", 0)
	ROM_LOAD( "cdj200.bin", 0x000000, 0x6ecd0, CRC(9db83be7) SHA1(d3534c43d5faa0891827393bb85e7c56c74c23ad) )
ROM_END

CONS( 200?, cdj200, 0, 0, cdj200, cdj200, cdj200_state, empty_init, "Pioneer", "CDJ-200",  MACHINE_NOT_WORKING|MACHINE_NO_SOUND )
