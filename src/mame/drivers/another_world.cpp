// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/*
    Another World game (Virtual Machine based driver)
*/

#include "emu.h"
#include "includes/anotherworld.h"
#include "cpu/anotherworld/anotherworld.h"

// device type definition
const device_type ANOTHERW_SOUND = &device_creator<anotherw_sound_device>;

//-------------------------------------------------
//  anotherw_sound_device - constructor
//-------------------------------------------------

anotherw_sound_device::anotherw_sound_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
    : device_t(mconfig, ANOTHERW_SOUND, "ANOTHERW_SOUND", tag, owner, clock, "anotherw_sound", __FILE__),
        device_sound_interface(mconfig, *this),
        m_stream(nullptr)
{
}

//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void anotherw_sound_device::device_start()
{
    // create the stream
    m_stream = machine().sound().stream_alloc(*this, 0, 1, clock());

    memset(m_channels, 0, sizeof(m_channels));
}


//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void anotherw_sound_device::device_reset()
{
    m_stream->update();
    for (auto & elem : m_channels)
        elem.m_active = false;
}


//-------------------------------------------------
//  device_post_load - device-specific post-load
//-------------------------------------------------

void anotherw_sound_device::device_post_load()
{
    device_clock_changed();
}


//-------------------------------------------------
//  device_clock_changed - called if the clock
//  changes
//-------------------------------------------------

void anotherw_sound_device::device_clock_changed()
{
    m_stream->set_sample_rate(clock());
}

//-------------------------------------------------
//  stream_generate - handle update requests for
//  our sound stream
//-------------------------------------------------

void anotherw_sound_device::sound_stream_update(sound_stream &stream, stream_sample_t **inputs, stream_sample_t **outputs, int samples)
{
    // reset the output stream
    memset(outputs[0], 0, samples * sizeof(*outputs[0]));

    // iterate over channels and accumulate sample data
    for (auto & elem : m_channels)
        elem.mix(outputs[0], samples);
}

#define OUTPUT_SAMPLE_RATE 384000
void anotherw_sound_device::playChannel(uint8_t channel, const MixerChunk *mc, uint16_t freq, uint8_t volume) {
    assert(channel < ANOTHERW_CHANNELS);

    anotherw_channel *ch = &m_channels[channel];
    ch->m_active = true;
    ch->m_volume = volume;
    ch->m_chunk = *mc;
    ch->m_chunkPos = 0;
    ch->m_chunkInc = (freq << 8) / OUTPUT_SAMPLE_RATE;
}

void anotherw_sound_device::stopChannel(uint8_t channel) {
    assert(channel < ANOTHERW_CHANNELS);
    m_channels[channel].m_active = false;
}

void anotherw_sound_device::setChannelVolume(uint8_t channel, uint8_t volume) {
    assert(channel < ANOTHERW_CHANNELS);
    m_channels[channel].m_volume = volume;
}

void anotherw_sound_device::stopAll() {
    for (uint8_t i = 0; i < ANOTHERW_CHANNELS; ++i) {
        m_channels[i].m_active = false;        
    }
}

//**************************************************************************
//  ANOTHERW CHANNEL
//**************************************************************************

//-------------------------------------------------
//  anotherw_channel - constructor
//-------------------------------------------------

anotherw_sound_device::anotherw_channel::anotherw_channel()
    : m_sample(0),
      m_count(0),
      m_active(false),
      m_volume(0)
{
}

static int16_t addclamp(int a, int b) {
    int add = a + b;
    if (add < -32768) {
        add = -32768;
    }
    else if (add > 32767) {
        add = 32767;
    }
    return (int16_t)add;
}

//-------------------------------------------------
//  generate_samples and
//  add them to an output stream
//-------------------------------------------------

void anotherw_sound_device::anotherw_channel::mix(stream_sample_t *buffer, int samples)
{
    // skip if not active
    if (!m_active)
        return;

    stream_sample_t *pBuf = buffer;

    // loop while we still have samples to generate
    for (int j = 0; j < samples; ++j, ++pBuf) {
        uint16_t p1, p2;
        uint16_t ilc = (m_chunkPos & 0xFF);
        p1 = m_chunkPos >> 8;
        m_chunkPos += m_chunkInc;

        if (m_chunk.loopLen != 0) {
            if (p1 == m_chunk.loopPos + m_chunk.loopLen - 1) {
                printf("Looping sample\n");
                m_chunkPos = p2 = m_chunk.loopPos;
            } else {
                p2 = p1 + 1;
            }
        } else {
            if (p1 == m_chunk.len - 1) {
                printf("Stopping sample\n");
                m_active = false;
                break;
            } else {
                p2 = p1 + 1;
            }
        }
        // interpolate
        int8_t b1 = *(int8_t *)(m_chunk.data + p1);
        int8_t b2 = *(int8_t *)(m_chunk.data + p2);
        int8_t b = (int8_t)((b1 * (0xFF - ilc) + b2 * ilc) >> 8);

        // set volume and clamp
        *pBuf = addclamp(*pBuf, ((int)b << 8) * m_volume / 0x40);
    }
}

//TODO: Maybe this shuld be stored in a ROM asset as well?
static uint8_t resource_indexes[] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x12, 0x13, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31,
    0x32, 0x33, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
    0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42,
    0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
    0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52,
    0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a,
    0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62,
    0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a,
    0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72,
    0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
    0x7b, 0x7c, 0x80, 0x81, 0x82, 0x83, 0x84, 0x88,
    0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x90, 0x91
};

static uint32_t resource_offset(uint16_t resNum){
    for (int i=0; i<sizeof(resource_indexes); i++){
        if (resNum == resource_indexes[i]) return i;
    }

    printf("ERROR! Resource 0x%X not found!\n", resNum);
    return 0;
}

const uint16_t anotherw_sound_device::frequenceTable[] = {
    0x0CFF, 0x0DC3, 0x0E91, 0x0F6F, 0x1056, 0x114E, 0x1259, 0x136C,
    0x149F, 0x15D9, 0x1726, 0x1888, 0x19FD, 0x1B86, 0x1D21, 0x1EDE,
    0x20AB, 0x229C, 0x24B3, 0x26D7, 0x293F, 0x2BB2, 0x2E4C, 0x3110,
    0x33FB, 0x370D, 0x3A43, 0x3DDF, 0x4157, 0x4538, 0x4998, 0x4DAE,
    0x5240, 0x5764, 0x5C9A, 0x61C8, 0x6793, 0x6E19, 0x7485, 0x7BBD
};

void another_world_state::playSound(uint16_t resNum, uint8_t freq, uint8_t vol, uint8_t channel){
    printf("playSound(0x%02X, freq:%d, vol:%d, channel:%d)\n", resNum, freq, vol, channel);

    const uint8_t * samples = memregion("samples")->base() + resource_offset(resNum) * 0x10000;

    if (vol == 0) {
        m_mixer->stopChannel(channel);
    } else {
        struct anotherw_sound_device::MixerChunk mc;
        memset(&mc, 0, sizeof(mc));
        mc.data = samples + 8; // skip header
        mc.len = (*(samples+1) << 8 | *(samples)) * 2;
        mc.loopLen = (*(samples+3) << 8 | *(samples+2)) * 2;
        if (mc.loopLen != 0) {
            mc.loopPos = mc.len;
        }
        assert(freq < 40);
        uint16_t f = anotherw_sound_device::frequenceTable[freq];
        m_mixer->playChannel(channel & 3, &mc, f, MIN(vol, 0x3F));
    }
}

/*
    driver init function
*/
DRIVER_INIT_MEMBER(another_world_state, another_world)
{
}

void another_world_state::machine_start(){
}

static INPUT_PORTS_START( another_world )
      PORT_START("keyboard")
      PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Arrow up") PORT_CODE(KEYCODE_RIGHT)
      PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Arrow down") PORT_CODE(KEYCODE_LEFT)
      PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Arrow left") PORT_CODE(KEYCODE_DOWN)
      PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Arrow right") PORT_CODE(KEYCODE_UP)
      PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Action") PORT_CODE(KEYCODE_LCONTROL)
      
      //TODO: hookup the actual keyboard letter keys as inputs for usage in the password screen
INPUT_PORTS_END

READ16_MEMBER(another_world_state::up_down_r)
{
    int value = ioport("keyboard")->read();
    return (value & 0x08) ? 0xFFFF : (value & 0x04) ? 1 : 0;
}

READ16_MEMBER(another_world_state::left_right_r)
{
    int value = ioport("keyboard")->read();
    return (value & 0x02) ? 0xFFFF : (value & 0x01) ? 1 : 0;
}

READ16_MEMBER(another_world_state::action_r)
{
    int value = ioport("keyboard")->read();
    return (value & 0x80) ? 1 : 0;
}

READ16_MEMBER(another_world_state::pos_mask_r)
{
    return ioport("keyboard")->read() & 0xf;
}

READ16_MEMBER(another_world_state::action_pos_mask_r)
{
    return ioport("keyboard")->read();
}

/* Graphics Layouts */

