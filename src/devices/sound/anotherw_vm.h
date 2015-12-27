#ifndef __ANOTHERWORLD_VM_SOUND__
#define __ANOTHERWORLD_VM_SOUND__

#define READ_BE_UINT16(p) (*(p) << 8 | *(p + 1))

//**************************************************************************
//  INTERFACE CONFIGURATION MACROS
//**************************************************************************

#define MCFG_ANOTHERW_VM_MUSIC_MARK_CALLBACK(_devcb) \
	devcb = &downcast<anotherw_vm_sound_device &>(*device).set_music_mark_callback(DEVCB_##_devcb);

class anotherw_vm_sound_device : public device_t,
                                 public device_sound_interface
{
public:
	// construction/destruction
	anotherw_vm_sound_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	auto music_mark_cb() { return m_write_music_mark.bind(); }

	struct MixerChunk
	{
		const uint8_t *data;
		uint16_t len;
		uint16_t loopPos;
		uint16_t loopLen;
	};

	// runtime configuration
	void playSound(uint8_t channel, uint16_t resNum, uint8_t freq, uint8_t vol);
	void playChannel(uint8_t channel, const MixerChunk *mc, uint16_t freq, uint8_t volume);
	void stopChannel(uint8_t channel);
	void setChannelVolume(uint8_t channel, uint8_t volume);
	void stopAll();
	void music_mark(uint8_t data);

	const uint8_t * m_samples_base_ptr;
	static const uint16_t frequenceTable[];
	struct VMSfxPlayer*  m_player;
	emu_timer* m_timer;
	TIMER_CALLBACK_MEMBER(musicPlayerCallback);

protected:
	// device-level overrides
	virtual void device_start() override;
	virtual void device_reset() override;
	virtual void device_post_load() override;
	virtual void device_clock_changed() override;

	// device_sound_interface overrides
	virtual void sound_stream_update(sound_stream &stream, std::vector<read_stream_view> const &inputs, std::vector<write_stream_view> &outputs) override;

	// a single channel
	class anotherw_channel
	{
	public:
		anotherw_channel();

		uint8_t     m_active;
		uint8_t     m_volume;
		MixerChunk  m_chunk;
		uint32_t    m_chunkPos;
		uint32_t    m_chunkInc;
	};

	// internal state
	static const int ANOTHERW_CHANNELS = 4;

	anotherw_channel   m_channels[ANOTHERW_CHANNELS];
	sound_stream *     m_stream;

private:
	devcb_write8    m_write_music_mark;
};

// device type definition
DECLARE_DEVICE_TYPE(ANOTHERW_VM_SOUND, anotherw_vm_sound_device)


/****************
 * Music Player *
 ****************/

struct VMSfxInstrument
{
	uint8_t *data;
	uint16_t volume;
};

struct VMSfxModule
{
	const uint8_t *data;
	uint16_t curPos;
	uint8_t curOrder;
	uint8_t numOrder;
	uint8_t orderTable[0x80];
	VMSfxInstrument samples[15];
};

struct VMSfxPattern
{
	uint16_t note_1;
	uint16_t note_2;
	uint16_t sampleStart;
	uint8_t *sampleBuffer;
	uint16_t sampleLen;
	uint16_t loopPos;
	uint8_t *loopData;
	uint16_t loopLen;
	uint16_t sampleVolume;
};

struct VMSfxPlayer
{
	anotherw_vm_sound_device *m_mixer;
	uint16_t m_delay;
	uint16_t m_resNum;
	VMSfxModule m_sfxMod;

	VMSfxPlayer(anotherw_vm_sound_device *mixer);
	void initPlayer();
	void freePlayer();

	void setEventsDelay(uint16_t delay);
	int loadSfxModule(uint16_t resNum, uint16_t delay, uint8_t pos);
	void prepareInstruments(const uint8_t *p);
	void start();
	void stop();
	void handleEvents();
	void handlePattern(uint8_t channel, const uint8_t *patternData);
};

#endif //#ifndef __ANOTHERWORLD_VM_SOUND__
