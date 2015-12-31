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

static uint8_t getPagePtrIndex(uint8_t pageId){
    uint8_t i;
    switch(pageId){
        case 0:
        case 1:
        case 2:
        case 3:
            i = pageId;
            break;
        case 0xFE:
            i = 2;
            break;
        case 0xFF:
            i = 3;
            break;
        default:
            i = 0;
    }
    return i;
}

void another_world_state::selectVideoPage(uint8_t pageId){
    m_curPage = getPagePtrIndex(pageId);
}

void another_world_state::fillPage(uint8_t pageId, uint8_t color){
    uint8_t i = getPagePtrIndex(pageId);

    for (int x=0; x<320; x++){
        for (int y=0; y<200; y++){
            m_page_bitmaps[i].pix16(y, x) = color; //TODO: m_palette->pen(color);
        }
    }
}

void another_world_state::copyVideoPage(uint8_t srcPageId, uint8_t dstPageId, uint16_t vscroll){
//TODO: add support for vertical scrolling
    uint8_t src = getPagePtrIndex(srcPageId);
    uint8_t dest = getPagePtrIndex(dstPageId);
    
    for (int x=0; x<320; x++){
        for (int y=0; y<200; y++){
            uint16_t color = m_page_bitmaps[src].pix16(y, x);
            m_page_bitmaps[dest].pix16(y, x) = color;
        }
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