static const gfx_layout charlayout =
{
    8,8,    /* 8*8 characters */
    96,     /* 96 characters */
    4,      /* 4 bits per pixel */
    { 0 },
    { 0, 1, 2, 3, 4, 5, 6, 7},
    { 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
    8*8    /* every char takes 8 consecutive bytes */
};

/* Graphics Decode Info */

static GFXDECODE_START( anotherw )
    GFXDECODE_ENTRY( "chargen", 0, charlayout,            0, 16)
GFXDECODE_END

TILE_GET_INFO_MEMBER(another_world_state::get_char_tile_info)
{
//    int attr = m_colorram[tile_index];
    int code = m_videoram[tile_index];// + ((attr & 0xe0) << 2);
//    int color = attr & 0x1f;
    int color = 15;

    tileinfo.group = color;

    SET_TILE_INFO_MEMBER(0, code, color, 0);
}

static ADDRESS_MAP_START( aw_prog_map, AS_PROGRAM, 8, another_world_state )
    AM_RANGE(0x00000, 0x0fbff) AM_ROMBANK("bytecode_bank")
    AM_RANGE(0x0fc00, 0x0ffff) AM_RAM AM_SHARE("videoram")
    AM_RANGE(0x10000, 0x1ffff) AM_ROMBANK("video1_bank") /* FIX-ME: This is just a hack for setting up a video1 bank */
    AM_RANGE(0x20000, 0x207ff) AM_ROMBANK("palette_bank") /* FIX-ME: This is just a hack for setting up a palette bank */
ADDRESS_MAP_END

static ADDRESS_MAP_START( aw_data_map, AS_DATA, 16, another_world_state )
    AM_RANGE(0x01f4, 0x01f5) AM_READ(action_r)
    AM_RANGE(0x01f6, 0x01f7) AM_READ(up_down_r)
    AM_RANGE(0x01f8, 0x01f9) AM_READ(left_right_r)
    AM_RANGE(0x01fa, 0x01fb) AM_READ(pos_mask_r)
    AM_RANGE(0x01fc, 0x01fd) AM_READ(action_pos_mask_r)
    AM_RANGE(0x0000, 0x01ff) AM_RAM //VM Variables
ADDRESS_MAP_END

static MACHINE_CONFIG_START( another_world, another_world_state )
    /* basic machine hardware */
    MCFG_CPU_ADD("maincpu", ANOTHER_WORLD, 50) /* This clock is based on the finest
                                                * pause interval resolution
                                                * 20msec = 50Hz
                                                * (only the "pause" opcode consumes cycles)
                                                */
    MCFG_CPU_PROGRAM_MAP(aw_prog_map)
    MCFG_CPU_DATA_MAP(aw_data_map)

    MCFG_MACHINE_START_OVERRIDE(another_world_state, anotherw)

    /* video hardware */
    MCFG_SCREEN_ADD("screen", RASTER)
    MCFG_SCREEN_REFRESH_RATE(60)
    MCFG_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(0))
    MCFG_SCREEN_SIZE(40*8, 25*8)
    MCFG_SCREEN_VISIBLE_AREA(0*8, 40*8-1, 0*8, 25*8-1)
    MCFG_SCREEN_UPDATE_DRIVER(another_world_state, screen_update_aw)

    MCFG_SCREEN_PALETTE("palette")
    MCFG_GFXDECODE_ADD("gfxdecode", "palette", anotherw)

    MCFG_PALETTE_ADD("palette", 16)
    MCFG_PALETTE_INDIRECT_ENTRIES(16) /*I am not sure yet what does it mean...*/

    /* sound hardware */
    MCFG_SPEAKER_STANDARD_MONO("mono")

    MCFG_SOUND_ADD("samples", ANOTHERW_SOUND, 384000)
    MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.0)
MACHINE_CONFIG_END

MACHINE_START_MEMBER(another_world_state, anotherw)
{
    membank("bytecode_bank")->configure_entries(0, 10, memregion("bytecode")->base(), 0x10000);
    membank("palette_bank")->configure_entries(0, 10, memregion("palettes")->base(), 0x0800);
    membank("video1_bank")->configure_entries(0, 10, memregion("video1")->base(), 0x10000);

    setupPart(0);
}

void another_world_state::setupPart(uint16_t resourceId){
    uint8_t bank = resourceId & 0xf;

    if (bank==9) bank--; //password screen TODO: do we really need this?!

    if (bank < 9){
        membank("bytecode_bank")->set_entry(bank);
        membank("palette_bank")->set_entry(bank);
        membank("video1_bank")->set_entry(bank);
    }
}

