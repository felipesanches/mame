#include "anotherw_vm.h"

// device type definition
DEFINE_DEVICE_TYPE(ANOTHERW_VM_SOUND, anotherw_vm_sound_device, "anotherw_vm", "Another World VM Sound Hardware")

//TODO: Maybe this should be stored in a ROM asset as well?
static uint8_t resource_indexes[] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
	0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33,
	0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c,
	0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x4a, 0x4b,
	0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x54,
	0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c,
	0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64,
	0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c,
	0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74,
	0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c,
	0x80, 0x81, 0x82, 0x83, 0x84, 0x88, 0x89, 0x8a,
	0x8b, 0x8c, 0x8d, 0x8e
};


static int32_t resource_offset(uint16_t resNum)
{
	for (int i=0; i<sizeof(resource_indexes); i++)
		if (resNum == resource_indexes[i])
			return i;

	printf("ERROR! Resource 0x%X not found!\n", resNum);
	return -1;
}


//-------------------------------------------------
//  anotherw_sound_device - constructor
//-------------------------------------------------

anotherw_vm_sound_device::anotherw_vm_sound_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, ANOTHERW_VM_SOUND, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, m_stream(nullptr)
	, m_write_music_mark(*this)
{
}


//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void anotherw_vm_sound_device::device_start()
{
	//resolve callbacks
	m_write_music_mark.resolve_safe();

	// create the stream
	m_stream = stream_alloc(0, 1, clock());

	memset(m_channels, 0, sizeof(m_channels));

	m_samples_base_ptr = owner()->memregion("samples")->base();
	m_player = new VMSfxPlayer(this);
	m_timer = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(anotherw_vm_sound_device::musicPlayerCallback),this));

	save_item(STRUCT_MEMBER(m_channels, m_active));
	save_item(STRUCT_MEMBER(m_channels, m_volume));
	save_item(STRUCT_MEMBER(m_channels, m_chunkPos));
	save_item(STRUCT_MEMBER(m_channels, m_chunkInc));
}

//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void anotherw_vm_sound_device::device_reset()
{
	m_stream->update();
	for (auto & elem : m_channels)
		elem.m_active = false;
}


//-------------------------------------------------
//  device_post_load - device-specific post-load
//-------------------------------------------------

void anotherw_vm_sound_device::device_post_load()
{
	device_clock_changed();
}


//-------------------------------------------------
//  device_clock_changed - called if the clock
//  changes
//-------------------------------------------------

void anotherw_vm_sound_device::device_clock_changed()
{
	m_stream->set_sample_rate(clock());
}


TIMER_CALLBACK_MEMBER(anotherw_vm_sound_device::musicPlayerCallback)
{
	m_player->handleEvents();
}


static int16_t addclamp(int a, int b)
{
	int add = a + b;
	if (add < -32768)
	{
		add = -32768;
	}
	else if (add > 32767)
	{
		add = 32767;
	}
	return (int16_t)add;
}


//-------------------------------------------------
//  stream_generate - handle update requests for
//  our sound stream
//-------------------------------------------------

void anotherw_vm_sound_device::sound_stream_update(sound_stream &stream, std::vector<read_stream_view> const &inputs, std::vector<write_stream_view> &outputs)
{
	auto &buffer = outputs[0];

	// loop while we still have samples to generate
	for (int sampleindex = 0; sampleindex < buffer.samples(); sampleindex++)
	{
		int16_t value = 0;
		// iterate over channels and accumulate sample data
		for (auto & channel : m_channels)
		{
			// skip if not active
			if (!channel.m_active) continue;

			uint16_t p1, p2;
			uint16_t ilc = (channel.m_chunkPos & 0xFF);  // ilc = interpolation counter
			p1 = channel.m_chunkPos >> 8;
			channel.m_chunkPos += channel.m_chunkInc;

			if (channel.m_chunk.loopLen != 0) {
				if (p1 == channel.m_chunk.loopPos + channel.m_chunk.loopLen - 1) {
					// Looping sample
					p2 = channel.m_chunk.loopPos;
					channel.m_chunkPos = p2 << 8;
				} else {
					p2 = p1 + 1;
				}
			} else {
				if (p1 == channel.m_chunk.len - 1) {
					// Stopping sample
					channel.m_active = false;
					break;
				} else {
					p2 = p1 + 1;
				}
			}

			// interpolate
			int8_t b1 = *(int8_t *)(channel.m_chunk.data + p1);
			int8_t b2 = *(int8_t *)(channel.m_chunk.data + p2);
			int8_t b = (int8_t)((b1 * (0xFF - ilc) + b2 * ilc) >> 8);

			// set volume and clamp
			value = addclamp(value, ((int)b << 8) * channel.m_volume / 0x40);
		}
		buffer.put(sampleindex, value);
	}
}

