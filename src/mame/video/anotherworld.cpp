// license:GPL2+
// copyright-holders:FelipeSanches
#include "emu.h"
#include "includes/anotherworld.h"

void another_world_state::video_start()
{
    m_char_tilemap = &machine().tilemap().create(m_gfxdecode, tilemap_get_info_delegate(FUNC(another_world_state::get_char_tile_info),this), TILEMAP_SCAN_ROWS,  8, 8, 40, 25);
    m_char_tilemap->configure_groups(*m_gfxdecode->gfx(0), 0x4f);

    m_curPage = 0;
    for (int i=0; i<4; i++){
        m_screen->register_screen_bitmap(m_page_bitmaps[i]);
    }

    for (int c = 0; c < 40*25; c++){
        m_videoram[c] = 0x00;
    }
}

void another_world_state::draw_charactere(uint8_t character, uint16_t x, uint16_t y, uint8_t color){
    const uint8_t *font = memregion("chargen")->base();

    for (int j = 0; j < 8; j++) {
        uint8_t row = font[(character - ' ') * 8 + j];
        for (int i = 0; i < 8; i++) {
            if (row & 0x80) {
                m_page_bitmaps[m_curPage].pix16(y+j, x+i) = color; //TODO: m_palette->pen(color);
            }
            row <<= 1;
        }
    }
}

UINT32 another_world_state::screen_update_aw(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
//    bitmap.fill(m_palette->black_pen(), cliprect);
    
    //m_char_tilemap->draw(screen, bitmap, cliprect, 0, 0);
    copybitmap(bitmap, m_page_bitmaps[m_curPage], 0, 0, 0, 0, cliprect);
    
    return 0;
}