ROM_START( aw_msdos )
    ROM_REGION( 0x90000, "bytecode", ROMREGION_ERASEFF ) /* MS-DOS: Bytecode */
    ROM_LOAD( "resource-0x15.bin", 0x00000, 0x10e1, CRC(f26172f6) SHA1(d0bc831a0683bb1416900c45be677a51fb9bc0fa) ) /* Protection screens */
    ROM_LOAD( "resource-0x18.bin", 0x10000, 0x268f, CRC(77edb7c0) SHA1(e30861c118818bd6e6c6e22205d3a657a87a2523) ) /* introduction cinematic */
    ROM_LOAD( "resource-0x1b.bin", 0x20000, 0x514a, CRC(82ccacd6) SHA1(f093b219e10d3bd9d9fc93d36cb232f13da4881e) ) /* lake initial stage + beast hunt */
    ROM_LOAD( "resource-0x1e.bin", 0x30000, 0x9b0f, CRC(0d73bb4b) SHA1(1f0817756eb13dcf914d91b9ee86e461f8b012e6) )
    ROM_LOAD( "resource-0x21.bin", 0x40000, 0xf4db, CRC(66546d1c) SHA1(c092649f280fbc7af4c831e56f8dd52675b9a571) )
    ROM_LOAD( "resource-0x24.bin", 0x50000, 0x205f, CRC(eba90ce1) SHA1(d5470f4cee0a261af6e50436cba5b916eaa6ae22) ) /* battlechar cinematic */
    ROM_LOAD( "resource-0x27.bin", 0x60000, 0xc630, CRC(d6d9b3be) SHA1(33fb28e37b1fc69993b41e3ee3ec1f17f9a0e3b1) )
    ROM_LOAD( "resource-0x2a.bin", 0x70000, 0x0b46, CRC(2bfb51c6) SHA1(6a6b293ecf7656f42fb5ba2fae9b9caa1de95a51) )
    ROM_LOAD( "resource-0x7e.bin", 0x80000, 0x10a1, CRC(c1d2eab1) SHA1(7b47c2797ad3d11d66d7d3ab7b6f6d6f1aeacc4a) ) /* password screen */

    ROM_REGION( 0x4800, "palettes", 0 ) /* MS-DOS: Palette */
    ROM_LOAD( "resource-0x14.bin", 0x0000, 0x0800, CRC(d72808cf) SHA1(b078c4a11628a384ab7c3128dfe93eaeb2745c07) ) /* Protection screens */
    ROM_LOAD( "resource-0x17.bin", 0x0800, 0x0800, CRC(47fffea1) SHA1(90d179214abc7cae251eb880c193abf6b628468d) ) /* introduction cinematic */
    ROM_LOAD( "resource-0x1a.bin", 0x1000, 0x0800, CRC(7f113f5b) SHA1(36a68781be5dac1533b08000f22166516e2e2f6f) ) /* lake initial stage + beast hunt */
    ROM_LOAD( "resource-0x1d.bin", 0x1800, 0x0800, CRC(e4de15de) SHA1(523fe81af8da8967abb1015a158d54998e2c13c2) )
    ROM_LOAD( "resource-0x20.bin", 0x2000, 0x0800, CRC(b2ec0730) SHA1(1568de3eb0de055771a47137e99a3f833ee2727d) )
    ROM_LOAD( "resource-0x23.bin", 0x2800, 0x0800, CRC(a348edf0) SHA1(79d83dc3814470d134be6042138f2788105572b1) ) /* battlechar cinematic */
    ROM_LOAD( "resource-0x26.bin", 0x3000, 0x0800, CRC(496504ed) SHA1(3c1e6630e2cc45b10d15174750e3e3c27b8aa642) )
    ROM_LOAD( "resource-0x29.bin", 0x3800, 0x0800, CRC(3a47eb2b) SHA1(ea89ff64ddf1e6928779f381176c002d9bb901ce) )
    ROM_LOAD( "resource-0x7d.bin", 0x4000, 0x0800, CRC(30a8a552) SHA1(a84d3129d6119d7669eb8179459b145cc1f543b7) ) /* password screen */

    ROM_REGION( 0x90000, "video1", ROMREGION_ERASEFF ) /* MS-DOS: Cinematic */
    ROM_LOAD( "resource-0x16.bin", 0x00000, 0x1404, CRC(114b0df5) SHA1(41d191da457779b0ce140035889ad2c73bf171b8) ) /* Protection screens */
    ROM_LOAD( "resource-0x19.bin", 0x10000, 0xfece, CRC(89c1285e) SHA1(4ed7e5558583fe7b442bd615f8ba3e19ebd25174) ) /* introduction cinematic */
    ROM_LOAD( "resource-0x1c.bin", 0x20000, 0xe0a6, CRC(374a9f2c) SHA1(b358843f81e2ca09d0be9957d9b012d96a134dc7) ) /* lake initial stage + beast hunt */
    ROM_LOAD( "resource-0x1f.bin", 0x30000, 0xd1d8, CRC(e11e2762) SHA1(33229378309d4bee4ff286b0713bf30d7591797a) )
    ROM_LOAD( "resource-0x22.bin", 0x40000, 0xfe6c, CRC(774b3023) SHA1(5b29e56485c10040a81cd9c860bc634547f3f8f4) )
    ROM_LOAD( "resource-0x25.bin", 0x50000, 0x721c, CRC(973c87df) SHA1(5fab9f14ab51e78cf95965f244e8b6464c96a64f) ) /* battlechar cinematic */
    ROM_LOAD( "resource-0x28.bin", 0x60000, 0xfdbe, CRC(0ff476d6) SHA1(61352b6dd8a66687c01c460a19d752772218abc0) )
    ROM_LOAD( "resource-0x2b.bin", 0x70000, 0x4e3a, CRC(94f58241) SHA1(fb812f75da62639094e095461dc5391b94bc0bd2) )
    ROM_LOAD( "resource-0x7f.bin", 0x80000, 0x13b8, CRC(125a7e9e) SHA1(1a38a2f3ab0df86ebfa6dea3feb74cd7488fb329) ) /* password screen */

    ROM_REGION( 0x8000, "video2", ROMREGION_ERASEFF ) /* MS-DOS: Video2 */
    ROM_LOAD( "resource-0x11.bin", 0x0000, 0x6214, CRC(2ea7976e) SHA1(93309c022be0f74064bf31618f8433b1c4d093dc) )

    ROM_REGION( 0x0300, "chargen", 0)
    ROM_LOAD( "anotherworld_chargen.rom", 0x0000, 0x0300, CRC(e2df8c47) SHA1(b79b41835aa2d5747932f8080bb6fb2cf32837d7) )

    ROM_REGION( 0x700000, "samples", ROMREGION_ERASEFF ) /* MS-DOS: Unknown resources. I guess most of these are sound samples (maybe all?!) */
    ROM_LOAD( "resource-0x01.bin", 0x000000, 0x1A3C, CRC(baa9a633) SHA1(6d76db3a050bbb6696a7a24048144aa0820c354a) )
    ROM_LOAD( "resource-0x02.bin", 0x010000, 0x2E34, CRC(117f183a) SHA1(b39c6b22df38f0f64b2e86dc0b002e39071eaf3c) )
    ROM_LOAD( "resource-0x03.bin", 0x020000, 0x69F8, CRC(49526d55) SHA1(2204c21463852bfbc47f65862e4d7b5bfcfec2ef) )
    ROM_LOAD( "resource-0x04.bin", 0x030000, 0x45CE, CRC(6639098a) SHA1(053e725590b4d30b36f584cf12d7f9b1fc7798e2) )
    ROM_LOAD( "resource-0x05.bin", 0x040000, 0x0EFA, CRC(ce3ede40) SHA1(1d72e4f81d6ee2134f176e1cb1e8f9eb6a5ff495) )
    ROM_LOAD( "resource-0x06.bin", 0x050000, 0x0D26, CRC(fa97ccff) SHA1(f18501867e2e5f7e256e9c4410d626e4355235c9) )
    ROM_LOAD( "resource-0x07.bin", 0x060000, 0x3CC0, CRC(c2fc8e2f) SHA1(1b1420412826e73294d8fff826d323aeaaca5214) )
    ROM_LOAD( "resource-0x08.bin", 0x070000, 0x2674, CRC(72b76acd) SHA1(88892cd9ac873a3a5aad409ef838d0e2329f3468) )
    ROM_LOAD( "resource-0x09.bin", 0x080000, 0x2BB6, CRC(fbf55601) SHA1(40ca27866ec62815e6ace999dedaee4b0b21ce8b) )
    ROM_LOAD( "resource-0x0a.bin", 0x090000, 0x2BB4, CRC(1c60352e) SHA1(972a49972fdbf889e519a42d6bdfe15cec2dd87c) )
    ROM_LOAD( "resource-0x0b.bin", 0x0a0000, 0x0426, CRC(f47dcea3) SHA1(ce0d8a56f80fdb3324d4415fabf86319d32fb290) )
    ROM_LOAD( "resource-0x0c.bin", 0x0b0000, 0x1852, CRC(e2dfed36) SHA1(f650ea9596a18b3536cec9d1dceef3c1bf6d6a70) )
    ROM_LOAD( "resource-0x0d.bin", 0x0c0000, 0x0594, CRC(c0dd33e4) SHA1(25f327737082969deece6ba98ac99e1623c95cb7) )
    ROM_LOAD( "resource-0x0e.bin", 0x0d0000, 0x13F0, CRC(cff8499b) SHA1(0c061f787d2acfd4a64deb0bc1e2c506b90b7db6) )
    ROM_LOAD( "resource-0x0f.bin", 0x0e0000, 0x079E, CRC(c7c2353f) SHA1(39ec64e4ce72171557413a9c1d7c0106426520f7) )
    ROM_LOAD( "resource-0x10.bin", 0x0f0000, 0x56A2, CRC(4d9db7c8) SHA1(78fb2762b40acf85fe0133d96d1325ec039a0525) )
    ROM_LOAD( "resource-0x12.bin", 0x100000, 0x7D00, CRC(3245f257) SHA1(9078b9a8cab39d495feca2e6cdb2fd54d2926428) )
    ROM_LOAD( "resource-0x13.bin", 0x110000, 0x7D00, CRC(f74291cf) SHA1(41a357c9f3823b5af94448f4d6894c706bf33b35) )
    ROM_LOAD( "resource-0x2c.bin", 0x120000, 0x0372, CRC(74d1ca59) SHA1(a51f11f9fcb4d1e2121d99f4208d8b0ad1dd6d3a) )
    ROM_LOAD( "resource-0x2d.bin", 0x130000, 0x1E04, CRC(23de4dd5) SHA1(d5fb5f7bf553ed4aeb4539443a304d24d8d11da0) )
    ROM_LOAD( "resource-0x2e.bin", 0x140000, 0x08EA, CRC(f6654b27) SHA1(f2f5cbb426845c6dfc6a3265cd9458321b64d5ba) )
    ROM_LOAD( "resource-0x2f.bin", 0x150000, 0x1A46, CRC(a5b0f7a5) SHA1(9145af546a9fb5970a595f6196914d7102d47f79) )
    ROM_LOAD( "resource-0x30.bin", 0x160000, 0x343E, CRC(2d5ea830) SHA1(70e336c95d9be763f690b6683a9477b41e5a1522) )
    ROM_LOAD( "resource-0x31.bin", 0x170000, 0x149E, CRC(74b46750) SHA1(224647901d1523a52b79639f0eb8302ba9dc82d9) )
    ROM_LOAD( "resource-0x32.bin", 0x180000, 0x1866, CRC(f15d1810) SHA1(50ff08a64090b3429ade13a9873c2b17319dee36) )
    ROM_LOAD( "resource-0x33.bin", 0x190000, 0x0266, CRC(b0dd8e22) SHA1(324c2a036c8d383991f7db413a90b72cf07b3a0f) )
    ROM_LOAD( "resource-0x35.bin", 0x1a0000, 0x01A8, CRC(947b7c3c) SHA1(62b453bc89e641156f22ff3d3adf2e438e928687) )
    ROM_LOAD( "resource-0x36.bin", 0x1b0000, 0x1FEC, CRC(0fa4219a) SHA1(317c02d8d8ace986ade0c0e20dc17b58988c685a) )
    ROM_LOAD( "resource-0x37.bin", 0x1c0000, 0x13A4, CRC(70599496) SHA1(29ad38779a663fb38d8e1b7bc8554379af40a7a9) )
    ROM_LOAD( "resource-0x38.bin", 0x1d0000, 0x15C4, CRC(6bea3d6d) SHA1(bf568a126b09a40d9dddd99059d047ee91c98b24) )
    ROM_LOAD( "resource-0x39.bin", 0x1e0000, 0x0E2A, CRC(de344a15) SHA1(bfe23d637153a35e2df0cd85a9759b996bdd1a99) )
    ROM_LOAD( "resource-0x3a.bin", 0x1f0000, 0x0366, CRC(4283476b) SHA1(ad66ee0a3d7ea3f8b2abffabdfc12435d0ae8777) )
    ROM_LOAD( "resource-0x3b.bin", 0x200000, 0x0078, CRC(ef77075e) SHA1(ff276ae093e9f43ac929e4ad9bd165699a3f5100) )
    ROM_LOAD( "resource-0x3c.bin", 0x210000, 0x1392, CRC(dfa9363a) SHA1(062423ff50e1f47b08636a1061f8f7ad92dbd9ee) )
    ROM_LOAD( "resource-0x3d.bin", 0x220000, 0x06E0, CRC(e6ba6acd) SHA1(9b750dcfa55fd6a681bbe842488785b053315c93) )
    ROM_LOAD( "resource-0x3e.bin", 0x230000, 0x21AE, CRC(d6138ce1) SHA1(d1f23331adb4cfe877833d15cd541d069b1a0bb7) )
    ROM_LOAD( "resource-0x3f.bin", 0x240000, 0x04FA, CRC(ef6b6f92) SHA1(148a0f86b07d355aeabc5c33cba7ae198745845e) )
    ROM_LOAD( "resource-0x40.bin", 0x250000, 0x129E, CRC(152c5abc) SHA1(c38417a9609a8961f34b3157218581d250572633) )
    ROM_LOAD( "resource-0x41.bin", 0x260000, 0x09B4, CRC(6f061599) SHA1(00b617367adc8dc60cffa31b84d9433cd8ec2306) )
    ROM_LOAD( "resource-0x42.bin", 0x270000, 0x04EC, CRC(1fd8273d) SHA1(128167f1c895431720944e583a114d1a792de813) )
    ROM_LOAD( "resource-0x43.bin", 0x280000, 0x7D00, CRC(f239adf3) SHA1(d95192c09eb03e51c829dad35497feb9c7a3690a) )
    ROM_LOAD( "resource-0x44.bin", 0x290000, 0x7D00, CRC(3b98dab3) SHA1(c85ec1b18f08c8704c9bac05a673246e6c1b752a) )
    ROM_LOAD( "resource-0x45.bin", 0x2a0000, 0x7D00, CRC(24a0cec9) SHA1(5cd9647773e57f241ced52518ac9eeac38d18de8) )
    ROM_LOAD( "resource-0x46.bin", 0x2b0000, 0x7D00, CRC(d9f161ce) SHA1(efa5c70e193e467a1fab11e133179a3e12dc428f) )
    ROM_LOAD( "resource-0x47.bin", 0x2c0000, 0x7D00, CRC(ddb21ee0) SHA1(981f7fa7459b7e127934764dcfff750d46c43da9) )
    ROM_LOAD( "resource-0x48.bin", 0x2d0000, 0x7D00, CRC(3054c09c) SHA1(0b646a8e772cc9e68fe5250c415c379fc816daaa) )
    ROM_LOAD( "resource-0x49.bin", 0x2e0000, 0x7D00, CRC(23180593) SHA1(0237a0c7321b02dd6e8ff875050bb47ae518041f) )
    ROM_LOAD( "resource-0x4a.bin", 0x2f0000, 0x03C0, CRC(3aef65eb) SHA1(fa59ec97e037e0d6f70dff519041c986b396d2c6) )
    ROM_LOAD( "resource-0x4b.bin", 0x300000, 0x13E6, CRC(eafe485d) SHA1(46f8788e0cd7c7c23c4ddc11530f3b3daef78b3d) )
    ROM_LOAD( "resource-0x4c.bin", 0x310000, 0x04DE, CRC(87bb26d7) SHA1(3869adc5b0e15f98bd723b9d86cc45d567036386) )
    ROM_LOAD( "resource-0x4d.bin", 0x320000, 0x05FA, CRC(12d3fd86) SHA1(98ceb97e85f0f6bbebd51669e63b6cb94a15b154) )
    ROM_LOAD( "resource-0x4e.bin", 0x330000, 0x025E, CRC(99049d27) SHA1(c8114836edd34ef07fa73ad8928bd690bae82d89) )
    ROM_LOAD( "resource-0x4f.bin", 0x340000, 0x0642, CRC(d670a518) SHA1(f4fd152cc3b88099b2072a46585ab12262d7b5e2) )
    ROM_LOAD( "resource-0x50.bin", 0x350000, 0x19D0, CRC(dd0fbb7c) SHA1(1425d0f37b9a9c23f2cdc8aea1098d52701c1141) )
    ROM_LOAD( "resource-0x51.bin", 0x360000, 0x00E8, CRC(39a17088) SHA1(f03f02386ca1076d662b9b0c8e7ab064da133f4c) )
    ROM_LOAD( "resource-0x52.bin", 0x370000, 0x1022, CRC(4804c511) SHA1(7b2ff405ab9cf4d72541c4d8ab88f6197a1b985c) )
    ROM_LOAD( "resource-0x53.bin", 0x380000, 0x7D00, CRC(59fd56ff) SHA1(5c85d468368436f4934e05dba2d22f438f3f1e71) )
    ROM_LOAD( "resource-0x54.bin", 0x390000, 0x58AA, CRC(369b10c2) SHA1(0fb6e046bcdbaf266da59347af9b16772906c46f) )
    ROM_LOAD( "resource-0x55.bin", 0x3a0000, 0x0990, CRC(64cdbc44) SHA1(8f9d488fcd5a3458b5854be340c014d9fc404a38) )
    ROM_LOAD( "resource-0x56.bin", 0x3b0000, 0x2C42, CRC(07457272) SHA1(f8eda4e48b340ce8f805f474ff18a3a93933e871) )
    ROM_LOAD( "resource-0x57.bin", 0x3c0000, 0x152C, CRC(e11dc02d) SHA1(98ea2fde3f4fe657ba70fd152cfa1c52d33e136a) )
    ROM_LOAD( "resource-0x58.bin", 0x3d0000, 0x05B4, CRC(b95ce570) SHA1(cea19fbcfd8fdfddc293b5f5f4d7c9ed94a7b0d5) )
    ROM_LOAD( "resource-0x59.bin", 0x3e0000, 0x23B4, CRC(337fad08) SHA1(4115be1cc05c97e68dd8ba4f70a10b00cfb4ab48) )
    ROM_LOAD( "resource-0x5a.bin", 0x3f0000, 0x1FA4, CRC(fabad21a) SHA1(79fceda09278c52530242c05c5f9d280083d2002) )
    ROM_LOAD( "resource-0x5b.bin", 0x400000, 0x0D20, CRC(928d6ed2) SHA1(2a22dd6f5f273306dde4425ceb40c03eb21a55d0) )
    ROM_LOAD( "resource-0x5c.bin", 0x410000, 0x0528, CRC(143de319) SHA1(133fae5024da5772fb32fde41491d58e680fb09e) )
    ROM_LOAD( "resource-0x5d.bin", 0x420000, 0x1608, CRC(814b714c) SHA1(a18f9a242316b49f5c29ebfbaa4e16caba676e9d) )
    ROM_LOAD( "resource-0x5e.bin", 0x430000, 0x01EA, CRC(d155e97d) SHA1(08c2b98495ed721a6951e046e12fcec2c2d2c6e4) )
    ROM_LOAD( "resource-0x5f.bin", 0x440000, 0x07EA, CRC(dcc0c6a0) SHA1(fe3978d33db6daf381dc4c44d6e19d6eaa3fc608) )
    ROM_LOAD( "resource-0x60.bin", 0x450000, 0x00E8, CRC(39a17088) SHA1(f03f02386ca1076d662b9b0c8e7ab064da133f4c) )
    ROM_LOAD( "resource-0x61.bin", 0x460000, 0x3978, CRC(ca534a10) SHA1(246c8eb599b8331d58be3bca7c0f807ca5b001f9) )
    ROM_LOAD( "resource-0x62.bin", 0x470000, 0x1178, CRC(864a3c3c) SHA1(fa9d60dd96d9d603e9867495c850ade54f364a96) )
    ROM_LOAD( "resource-0x63.bin", 0x480000, 0x14B0, CRC(8cbecf5b) SHA1(f7ead642795215b799d59a45fa88f782f1bf6e5e) )
    ROM_LOAD( "resource-0x64.bin", 0x490000, 0x0AA4, CRC(6dc08878) SHA1(3a3c7ffb0d7f05c4f4739688375d9de0b69c6f2c) )
    ROM_LOAD( "resource-0x65.bin", 0x4a0000, 0x02DA, CRC(f897d545) SHA1(ade7d0c63bb5d796778cedd79003b7bd0b23d204) )
    ROM_LOAD( "resource-0x66.bin", 0x4b0000, 0x2674, CRC(72b76acd) SHA1(88892cd9ac873a3a5aad409ef838d0e2329f3468) )
    ROM_LOAD( "resource-0x67.bin", 0x4c0000, 0x12F0, CRC(5641e052) SHA1(16950161c35c663c22bf6fcdf22878981be09c10) )
    ROM_LOAD( "resource-0x68.bin", 0x4d0000, 0x5D58, CRC(70ac0abc) SHA1(e60978d280601e182d667ed6f145c7764bc781ee) )
    ROM_LOAD( "resource-0x69.bin", 0x4e0000, 0xA222, CRC(d4abc6b3) SHA1(f57cff1a37ca52e393395ada12bb5e3963f17f58) )
    ROM_LOAD( "resource-0x6a.bin", 0x4f0000, 0x2E68, CRC(63944f2f) SHA1(42747182802a7f6f38a243afe4d2821150851ffd) )
    ROM_LOAD( "resource-0x6b.bin", 0x500000, 0x51C6, CRC(34b2f465) SHA1(9dcd19ea40de58d2761d34fddc40abb8cc565e2a) )
    ROM_LOAD( "resource-0x6c.bin", 0x510000, 0x13E6, CRC(eafe485d) SHA1(46f8788e0cd7c7c23c4ddc11530f3b3daef78b3d) )
    ROM_LOAD( "resource-0x6d.bin", 0x520000, 0x149E, CRC(74b46750) SHA1(224647901d1523a52b79639f0eb8302ba9dc82d9) )
    ROM_LOAD( "resource-0x6e.bin", 0x530000, 0x58AA, CRC(369b10c2) SHA1(0fb6e046bcdbaf266da59347af9b16772906c46f) )
    ROM_LOAD( "resource-0x6f.bin", 0x540000, 0x445C, CRC(9fbc37cc) SHA1(a4367d4f895815ed4aa95dfd9d3de6e7510ea435) )
    ROM_LOAD( "resource-0x70.bin", 0x550000, 0x0D90, CRC(9ba3cf4c) SHA1(c98fd8c68667d07d31ca68f5ef4d9c73d1975072) )
    ROM_LOAD( "resource-0x71.bin", 0x560000, 0x09E4, CRC(731c4396) SHA1(f33bcbbc80ad6baa87f1e141f707f182b927f764) )
    ROM_LOAD( "resource-0x72.bin", 0x570000, 0x198A, CRC(b80ebd82) SHA1(dc4723549fc84cad622aa62fdf4087222e885d9a) )
    ROM_LOAD( "resource-0x73.bin", 0x580000, 0x25D2, CRC(5a69c367) SHA1(7829750a8eb36489c903459ec6968e1e32039cc9) )
    ROM_LOAD( "resource-0x74.bin", 0x590000, 0x2430, CRC(b098e332) SHA1(bf23318557646a2c4baa2403df327c34a6fafc89) )
    ROM_LOAD( "resource-0x75.bin", 0x5a0000, 0x1316, CRC(6799486d) SHA1(d39f18abafefae82cb836af4beeece65fc866f01) )
    ROM_LOAD( "resource-0x76.bin", 0x5b0000, 0x0220, CRC(cc99c33b) SHA1(51619b25a95157feab5ef0e49764e3c15e5e925e) )
    ROM_LOAD( "resource-0x77.bin", 0x5c0000, 0x05EA, CRC(8bde4f5f) SHA1(e7074924926bee2fd09e087b7206b91241ed63ff) )
    ROM_LOAD( "resource-0x78.bin", 0x5d0000, 0x043C, CRC(e6670e02) SHA1(a717d4e53974ae208231a09e9469044acb8feddf) )
    ROM_LOAD( "resource-0x79.bin", 0x5e0000, 0x08EA, CRC(f6654b27) SHA1(f2f5cbb426845c6dfc6a3265cd9458321b64d5ba) )
    ROM_LOAD( "resource-0x7a.bin", 0x5f0000, 0x1478, CRC(be848cc6) SHA1(73c5f77cb876a1eec9a7feb1bd272611246bbc78) )
    ROM_LOAD( "resource-0x7b.bin", 0x600000, 0x432E, CRC(362f63fd) SHA1(99b0b4778db2cac5ba50cf8d4af1af2dec10ebef) )
    ROM_LOAD( "resource-0x7c.bin", 0x610000, 0x06CE, CRC(cb8a8c70) SHA1(9d3bd5266513c5bf57c74a1e5c4f5696f56ad9c5) )
    ROM_LOAD( "resource-0x80.bin", 0x620000, 0x189A, CRC(e945756c) SHA1(6be786d329155b92bf33abdaab23cf2f7f5f0324) )
    ROM_LOAD( "resource-0x81.bin", 0x630000, 0x07D8, CRC(ae03325d) SHA1(858c2ad7b36d3353c62771777310791b5e23716a) )
    ROM_LOAD( "resource-0x82.bin", 0x640000, 0x0462, CRC(35633c98) SHA1(31e002f572582fc1dc8cff58c5056ccac5d08dc6) )
    ROM_LOAD( "resource-0x83.bin", 0x650000, 0x0FA8, CRC(3a0dfbe6) SHA1(d8f551632cc824f105b33c75fc4958b427c9761d) )
    ROM_LOAD( "resource-0x84.bin", 0x660000, 0x672E, CRC(4798d58e) SHA1(4dff20828f335d6cefec8f4ff28125f1ecaae1d7) )
    ROM_LOAD( "resource-0x88.bin", 0x670000, 0x247C, CRC(700a39ed) SHA1(b2b87b9c1fb620ac9c5a6c107c9ad1342fafb3ab) )
    ROM_LOAD( "resource-0x89.bin", 0x680000, 0x08C0, CRC(87bbc004) SHA1(944af4f0459e07f2f800308bdeb158a39dccdce0) )
    ROM_LOAD( "resource-0x8a.bin", 0x690000, 0x3CC0, CRC(7431d60a) SHA1(a5beb5905db05d573fe693657be6d41548e5f3dc) )
    ROM_LOAD( "resource-0x8b.bin", 0x6a0000, 0x4F5A, CRC(87a99df3) SHA1(8895ffc5521004a4a99d297f10dd247746c3297c) )
    ROM_LOAD( "resource-0x8c.bin", 0x6b0000, 0x4418, CRC(a6d0aacf) SHA1(206869cb68ce4bd40fbcdae0c80efbceeeaca89a) )
    ROM_LOAD( "resource-0x8d.bin", 0x6c0000, 0x293C, CRC(05a228f0) SHA1(830c2507269781f908a1785e21b2d3caf1957abb) )
    ROM_LOAD( "resource-0x8e.bin", 0x6d0000, 0x3FC8, CRC(4cb7d9a4) SHA1(e200c96bd2726069da7e8fcb9599bf4b2e000b48) )
    ROM_LOAD( "resource-0x90.bin", 0x6e0000, 0x7D00, CRC(06bc3e04) SHA1(50d420f8d3d6166cbc691082fdd763cb8751746f) )
    ROM_LOAD( "resource-0x91.bin", 0x6f0000, 0x7D00, CRC(81449e16) SHA1(220bd63808dbc240472e1897eaae7d96ac726310) )