#define MIN(a, b) (a<b ? a : b)
void anotherw_vm_sound_device::playSound(uint8_t channel, uint16_t resNum, uint8_t freq, uint8_t vol)
{
	if (vol == 0)
		stopChannel(channel);
	else
	{
		int32_t offset = resource_offset(resNum);
		if (offset < 0)
			return;

		const uint8_t * samples = m_samples_base_ptr + offset * 0x10000;

		struct MixerChunk mc;
		memset(&mc, 0, sizeof(mc));
		mc.data = samples + 8; // skip header
		mc.len = READ_BE_UINT16(samples) * 2;
		mc.loopLen = READ_BE_UINT16(samples+2) * 2;
		if (mc.loopLen != 0)
			mc.loopPos = mc.len;

		assert(freq < 40);
		playChannel(channel & 3, &mc, frequenceTable[freq], MIN(vol, 0x3F));
	}
}


#define OUTPUT_SAMPLE_RATE 31677
void anotherw_vm_sound_device::playChannel(uint8_t channel, const MixerChunk *mc, uint16_t freq, uint8_t volume)
{
	assert(channel < ANOTHERW_CHANNELS);

	anotherw_channel *ch = &m_channels[channel];
	ch->m_active = true;
	ch->m_volume = volume;
	ch->m_chunk = *mc;
	ch->m_chunkPos = 0;
	ch->m_chunkInc = (freq << 8) / OUTPUT_SAMPLE_RATE;
}


void anotherw_vm_sound_device::stopChannel(uint8_t channel)
{
	assert(channel < ANOTHERW_CHANNELS);
	m_channels[channel].m_active = false;
}


void anotherw_vm_sound_device::setChannelVolume(uint8_t channel, uint8_t volume)
{
	assert(channel < ANOTHERW_CHANNELS);
	m_channels[channel].m_volume = volume;
}


void anotherw_vm_sound_device::stopAll()
{
	for (uint8_t i = 0; i < ANOTHERW_CHANNELS; ++i)
		m_channels[i].m_active = false;
}


//**************************************************************************
//  ANOTHERW CHANNEL
//**************************************************************************

//-------------------------------------------------
//  anotherw_channel - constructor
//-------------------------------------------------

anotherw_vm_sound_device::anotherw_channel::anotherw_channel()
	: m_active(false)
	, m_volume(0)
{
}


//-------------------------------------------------
//  generate_samples and
//  add them to an output stream
//-------------------------------------------------

const uint16_t anotherw_vm_sound_device::frequenceTable[] = {
	0x0CFF, 0x0DC3, 0x0E91, 0x0F6F, 0x1056, 0x114E, 0x1259, 0x136C,
	0x149F, 0x15D9, 0x1726, 0x1888, 0x19FD, 0x1B86, 0x1D21, 0x1EDE,
	0x20AB, 0x229C, 0x24B3, 0x26D7, 0x293F, 0x2BB2, 0x2E4C, 0x3110,
	0x33FB, 0x370D, 0x3A43, 0x3DDF, 0x4157, 0x4538, 0x4998, 0x4DAE,
	0x5240, 0x5764, 0x5C9A, 0x61C8, 0x6793, 0x6E19, 0x7485, 0x7BBD
};


void anotherw_vm_sound_device::music_mark(uint8_t data)
{
	m_write_music_mark(data);
}


/****************
 * Music Player *
 ****************/

VMSfxPlayer::VMSfxPlayer(anotherw_vm_sound_device *mixer)
	: m_mixer(mixer), m_delay(0), m_resNum(0)
{
	/*******************************************************************
	TODO:

	mixer->save_item(NAME(m_delay));
	mixer->save_item(NAME(m_resNum));
	mixer->save_item(STRUCT_MEMBER(m_sfxMod, curPos));
	mixer->save_item(STRUCT_MEMBER(m_sfxMod, curOrder));
	mixer->save_item(STRUCT_MEMBER(m_sfxMod, numOrder));
	mixer->save_pointer(NAME(m_sfxMod.orderTable), 0x80);
	mixer->save_pointer(STRUCT_MEMBER(m_sfxMod.samples, volume), 15);
	
	I also think code will need to be restructured to reload
	the data fields on these structs based on saved-state:
	
	VMSfxModule -> uint8_t *data
	VMSfxInstrument -> uint8_t *data

	*******************************************************************/
}


void VMSfxPlayer::initPlayer()
{
}


void VMSfxPlayer::freePlayer()
{
	stop();
}


void VMSfxPlayer::setEventsDelay(uint16_t delay)
{
	m_delay = delay * 60 / 7050;
}


int VMSfxPlayer::loadSfxModule(uint16_t resNum, uint16_t delay, uint8_t pos)
{
	printf("loadSfxModule delay:%d, pos:%d\n", delay, pos);
	int32_t offset = resource_offset(resNum);
	if (offset < 0)
		return -1;

	const uint8_t * resource_ptr = ((uint8_t*) m_mixer->m_samples_base_ptr) + offset * 0x10000;

	m_resNum = resNum;
	memset(&m_sfxMod, 0, sizeof(VMSfxModule));
	m_sfxMod.curOrder = pos;
	m_sfxMod.numOrder = READ_BE_UINT16(resource_ptr + 0x3E);

	for (int i = 0; i < 0x80; ++i)
		m_sfxMod.orderTable[i] = *(resource_ptr + 0x40 + i);

	if (delay != 0)
		setEventsDelay(delay);
	else
		setEventsDelay(READ_BE_UINT16(resource_ptr));

	m_sfxMod.data = resource_ptr + 0xC0;
	prepareInstruments(resource_ptr + 2);

	return 0;
}


