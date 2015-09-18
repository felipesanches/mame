// license:GPL2+
// copyright-holders:FelipeSanches
//
// Wacky Gator
//
// Most of this driver is based on guessing since I do not have access to the actual pcb for this game.
// Once we get a pcb, please review the correctness of the code in this driver before deleting this comment.
//
#include "emu.h"
#include "cpu/m6809/m6809.h"
#include "sound/msm5205.h"

class wackygtr_state : public driver_device
{
public:
	wackygtr_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag),
    m_msm(*this, "msm"),
    m_maincpu(*this, "maincpu")
	{ }

    UINT8* m_samples;
	int m_adpcm_data;
    int m_adpcm_pos;
    int m_adpcm_length;

	required_device<msm5205_device> m_msm;
	required_device<cpu_device> m_maincpu;
	DECLARE_DRIVER_INIT(wackygtr);
//protected:
//	virtual void machine_reset();

	INTERRUPT_GEN_MEMBER(wackygtr_interrupt);
	DECLARE_WRITE_LINE_MEMBER(adpcm_int);
	DECLARE_READ8_MEMBER(_0x6000_r);
	DECLARE_READ8_MEMBER(_0x6001_r);
	DECLARE_READ8_MEMBER(_0x6002_r);
	DECLARE_READ8_MEMBER(_0x71d0_r);
	DECLARE_READ8_MEMBER(_0x71d5_r);
	DECLARE_WRITE8_MEMBER(sample_pos_w);
	DECLARE_WRITE8_MEMBER(sample_length_w);
};


DRIVER_INIT_MEMBER(wackygtr_state, wackygtr)
{
    m_adpcm_data = -1;
    m_adpcm_pos = 0;
    m_adpcm_length = 0;
    m_samples = memregion("oki")->base();
}

READ8_MEMBER(wackygtr_state::_0x6000_r){
    return 0x00;
}

READ8_MEMBER(wackygtr_state::_0x6001_r){
    return 0x00;
}

READ8_MEMBER(wackygtr_state::_0x6002_r){
    return 0x00;
}

READ8_MEMBER(wackygtr_state::_0x71d0_r){
    return 0;
}

READ8_MEMBER(wackygtr_state::_0x71d5_r){
    return 0xFF;
}

WRITE8_MEMBER(wackygtr_state::sample_pos_w){
    m_adpcm_pos = (m_adpcm_pos & 0xFF) << 8 | (data & 0xFF);
}

WRITE8_MEMBER(wackygtr_state::sample_length_w){
    m_adpcm_length = (m_adpcm_length & 0xFF) << 8 | (data & 0xFF);
}

static INPUT_PORTS_START( wackygtr )
	PORT_START("INP0")
	PORT_BIT( 0xff, IP_ACTIVE_LOW, IPT_UNKNOWN )
INPUT_PORTS_END

INTERRUPT_GEN_MEMBER(wackygtr_state::wackygtr_interrupt)
{
	device.execute().set_input_line(0, ASSERT_LINE);
}

WRITE_LINE_MEMBER(wackygtr_state::adpcm_int)
{
    if (m_adpcm_length > 0)
    {
        m_adpcm_length--;
	    if (m_adpcm_data == -1)
	    {
		    /* transferring 1st nibble */
		    m_adpcm_data = m_samples[m_adpcm_pos];
		    m_adpcm_pos = (m_adpcm_pos + 1) & 0xffff;
		    m_msm->data_w((m_adpcm_data >> 4) & 0xf);
	    }
	    else
	    {
		    /* transferring 2nd nibble */
		    m_msm->data_w(m_adpcm_data & 0x0f);
		    m_adpcm_data = -1;
	    }
    }
}

static ADDRESS_MAP_START( program_map, AS_PROGRAM, 8, wackygtr_state )
	AM_RANGE(0x3000, 0x3000) AM_WRITE(sample_length_w)
	AM_RANGE(0x3001, 0x3001) AM_WRITE(sample_pos_w)
	AM_RANGE(0x6000, 0x6000) AM_READ(_0x6000_r)
	AM_RANGE(0x6001, 0x6001) AM_READ(_0x6001_r)
	AM_RANGE(0x6002, 0x6002) AM_READ(_0x6002_r)
	AM_RANGE(0x71d0, 0x71d0) AM_READ(_0x71d0_r)
	AM_RANGE(0x71d5, 0x71d5) AM_READ(_0x71d5_r)
	AM_RANGE(0x7000, 0x7fff) AM_RAM
	AM_RANGE(0x8000, 0xffff) AM_ROM
ADDRESS_MAP_END

static MACHINE_CONFIG_START( wackygtr, wackygtr_state )
	/* basic machine hardware */
	MCFG_CPU_ADD("maincpu", M6809E, XTAL_8MHz)
	MCFG_CPU_PROGRAM_MAP(program_map)
//	MCFG_CPU_PERIODIC_INT_DRIVER(wackygtr_state, wackygtr_interrupt,  60)

	/* Video */
//	MCFG_DEFAULT_LAYOUT(layout_wackygtr)

	/* Sound */
	/* sound hardware */
	MCFG_SPEAKER_STANDARD_MONO("mono")

	MCFG_SOUND_ADD("msm", MSM5205, XTAL_384kHz )
	MCFG_MSM5205_VCLK_CB(WRITELINE(wackygtr_state, adpcm_int))   /* IRQ handler */
	MCFG_MSM5205_PRESCALER_SELECTOR(MSM5205_S48_4B)      /* 8 KHz, 4 Bits  */
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.0)
MACHINE_CONFIG_END


ROM_START( wackygtr )
	ROM_REGION(0x10000, "maincpu", 0)
	ROM_LOAD("wp3-pr0.4d", 0x8000, 0x8000, CRC(71ca4437) SHA1(c7d948c5593e6053fd0a65601f6c06871f5861f0))

	ROM_REGION(0x10000, "oki", 0)
	ROM_LOAD("wp3-vo0.2h", 0x0000, 0x10000, CRC(91c7986f) SHA1(bc9fa0d41c1caa0f909a349f511d022b7e42c6cd))
ROM_END

GAME(19??, wackygtr,    0, wackygtr,  wackygtr, wackygtr_state, wackygtr,  ROT0,   "DataEast", "Wacky Gator", MACHINE_IS_SKELETON_MECHANICAL)
