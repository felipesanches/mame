#include "cpu/anotherworld/anotherworld.h"

class another_world_state : public driver_device
{
public:
    another_world_state(const machine_config &mconfig, device_type type, const char *tag)
        : driver_device(mconfig, type, tag),
        m_videoram(*this, "videoram"),
        m_gfxdecode(*this, "gfxdecode"),
        m_maincpu(*this, "maincpu"),
        m_screen(*this, "screen")
    { }

    DECLARE_DRIVER_INIT(another_world);
    virtual void machine_start() override;
    TILE_GET_INFO_MEMBER(get_char_tile_info);
    virtual void video_start() override;
    DECLARE_PALETTE_INIT(anotherw);
    uint8_t m_curPage;
    bitmap_ind16 m_page_bitmaps[4];
    tilemap_t    *m_char_tilemap;
    required_shared_ptr<UINT8> m_videoram;
    required_device<gfxdecode_device> m_gfxdecode;
    required_device<another_world_cpu_device> m_maincpu;
    required_device<screen_device> m_screen;
    UINT32 screen_update_aw(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);
    void draw_charactere(uint8_t character, uint16_t x, uint16_t y, uint8_t color);
};