#ifndef __ANOTHERW_H__
#define __ANOTHERW_H__

#include "machine/gen_latch.h"
#include "machine/8364_paula.h"
#include "screen.h"

class another_world_state : public driver_device
{
public:
    another_world_state(const machine_config &mconfig, device_type type, const char *tag)
        : driver_device(mconfig, type, tag),
        m_maincpu(*this, "maincpu"),
        m_soundcpu(*this, "soundcpu"),
        m_videocpu(*this, "videocpu"),
        m_screen(*this, "screen"),
        m_palette(*this, "palette")
    { }

    virtual void machine_start() override;
    virtual void video_start() override;
    DECLARE_MACHINE_START(anotherw);
    DECLARE_DRIVER_INIT(another_world);
    DECLARE_PALETTE_INIT(anotherw);

    DECLARE_READ8_MEMBER(fetchbyte);
    DECLARE_WRITE8_MEMBER(set_instruction_pointer);
    DECLARE_WRITE8_MEMBER(switch_level_bank);
    DECLARE_WRITE8_MEMBER(changePalette);

//    bitmap_ind16* m_curPagePtr1;
//    bitmap_ind16* m_curPagePtr2;
//    bitmap_ind16* m_curPagePtr3;
    bitmap_ind16 m_screen_bitmap;
//    bitmap_ind16 m_page_bitmaps[4];
    
    required_device<cpu_device> m_maincpu;
    required_device<cpu_device> m_soundcpu;
    required_device<cpu_device> m_videocpu;
    required_device<screen_device> m_screen;
    required_device<palette_device> m_palette;

    uint32_t screen_update_aw(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);
    void set_palette(uint8_t paletteId);

private:
    uint16_t instruction_pointer;
    uint8_t level_bank;
};

#endif //#ifndef __ANOTHERW_H__