ROM_END

ROM_START(aw_amipk)
    /* These resources were extracted from the couple Amiga disk image files that came
       as a "BONUS" in a presskit release when "The Digital Lounge" unveiled the console
       versions of Another World 20th Anniversary Edition in 2011.
      
       The files were downloaded from:
       http://www.thedigitalounge.com/presskit.html
       http://www.thedigitalounge.com/dl/AnotherWorld-Retro-presskit.zip
     */

    ROM_REGION( 0x90000, "bytecode", ROMREGION_ERASEFF ) /* Amiga: Bytecode */
    ROM_LOAD( "resource-0x15.bin", 0x00000, 0x0DD8, CRC(256f1a59) SHA1(d63ff7333b44309be84719b59d8325773e8bbacc) )
    ROM_LOAD( "resource-0x18.bin", 0x10000, 0x2530, CRC(ceb83037) SHA1(90900a34cd892ca0143b42519f44e12f478363fc) )
    ROM_LOAD( "resource-0x1b.bin", 0x20000, 0x4C02, CRC(58632e5e) SHA1(9ebdcca0b5b06b8045d59430dc390dc839d7cffa) )
    ROM_LOAD( "resource-0x1e.bin", 0x30000, 0x98B6, CRC(44397d0c) SHA1(8d0d33e6d15dbe40d3999320ada3d5ac2aea080c) )
    ROM_LOAD( "resource-0x21.bin", 0x40000, 0xEE5E, CRC(0d32f584) SHA1(3aaa58fc9b792105bf810b1cf20551780d536bbd) )
    ROM_LOAD( "resource-0x24.bin", 0x50000, 0x1B00, CRC(2a37cffa) SHA1(367d3aae5401e10a386c382f3e4705f7ee4db504) )
    ROM_LOAD( "resource-0x27.bin", 0x60000, 0x99DC, CRC(70658790) SHA1(17ff6ed674d9fbc089095344f28dc770dc12b022) )
    ROM_LOAD( "resource-0x2a.bin", 0x70000, 0x09F4, CRC(2c5feaf3) SHA1(ec39742fe7f5dc1ee0bc48a1f4768e58994dc6eb) )
    ROM_LOAD( "resource-0x7e.bin", 0x80000, 0x0CC6, CRC(69502adb) SHA1(5605fcd4ccaeed38ef559beea5167f5c112dbad2) )

    ROM_REGION( 0x4800, "palettes", 0 ) /* Amiga: Palette */
    ROM_LOAD( "resource-0x14.bin", 0x0000, 0x800, CRC(9014fbcf) SHA1(25d4d0adf5d391c56470fd901ec543d10ff6e47a) )
    ROM_LOAD( "resource-0x17.bin", 0x0800, 0x800, CRC(9aad9bf2) SHA1(25629d95041e560279372ad1974a85ff3d976bec) )
    ROM_LOAD( "resource-0x1a.bin", 0x1000, 0x800, CRC(aa0a59d9) SHA1(9bfb05a6d90897e03b3b553160e7b575aaa58627) )
    ROM_LOAD( "resource-0x1d.bin", 0x1800, 0x800, CRC(cab53434) SHA1(da868275a18022b6d2add6172748a51b1822b5e3) )
    ROM_LOAD( "resource-0x20.bin", 0x2000, 0x800, CRC(076f9d3d) SHA1(92d95f3953b015f3d2d46c6ef2696a3658911009) )
    ROM_LOAD( "resource-0x23.bin", 0x2800, 0x800, CRC(9e89c08c) SHA1(bece0c50a9113c12da63895655a6e8328388564b) )
    ROM_LOAD( "resource-0x26.bin", 0x3000, 0x800, CRC(7d81ceb1) SHA1(e2067de55fd54533be04a4fcc6ead36debf414b0) )
    ROM_LOAD( "resource-0x29.bin", 0x3800, 0x800, CRC(ff34b1f5) SHA1(8bf1a6ffc206dda4c69f982e3589bb1bd593ba55) )
    ROM_LOAD( "resource-0x7d.bin", 0x4000, 0x800, CRC(479f78d5) SHA1(62a8f1863bc08747c7c57d337e95d0e2a7a01d31) )

    ROM_REGION( 0x90000, "video1", ROMREGION_ERASEFF ) /* Amiga: Cinematic */
    ROM_LOAD( "resource-0x16.bin", 0x00000, 0x1090, CRC(ed7318ea) SHA1(34067e81613335837de25b4564808d6b78ed43a6) )
    ROM_LOAD( "resource-0x19.bin", 0x10000, 0xFE7A, CRC(a3c05a66) SHA1(f6d04525399bc4945a675351619426b11dc846d5) )
    ROM_LOAD( "resource-0x1c.bin", 0x20000, 0xFDBA, CRC(c40cce3a) SHA1(5229b1dac15088152ec2be77c0ede002860d9c3c) )
    ROM_LOAD( "resource-0x1f.bin", 0x30000, 0xD1D8, CRC(a31c9019) SHA1(30a7f55125fbd91ef75a39b29c12673d83293633) )
    ROM_LOAD( "resource-0x22.bin", 0x40000, 0xFD08, CRC(3df7fdcb) SHA1(47e171bbe7ce1bac3936570f4af771e9b871f33e) )
    ROM_LOAD( "resource-0x25.bin", 0x50000, 0x5E58, CRC(624c667d) SHA1(8c5ad239f514b8eaddd3b069d18e80eb75c14735) )
    ROM_LOAD( "resource-0x28.bin", 0x60000, 0xFF9A, CRC(5a56d563) SHA1(799df15d736729004a9dffb390ad09c0d7b2369a) )
    ROM_LOAD( "resource-0x2b.bin", 0x70000, 0x4E3A, CRC(cf09c940) SHA1(edb0d045047769e3fd76b75da61b0d5d2afd99d9) )
    ROM_LOAD( "resource-0x7f.bin", 0x80000, 0x13B8, CRC(2a44528f) SHA1(e40b9d2b4a126d339668a2d713bfc48d73bb3fb8) )

    ROM_REGION( 0x8000, "video2", ROMREGION_ERASEFF ) /* Amiga: Video2 */
    ROM_LOAD( "resource-0x11.bin", 0x0000, 0x6214, CRC(2ea7976e) SHA1(93309c022be0f74064bf31618f8433b1c4d093dc) )

    ROM_REGION( 0x0300, "chargen", 0)
    ROM_LOAD( "anotherworld_chargen.rom", 0x0000, 0x0300, BAD_DUMP CRC(e2df8c47) SHA1(b79b41835aa2d5747932f8080bb6fb2cf32837d7) )
    /* The chargen ROM was copied from the MSDOS version.
     * TODO: extract the font from the amiga version and confirm it is the same.
     */

    ROM_REGION( 0x700000, "samples", ROMREGION_ERASEFF )
    /* Amiga: Unknown resources.
     *        I guess most of these are sound samples (maybe all?!)
     */    
    ROM_LOAD( "resource-0x01.bin", 0x000000, 0x1A3C, CRC(baa9a633) SHA1(6d76db3a050bbb6696a7a24048144aa0820c354a) )
    ROM_LOAD( "resource-0x02.bin", 0x010000, 0x2E34, CRC(117f183a) SHA1(b39c6b22df38f0f64b2e86dc0b002e39071eaf3c) )
    ROM_LOAD( "resource-0x03.bin", 0x020000, 0x69F8, CRC(49526d55) SHA1(2204c21463852bfbc47f65862e4d7b5bfcfec2ef) )
    ROM_LOAD( "resource-0x04.bin", 0x030000, 0x45CE, CRC(6639098a) SHA1(053e725590b4d30b36f584cf12d7f9b1fc7798e2) )
    ROM_LOAD( "resource-0x05.bin", 0x040000, 0x0EFA, CRC(ce3ede40) SHA1(1d72e4f81d6ee2134f176e1cb1e8f9eb6a5ff495) )
    ROM_LOAD( "resource-0x06.bin", 0x050000, 0x0D26, CRC(fa97ccff) SHA1(f18501867e2e5f7e256e9c4410d626e4355235c9) )
    ROM_LOAD( "resource-0x07.bin", 0x060000, 0x3CC0, CRC(c2fc8e2f) SHA1(1b1420412826e73294d8fff826d323aeaaca5214) )
    ROM_LOAD( "resource-0x08.bin", 0x070000, 0x2674, CRC(72b76acd) SHA1(88892cd9ac873a3a5aad409ef838d0e2329f3468) )
    ROM_LOAD( "resource-0x09.bin", 0x080000, 0x2BB6, CRC(fbf55601) SHA1(40ca27866ec62815e6ace999dedaee4b0b21ce8b) )
    ROM_LOAD( "resource-0x0a.bin", 0x090000, 0x2BB4, CRC(1c60352e) SHA1(972a49972fdbf889e519a42d6bdfe15cec2dd87c) )
    ROM_LOAD( "resource-0x0b.bin", 0x0a0000, 0x0426, CRC(f47dcea3) SHA1(ce0d8a56f80fdb3324d4415fabf86319d32fb290) )
    ROM_LOAD( "resource-0x0c.bin", 0x0b0000, 0x1852, CRC(e2dfed36) SHA1(f650ea9596a18b3536cec9d1dceef3c1bf6d6a70) )
    ROM_LOAD( "resource-0x0d.bin", 0x0c0000, 0x0594, CRC(c0dd33e4) SHA1(25f327737082969deece6ba98ac99e1623c95cb7) )
    ROM_LOAD( "resource-0x0e.bin", 0x0d0000, 0x13F0, CRC(cff8499b) SHA1(0c061f787d2acfd4a64deb0bc1e2c506b90b7db6) )
    ROM_LOAD( "resource-0x0f.bin", 0x0e0000, 0x079E, CRC(c7c2353f) SHA1(39ec64e4ce72171557413a9c1d7c0106426520f7) )
    ROM_LOAD( "resource-0x10.bin", 0x0f0000, 0x56A2, CRC(4d9db7c8) SHA1(78fb2762b40acf85fe0133d96d1325ec039a0525) )
    ROM_LOAD( "resource-0x12.bin", 0x100000, 0x7D00, BAD_DUMP CRC(3245f257) SHA1(9078b9a8cab39d495feca2e6cdb2fd54d2926428) ) /* copied from MSDOS version */
    ROM_LOAD( "resource-0x13.bin", 0x110000, 0x7D00, BAD_DUMP CRC(f74291cf) SHA1(41a357c9f3823b5af94448f4d6894c706bf33b35) ) /* copied from MSDOS version */
    ROM_LOAD( "resource-0x2c.bin", 0x120000, 0x0372, CRC(74d1ca59) SHA1(a51f11f9fcb4d1e2121d99f4208d8b0ad1dd6d3a) )
    ROM_LOAD( "resource-0x2d.bin", 0x130000, 0x1E04, CRC(23de4dd5) SHA1(d5fb5f7bf553ed4aeb4539443a304d24d8d11da0) )
    ROM_LOAD( "resource-0x2e.bin", 0x140000, 0x08EA, CRC(f6654b27) SHA1(f2f5cbb426845c6dfc6a3265cd9458321b64d5ba) )
    ROM_LOAD( "resource-0x2f.bin", 0x150000, 0x1A46, CRC(a5b0f7a5) SHA1(9145af546a9fb5970a595f6196914d7102d47f79) )
    ROM_LOAD( "resource-0x30.bin", 0x160000, 0x343E, CRC(2d5ea830) SHA1(70e336c95d9be763f690b6683a9477b41e5a1522) )
    ROM_LOAD( "resource-0x31.bin", 0x170000, 0x149E, CRC(74b46750) SHA1(224647901d1523a52b79639f0eb8302ba9dc82d9) )
    ROM_LOAD( "resource-0x32.bin", 0x180000, 0x1866, CRC(f15d1810) SHA1(50ff08a64090b3429ade13a9873c2b17319dee36) )
    ROM_LOAD( "resource-0x33.bin", 0x190000, 0x0266, CRC(b0dd8e22) SHA1(324c2a036c8d383991f7db413a90b72cf07b3a0f) )
    ROM_LOAD( "resource-0x35.bin", 0x1a0000, 0x01A8, CRC(947b7c3c) SHA1(62b453bc89e641156f22ff3d3adf2e438e928687) )
    ROM_LOAD( "resource-0x36.bin", 0x1b0000, 0x1FEC, CRC(0fa4219a) SHA1(317c02d8d8ace986ade0c0e20dc17b58988c685a) )
    ROM_LOAD( "resource-0x37.bin", 0x1c0000, 0x13A4, CRC(70599496) SHA1(29ad38779a663fb38d8e1b7bc8554379af40a7a9) )
    ROM_LOAD( "resource-0x38.bin", 0x1d0000, 0x15C4, CRC(6bea3d6d) SHA1(bf568a126b09a40d9dddd99059d047ee91c98b24) )
    ROM_LOAD( "resource-0x39.bin", 0x1e0000, 0x0E2A, CRC(de344a15) SHA1(bfe23d637153a35e2df0cd85a9759b996bdd1a99) )
    ROM_LOAD( "resource-0x3a.bin", 0x1f0000, 0x0366, CRC(4283476b) SHA1(ad66ee0a3d7ea3f8b2abffabdfc12435d0ae8777) )
    ROM_LOAD( "resource-0x3b.bin", 0x200000, 0x0078, CRC(ef77075e) SHA1(ff276ae093e9f43ac929e4ad9bd165699a3f5100) )
    ROM_LOAD( "resource-0x3c.bin", 0x210000, 0x1392, CRC(dfa9363a) SHA1(062423ff50e1f47b08636a1061f8f7ad92dbd9ee) )
    ROM_LOAD( "resource-0x3d.bin", 0x220000, 0x06E0, CRC(e6ba6acd) SHA1(9b750dcfa55fd6a681bbe842488785b053315c93) )
    ROM_LOAD( "resource-0x3e.bin", 0x230000, 0x21AE, CRC(d6138ce1) SHA1(d1f23331adb4cfe877833d15cd541d069b1a0bb7) )
    ROM_LOAD( "resource-0x3f.bin", 0x240000, 0x04FA, CRC(ef6b6f92) SHA1(148a0f86b07d355aeabc5c33cba7ae198745845e) )
    ROM_LOAD( "resource-0x40.bin", 0x250000, 0x129E, CRC(152c5abc) SHA1(c38417a9609a8961f34b3157218581d250572633) )
    ROM_LOAD( "resource-0x41.bin", 0x260000, 0x09B4, CRC(6f061599) SHA1(00b617367adc8dc60cffa31b84d9433cd8ec2306) )
    ROM_LOAD( "resource-0x42.bin", 0x270000, 0x04EC, CRC(1fd8273d) SHA1(128167f1c895431720944e583a114d1a792de813) )
    ROM_LOAD( "resource-0x43.bin", 0x280000, 0x7D00, CRC(f239adf3) SHA1(d95192c09eb03e51c829dad35497feb9c7a3690a) )
    ROM_LOAD( "resource-0x44.bin", 0x290000, 0x7D00, CRC(3b98dab3) SHA1(c85ec1b18f08c8704c9bac05a673246e6c1b752a) )
    ROM_LOAD( "resource-0x45.bin", 0x2a0000, 0x7D00, CRC(24a0cec9) SHA1(5cd9647773e57f241ced52518ac9eeac38d18de8) )
    ROM_LOAD( "resource-0x46.bin", 0x2b0000, 0x7D00, CRC(d9f161ce) SHA1(efa5c70e193e467a1fab11e133179a3e12dc428f) )
    ROM_LOAD( "resource-0x47.bin", 0x2c0000, 0x7D00, CRC(ddb21ee0) SHA1(981f7fa7459b7e127934764dcfff750d46c43da9) )
    ROM_LOAD( "resource-0x48.bin", 0x2d0000, 0x7D00, CRC(3054c09c) SHA1(0b646a8e772cc9e68fe5250c415c379fc816daaa) )
    ROM_LOAD( "resource-0x49.bin", 0x2e0000, 0x7D00, CRC(23180593) SHA1(0237a0c7321b02dd6e8ff875050bb47ae518041f) )
    ROM_LOAD( "resource-0x4a.bin", 0x2f0000, 0x03C0, CRC(3aef65eb) SHA1(fa59ec97e037e0d6f70dff519041c986b396d2c6) )
    ROM_LOAD( "resource-0x4b.bin", 0x300000, 0x13E6, CRC(eafe485d) SHA1(46f8788e0cd7c7c23c4ddc11530f3b3daef78b3d) )
    ROM_LOAD( "resource-0x4c.bin", 0x310000, 0x04DE, CRC(87bb26d7) SHA1(3869adc5b0e15f98bd723b9d86cc45d567036386) )
    ROM_LOAD( "resource-0x4d.bin", 0x320000, 0x05FA, CRC(12d3fd86) SHA1(98ceb97e85f0f6bbebd51669e63b6cb94a15b154) )
    ROM_LOAD( "resource-0x4e.bin", 0x330000, 0x025E, CRC(99049d27) SHA1(c8114836edd34ef07fa73ad8928bd690bae82d89) )
    ROM_LOAD( "resource-0x4f.bin", 0x340000, 0x0642, CRC(d670a518) SHA1(f4fd152cc3b88099b2072a46585ab12262d7b5e2) )
    ROM_LOAD( "resource-0x50.bin", 0x350000, 0x19D0, CRC(dd0fbb7c) SHA1(1425d0f37b9a9c23f2cdc8aea1098d52701c1141) )
    ROM_LOAD( "resource-0x51.bin", 0x360000, 0x00E8, CRC(39a17088) SHA1(f03f02386ca1076d662b9b0c8e7ab064da133f4c) )
    ROM_LOAD( "resource-0x52.bin", 0x370000, 0x1022, CRC(4804c511) SHA1(7b2ff405ab9cf4d72541c4d8ab88f6197a1b985c) )
    ROM_LOAD( "resource-0x53.bin", 0x380000, 0x7D00, CRC(59fd56ff) SHA1(5c85d468368436f4934e05dba2d22f438f3f1e71) )
    ROM_LOAD( "resource-0x54.bin", 0x390000, 0x58AA, CRC(369b10c2) SHA1(0fb6e046bcdbaf266da59347af9b16772906c46f) )
    ROM_LOAD( "resource-0x55.bin", 0x3a0000, 0x0990, CRC(64cdbc44) SHA1(8f9d488fcd5a3458b5854be340c014d9fc404a38) )
    ROM_LOAD( "resource-0x56.bin", 0x3b0000, 0x2C42, CRC(07457272) SHA1(f8eda4e48b340ce8f805f474ff18a3a93933e871) )
    ROM_LOAD( "resource-0x57.bin", 0x3c0000, 0x152C, CRC(e11dc02d) SHA1(98ea2fde3f4fe657ba70fd152cfa1c52d33e136a) )
    ROM_LOAD( "resource-0x58.bin", 0x3d0000, 0x05B4, CRC(b95ce570) SHA1(cea19fbcfd8fdfddc293b5f5f4d7c9ed94a7b0d5) )
    ROM_LOAD( "resource-0x59.bin", 0x3e0000, 0x23B4, CRC(337fad08) SHA1(4115be1cc05c97e68dd8ba4f70a10b00cfb4ab48) )
    ROM_LOAD( "resource-0x5a.bin", 0x3f0000, 0x1FA4, CRC(fabad21a) SHA1(79fceda09278c52530242c05c5f9d280083d2002) )
    ROM_LOAD( "resource-0x5b.bin", 0x400000, 0x0D20, CRC(928d6ed2) SHA1(2a22dd6f5f273306dde4425ceb40c03eb21a55d0) )
    ROM_LOAD( "resource-0x5c.bin", 0x410000, 0x0528, CRC(143de319) SHA1(133fae5024da5772fb32fde41491d58e680fb09e) )
    ROM_LOAD( "resource-0x5d.bin", 0x420000, 0x1608, CRC(814b714c) SHA1(a18f9a242316b49f5c29ebfbaa4e16caba676e9d) )
    ROM_LOAD( "resource-0x5e.bin", 0x430000, 0x01EA, CRC(d155e97d) SHA1(08c2b98495ed721a6951e046e12fcec2c2d2c6e4) )
    ROM_LOAD( "resource-0x5f.bin", 0x440000, 0x07EA, CRC(dcc0c6a0) SHA1(fe3978d33db6daf381dc4c44d6e19d6eaa3fc608) )
    ROM_LOAD( "resource-0x60.bin", 0x450000, 0x00E8, CRC(39a17088) SHA1(f03f02386ca1076d662b9b0c8e7ab064da133f4c) )
    ROM_LOAD( "resource-0x61.bin", 0x460000, 0x3978, CRC(ca534a10) SHA1(246c8eb599b8331d58be3bca7c0f807ca5b001f9) )
    ROM_LOAD( "resource-0x62.bin", 0x470000, 0x1178, CRC(864a3c3c) SHA1(fa9d60dd96d9d603e9867495c850ade54f364a96) )
    ROM_LOAD( "resource-0x63.bin", 0x480000, 0x14B0, CRC(8cbecf5b) SHA1(f7ead642795215b799d59a45fa88f782f1bf6e5e) )
    ROM_LOAD( "resource-0x64.bin", 0x490000, 0x0AA4, CRC(6dc08878) SHA1(3a3c7ffb0d7f05c4f4739688375d9de0b69c6f2c) )
    ROM_LOAD( "resource-0x65.bin", 0x4a0000, 0x02DA, CRC(f897d545) SHA1(ade7d0c63bb5d796778cedd79003b7bd0b23d204) )
    ROM_LOAD( "resource-0x66.bin", 0x4b0000, 0x2674, CRC(72b76acd) SHA1(88892cd9ac873a3a5aad409ef838d0e2329f3468) )
    ROM_LOAD( "resource-0x67.bin", 0x4c0000, 0x12F0, CRC(5641e052) SHA1(16950161c35c663c22bf6fcdf22878981be09c10) )
    ROM_LOAD( "resource-0x68.bin", 0x4d0000, 0x5D58, CRC(70ac0abc) SHA1(e60978d280601e182d667ed6f145c7764bc781ee) )
    ROM_LOAD( "resource-0x69.bin", 0x4e0000, 0xA222, CRC(d4abc6b3) SHA1(f57cff1a37ca52e393395ada12bb5e3963f17f58) )
    ROM_LOAD( "resource-0x6a.bin", 0x4f0000, 0x2E68, CRC(63944f2f) SHA1(42747182802a7f6f38a243afe4d2821150851ffd) )
    ROM_LOAD( "resource-0x6b.bin", 0x500000, 0x51C6, CRC(34b2f465) SHA1(9dcd19ea40de58d2761d34fddc40abb8cc565e2a) )
    ROM_LOAD( "resource-0x6c.bin", 0x510000, 0x13E6, CRC(eafe485d) SHA1(46f8788e0cd7c7c23c4ddc11530f3b3daef78b3d) )
    ROM_LOAD( "resource-0x6d.bin", 0x520000, 0x149E, CRC(74b46750) SHA1(224647901d1523a52b79639f0eb8302ba9dc82d9) )
    ROM_LOAD( "resource-0x6e.bin", 0x530000, 0x58AA, CRC(369b10c2) SHA1(0fb6e046bcdbaf266da59347af9b16772906c46f) )
    ROM_LOAD( "resource-0x6f.bin", 0x540000, 0x445C, CRC(9fbc37cc) SHA1(a4367d4f895815ed4aa95dfd9d3de6e7510ea435) )
    ROM_LOAD( "resource-0x70.bin", 0x550000, 0x0D90, CRC(9ba3cf4c) SHA1(c98fd8c68667d07d31ca68f5ef4d9c73d1975072) )
    ROM_LOAD( "resource-0x71.bin", 0x560000, 0x09E4, CRC(731c4396) SHA1(f33bcbbc80ad6baa87f1e141f707f182b927f764) )
    ROM_LOAD( "resource-0x72.bin", 0x570000, 0x198A, CRC(b80ebd82) SHA1(dc4723549fc84cad622aa62fdf4087222e885d9a) )
    ROM_LOAD( "resource-0x73.bin", 0x580000, 0x25D2, CRC(5a69c367) SHA1(7829750a8eb36489c903459ec6968e1e32039cc9) )
    ROM_LOAD( "resource-0x74.bin", 0x590000, 0x2430, CRC(b098e332) SHA1(bf23318557646a2c4baa2403df327c34a6fafc89) )
    ROM_LOAD( "resource-0x75.bin", 0x5a0000, 0x1316, CRC(6799486d) SHA1(d39f18abafefae82cb836af4beeece65fc866f01) )
    ROM_LOAD( "resource-0x76.bin", 0x5b0000, 0x0220, CRC(cc99c33b) SHA1(51619b25a95157feab5ef0e49764e3c15e5e925e) )
    ROM_LOAD( "resource-0x77.bin", 0x5c0000, 0x05EA, CRC(8bde4f5f) SHA1(e7074924926bee2fd09e087b7206b91241ed63ff) )
    ROM_LOAD( "resource-0x78.bin", 0x5d0000, 0x043C, CRC(e6670e02) SHA1(a717d4e53974ae208231a09e9469044acb8feddf) )
    ROM_LOAD( "resource-0x79.bin", 0x5e0000, 0x08EA, CRC(f6654b27) SHA1(f2f5cbb426845c6dfc6a3265cd9458321b64d5ba) )
    ROM_LOAD( "resource-0x7a.bin", 0x5f0000, 0x1478, CRC(be848cc6) SHA1(73c5f77cb876a1eec9a7feb1bd272611246bbc78) )
    ROM_LOAD( "resource-0x7b.bin", 0x600000, 0x432E, CRC(362f63fd) SHA1(99b0b4778db2cac5ba50cf8d4af1af2dec10ebef) )
    ROM_LOAD( "resource-0x7c.bin", 0x610000, 0x06CE, CRC(cb8a8c70) SHA1(9d3bd5266513c5bf57c74a1e5c4f5696f56ad9c5) )
    ROM_LOAD( "resource-0x80.bin", 0x620000, 0x189A, CRC(e945756c) SHA1(6be786d329155b92bf33abdaab23cf2f7f5f0324) )
    ROM_LOAD( "resource-0x81.bin", 0x630000, 0x07D8, CRC(ae03325d) SHA1(858c2ad7b36d3353c62771777310791b5e23716a) )
    ROM_LOAD( "resource-0x82.bin", 0x640000, 0x0462, CRC(35633c98) SHA1(31e002f572582fc1dc8cff58c5056ccac5d08dc6) )
    ROM_LOAD( "resource-0x83.bin", 0x650000, 0x0FA8, CRC(3a0dfbe6) SHA1(d8f551632cc824f105b33c75fc4958b427c9761d) )
    ROM_LOAD( "resource-0x84.bin", 0x660000, 0x672E, CRC(4798d58e) SHA1(4dff20828f335d6cefec8f4ff28125f1ecaae1d7) )
    ROM_LOAD( "resource-0x88.bin", 0x670000, 0x247C, CRC(700a39ed) SHA1(b2b87b9c1fb620ac9c5a6c107c9ad1342fafb3ab) )
    ROM_LOAD( "resource-0x89.bin", 0x680000, 0x08C0, CRC(87bbc004) SHA1(944af4f0459e07f2f800308bdeb158a39dccdce0) )
    ROM_LOAD( "resource-0x8a.bin", 0x690000, 0x3CC0, CRC(7431d60a) SHA1(a5beb5905db05d573fe693657be6d41548e5f3dc) )
    ROM_LOAD( "resource-0x8b.bin", 0x6a0000, 0x4F5A, CRC(87a99df3) SHA1(8895ffc5521004a4a99d297f10dd247746c3297c) )
    ROM_LOAD( "resource-0x8c.bin", 0x6b0000, 0x4418, CRC(a6d0aacf) SHA1(206869cb68ce4bd40fbcdae0c80efbceeeaca89a) )
    ROM_LOAD( "resource-0x8d.bin", 0x6c0000, 0x293C, CRC(05a228f0) SHA1(830c2507269781f908a1785e21b2d3caf1957abb) )
    ROM_LOAD( "resource-0x8e.bin", 0x6d0000, 0x3FC8, CRC(4cb7d9a4) SHA1(e200c96bd2726069da7e8fcb9599bf4b2e000b48) )
    ROM_LOAD( "resource-0x90.bin", 0x6e0000, 0x7D00, CRC(06bc3e04) SHA1(50d420f8d3d6166cbc691082fdd763cb8751746f) )
    ROM_LOAD( "resource-0x91.bin", 0x6f0000, 0x7D00, CRC(81449e16) SHA1(220bd63808dbc240472e1897eaae7d96ac726310) )
ROM_END

/*    YEAR  NAME      PARENT    COMPAT  MACHINE        INPUT          INIT                                COMPANY              FULLNAME */
COMP( 199?, aw_msdos, 0,        0,      another_world, another_world, another_world_state, another_world, "Delphine Software", "Another World (VM) - MSDOS" , MACHINE_NOT_WORKING)
COMP( 199?, aw_amipk, 0,        0,      another_world, another_world, another_world_state, another_world, "Delphine Software", "Another World (VM) - Amiga version - nologo - noprotec (from 2011 presskit 'bonus')" , MACHINE_NOT_WORKING)