void VMSfxPlayer::prepareInstruments(const uint8_t *p)
{
	memset(m_sfxMod.samples, 0, sizeof(m_sfxMod.samples));

	for (int i = 0; i < 15; ++i)
	{
		VMSfxInstrument *ins = &m_sfxMod.samples[i];
		uint16_t resNum = READ_BE_UINT16(p); p += 2;

		if (resNum != 0)
		{
			int32_t offset = resource_offset(resNum);
			if (offset < 0)
				return;

			ins->volume = READ_BE_UINT16(p);
			ins->data = ((uint8_t*) m_mixer->m_samples_base_ptr) + offset * 0x10000;
			//memset(ins->data + 8, 0, 4);
		}
		p += 2; // skip volume
	}
}


void VMSfxPlayer::start()
{
	m_sfxMod.curPos = 0;
	m_mixer->m_timer->adjust(attotime::from_msec(m_delay), 0, attotime::from_msec(m_delay));
}


void VMSfxPlayer::stop()
{
	if (m_resNum != 0)
	{
		m_resNum = 0;
		m_mixer->m_timer->adjust(attotime::zero);
	}
}


void VMSfxPlayer::handleEvents()
{
	uint8_t order = m_sfxMod.orderTable[m_sfxMod.curOrder];
	const uint8_t *patternData = m_sfxMod.data + m_sfxMod.curPos + order * 1024;
	for (uint8_t ch = 0; ch < 4; ++ch)
	{
		handlePattern(ch, patternData);
		patternData += 4;
	}
	m_sfxMod.curPos += (4 * 4);

	if (m_sfxMod.curPos >= 1024)
	{
		m_sfxMod.curPos = 0;
		order = m_sfxMod.curOrder + 1;
		if (order == m_sfxMod.numOrder)
		{
			m_resNum = 0;
			m_mixer->m_timer->adjust(attotime::never);
			m_mixer->stopAll();
		}
		m_sfxMod.curOrder = order;
	}
}


void VMSfxPlayer::handlePattern(uint8_t channel, const uint8_t *data)
{
	VMSfxPattern pat;
	memset(&pat, 0, sizeof(VMSfxPattern));
	pat.note_1 = READ_BE_UINT16(data + 0);
	pat.note_2 = READ_BE_UINT16(data + 2);
	if (pat.note_1 != 0xFFFD)
	{
		uint16_t sample = (pat.note_2 & 0xF000) >> 12;
		if (sample != 0)
		{
			uint8_t *ptr = m_sfxMod.samples[sample - 1].data;
			if (ptr != 0)
			{
				pat.sampleVolume = m_sfxMod.samples[sample - 1].volume;
				pat.sampleStart = 8;
				pat.sampleBuffer = ptr;
				pat.sampleLen = READ_BE_UINT16(ptr) * 2;
				uint16_t loopLen = READ_BE_UINT16(ptr + 2) * 2;
				if (loopLen != 0)
				{
					pat.loopPos = pat.sampleLen;
					pat.loopData = ptr;
					pat.loopLen = loopLen;
				}
				else
				{
					pat.loopPos = 0;
					pat.loopData = 0;
					pat.loopLen = 0;
				}
				int16_t m = pat.sampleVolume;
				uint8_t effect = (pat.note_2 & 0x0F00) >> 8;
				uint8_t volume = (pat.note_2 & 0xFF);
				if (effect == 5) // volume up
				{
					m += volume;
					if (m > 0x3F) m = 0x3F;
				}
				else if (effect == 6) // volume down
				{
					m -= volume;
					if (m < 0) m = 0;
				}
				m_mixer->setChannelVolume(channel, m);
				pat.sampleVolume = m;
			}
		}
	}
	if (pat.note_1 == 0xFFFD)
	{
		m_mixer->music_mark(pat.note_2);
	}
	else if (pat.note_1 != 0)
	{
		if (pat.note_1 == 0xFFFE)
		{
			m_mixer->stopChannel(channel);
		}
		else if (pat.sampleBuffer != 0)
		{
			struct anotherw_vm_sound_device::MixerChunk mc;
			memset(&mc, 0, sizeof(mc));
			mc.data = pat.sampleBuffer + pat.sampleStart;
			mc.len = pat.sampleLen;
			mc.loopPos = pat.loopPos;
			mc.loopLen = pat.loopLen;
			assert(pat.note_1 >= 0x37 && pat.note_1 < 0x1000);
			// convert amiga period value to hz
			uint16_t freq = 7159092 / (pat.note_1 * 2);
			m_mixer->playChannel(channel, &mc, freq, pat.sampleVolume);
		}
	}
}
