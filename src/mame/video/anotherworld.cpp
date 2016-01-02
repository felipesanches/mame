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
    
    m_interpTable[0] = 0x4000;

    for (int i = 1; i < 0x400; ++i) {
        m_interpTable[i] = 0x4000 / i;
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

void another_world_state::setDataBuffer(uint8_t type, uint16_t offset){
    switch (type){
        case CINEMATIC:
            m_polygonData = memregion("video1")->base();
            break;
        case VIDEO_2:
            m_polygonData = memregion("video2")->base();
            break;
    }

    m_data_offset = offset;
}

void Polygon::readVertices(const uint8_t *p, uint16_t zoom) {
    bbox_w = (*p++) * zoom / 64;
    bbox_h = (*p++) * zoom / 64;
    numPoints = *p++;
    assert((numPoints & 1) == 0 && numPoints < MAX_POINTS);

    //Read all points, directly from bytecode segment
    for (int i = 0; i < numPoints; ++i) {
        Point *pt = &points[i];
        pt->x = (*p++) * zoom / 64;
        pt->y = (*p++) * zoom / 64;
    }
}

/* This is a recursive method.
 * A shape can be given in two different ways:
 *
 * -> A list of screenspace vertices.
 * -> A list of objectspace vertices, based on a delta from the first vertex.
 */
void another_world_state::readAndDrawPolygon(uint8_t color, uint16_t zoom, const Point &pt) {
    if (!m_polygonData){
        printf("ERROR: m_polygonData is NULL!\n");
        return;
    }

    uint8_t value = m_polygonData[m_data_offset++];

    if (value >= 0xC0) {
        if (color & 0x80) {
            color = value & 0x3F; //why?
        }

        m_polygon.readVertices(&m_polygonData[m_data_offset], zoom);
        fillPolygon(color, zoom, pt);
    } else {
        value &= 0x3F;
        switch (value){
            case 2:
                readAndDrawPolygonHierarchy(zoom, pt);
                break;
            default:
                printf("ERROR: readAndDrawPolygon() (value != 2)\n");
        }
    }
}

/*
    What is read from the bytecode is not
    a pure screnspace polygon but a
    polygonspace polygon.
*/
void another_world_state::readAndDrawPolygonHierarchy(uint16_t zoom, const Point &pgc) {

    Point pt(pgc);
    pt.x -= m_polygonData[m_data_offset++] * zoom / 64;
    pt.y -= m_polygonData[m_data_offset++] * zoom / 64;

    int16_t children = m_polygonData[m_data_offset++];

    for ( ; children >= 0; --children) {
        uint16_t offset = m_polygonData[m_data_offset++];
        offset = offset << 8 | m_polygonData[m_data_offset++];
        
        Point po(pt);
        po.x += m_polygonData[m_data_offset++] * zoom / 64;
        po.y += m_polygonData[m_data_offset++] * zoom / 64;

        uint16_t color = 0xFF;
        if (offset & 0x8000) {
            color = m_polygonData[m_data_offset /* +1 here? */] & 0x7F;
            m_data_offset+=2;
        }

        uint16_t backup = m_data_offset;

        m_data_offset = (offset & 0x7FFF) * 2;
        readAndDrawPolygon(color, zoom, po);

        m_data_offset = backup;
    }
}

int32_t another_world_state::calcStep(const Point &p1, const Point &p2, uint16_t &dy) {
    dy = p2.y - p1.y;
    return (p2.x - p1.x) * m_interpTable[dy] * 4;
}

/* Blend a line in the current framebuffer
*/
void another_world_state::drawLineBlend(int16_t x1, int16_t x2, uint8_t color) {
    int16_t xmax = MAX(x1, x2);
    int16_t xmin = MIN(x1, x2);
    uint8_t i = getPagePtrIndex(m_curPage);

    for (int16_t x=xmin; x<=xmax; x++){
        color = m_page_bitmaps[i].pix16(m_hliney, x);
        m_page_bitmaps[i].pix16(m_hliney, x) = (color & 0x7) | 0x8;
    }
}

void another_world_state::drawLineN(int16_t x1, int16_t x2, uint8_t color) {
    int16_t xmax = MAX(x1, x2);
    int16_t xmin = MIN(x1, x2);
    uint8_t cur = getPagePtrIndex(m_curPage);

    for (int16_t x=xmin; x<=xmax; x++){
        m_page_bitmaps[cur].pix16(m_hliney, x) = color;
    }
}

void another_world_state::drawLineP(int16_t x1, int16_t x2, uint8_t color) {
    int16_t xmax = MAX(x1, x2);
    int16_t xmin = MIN(x1, x2);
    uint8_t cur = getPagePtrIndex(m_curPage);

    for (int16_t x=xmin; x<=xmax; x++){
        color = m_page_bitmaps[0].pix16(m_hliney, x);
        m_page_bitmaps[cur].pix16(m_hliney, x) = color;
    }
}

void another_world_state::fillPolygon(uint16_t color, uint16_t zoom, const Point &pt) {
    
    if (m_polygon.bbox_w == 0 && m_polygon.bbox_h == 1 && m_polygon.numPoints == 4) {
        drawPoint(color, pt.x, pt.y);
        return;
    }

    int16_t x1 = pt.x - m_polygon.bbox_w / 2;
    int16_t x2 = pt.x + m_polygon.bbox_w / 2;
    int16_t y1 = pt.y - m_polygon.bbox_h / 2;
    int16_t y2 = pt.y + m_polygon.bbox_h / 2;

    if (x1 > 319 || x2 < 0 || y1 > 199 || y2 < 0)
        return;

    m_hliney = y1;
    
    uint16_t i, j;
    i = 0;
    j = m_polygon.numPoints - 1;
    
    x2 = m_polygon.points[i].x + x1;
    x1 = m_polygon.points[j].x + x1;

    i++;
    j--;

    drawLine drawFct;
    if (color < 0x10) {
        drawFct = &another_world_state::drawLineN;
    } else if (color > 0x10) {
        drawFct = &another_world_state::drawLineP;
    } else {
        drawFct = &another_world_state::drawLineBlend;
    }

    uint32_t cpt1 = x1 << 16;
    uint32_t cpt2 = x2 << 16;

    while (true) {
        m_polygon.numPoints -= 2;
        if (m_polygon.numPoints == 0) break;
        uint16_t h;
        int32_t step1 = calcStep(m_polygon.points[j + 1], m_polygon.points[j], h);
        int32_t step2 = calcStep(m_polygon.points[i - 1], m_polygon.points[i], h);

        i++;
        j--;

        cpt1 = (cpt1 & 0xFFFF0000) | 0x7FFF;
        cpt2 = (cpt2 & 0xFFFF0000) | 0x8000;

        if (h == 0) {
            cpt1 += step1;
            cpt2 += step2;
        } else {
            for (; h != 0; --h) {
                if (m_hliney >= 0) {
                    x1 = cpt1 >> 16;
                    x2 = cpt2 >> 16;
                    if (x1 <= 319 && x2 >= 0) {
                        if (x1 < 0) x1 = 0;
                        if (x2 > 319) x2 = 319;
                        (this->*drawFct)(x1, x2, color);
                    }
                }
                cpt1 += step1;
                cpt2 += step2;
                m_hliney++;
                if (m_hliney > 199) return;
            }
        }
    }
}

void another_world_state::drawPoint(uint8_t color, int16_t x, int16_t y) {
    uint8_t i = getPagePtrIndex(m_curPage);
    m_page_bitmaps[i].pix16(y, x) = color;
}

#define NUM_COLORS 16
void another_world_state::changePalette(uint8_t paletteId){
    const uint8_t *colors = memregion("palettes")->base();
    uint8_t r, g, b;

    for (int i = 0; i < NUM_COLORS; ++i)
    {
        uint8_t c1 = *(colors + paletteId * 2*NUM_COLORS + 2*i);
        uint8_t c2 = *(colors + paletteId * 2*NUM_COLORS + 2*i + 1);
        r = ((c1 & 0x0F) << 2) | ((c1 & 0x0F) >> 2);
        g = ((c2 & 0xF0) >> 2) | ((c2 & 0xF0) >> 6);
        b = ((c2 & 0x0F) >> 2) | ((c2 & 0x0F) << 2);
        m_palette->set_pen_color(i, pal6bit(r), pal6bit(g), pal6bit(b));
    }
}

void another_world_state::selectVideoPage(uint8_t pageId){
    m_curPage = getPagePtrIndex(pageId);
}

void another_world_state::fillPage(uint8_t pageId, uint8_t color){
    uint8_t i = getPagePtrIndex(pageId);

    for (int x=0; x<320; x++){
        for (int y=0; y<200; y++){
            m_page_bitmaps[i].pix16(y, x) = color;
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
                m_page_bitmaps[m_curPage].pix16(y+j, x+i) = color;
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