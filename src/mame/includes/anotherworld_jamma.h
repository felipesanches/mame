#ifndef __ANOTHERWORLD_JAMMA_H__
#define __ANOTHERWORLD_JAMMA_H__

#include "machine/gen_latch.h"
#include "machine/8364_paula.h"
#include "screen.h"
#include "emupal.h"

class awjamma_state : public driver_device
{
public:
	awjamma_state(const machine_config &mconfig, device_type type, const char *tag)
	: driver_device(mconfig, type, tag)
	, m_maincpu(*this, "maincpu")
	, m_soundcpu(*this, "soundcpu")
	, m_videocpu(*this, "videocpu")
	, m_screen(*this, "screen")
	, m_paula(*this, "paula")
	, m_palette(*this, "palette")
	{ }

	void awjamma(machine_config &config);
	
private:
	virtual void machine_start() override;
	virtual void machine_reset() override;
	virtual void video_start() override;

	void maincpu_prog_map(address_map &map);
	void soundcpu_prog_map(address_map &map);
	void videocpu_prog_map(address_map &map);

	uint8_t fetch_bytecode_byte(offs_t offset);
	uint8_t fetch_polygon_data(offs_t offset);
	uint8_t chargen_r(offs_t offset);
	uint8_t strings_r(offs_t offset);

	void set_instruction_pointer(offs_t offset, uint8_t data);
	void set_polygon_data_offset(offs_t offset, uint8_t data);
	void switch_level_bank(uint8_t data);
	void changePalette(uint8_t data);
	void switch_work_videopage_bank(uint8_t data);
	void set_screen_selector(uint8_t data);
	void select_active_videopage_y(uint8_t data);
	void select_work_videopage_y(uint8_t data);
	void select_screens_y(uint8_t data);
	void select_polygon_data_source(uint8_t data);
	void set_palette(uint8_t paletteId);
	void init_palette(palette_device &palette);

	uint32_t screen_update_aw(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);

	std::unique_ptr<uint8_t[]> m_active_vram;
	std::unique_ptr<uint8_t[]> m_work_vram;

	required_device<cpu_device> m_maincpu;
	required_device<cpu_device> m_soundcpu;
	required_device<cpu_device> m_videocpu;
	required_device<screen_device> m_screen;
	required_device<paula_8364_device> m_paula;
	required_device<palette_device> m_palette;

	uint16_t instruction_pointer;
	uint16_t polygon_data_offset;
	uint8_t level_bank;
	uint8_t work_videopage_bank;
	uint8_t screens_bank;
	uint8_t active_videopage_y;
	uint8_t work_videopage_y;
	uint8_t screens_y;
	uint8_t* bytecode_base;
	uint8_t* chargen_base;
	uint8_t* strings_base;
	uint8_t* polygon_cinematic_base;
	uint8_t* polygon_video2_base;
	bool use_video_2;
};

#endif //#ifndef __ANOTHERWORLD_JAMMA_H__
