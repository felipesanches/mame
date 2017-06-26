// license:GPL2+
// copyright-holders:FelipeSanches
#include "emu.h"
#include "includes/anotherworld_vm.h"
#include "screen.h"

void another_world_vm_state::video_start()
{
    for (int i=0; i<4; i++){
        m_screen->register_screen_bitmap(m_page_bitmaps[i]);
    }
    m_screen->register_screen_bitmap(m_screen_bitmap);

    m_curPagePtr1 = &m_page_bitmaps[2];
    m_curPagePtr2 = &m_page_bitmaps[2];
    m_curPagePtr3 = &m_page_bitmaps[1];

}

bitmap_ind16* another_world_vm_state::getPagePtr(uint8_t pageId){
    switch(pageId){
        case 0:
        case 1:
        case 2:
        case 3:
            return &m_page_bitmaps[pageId];
            break;
        case 0xFF:
            return m_curPagePtr3;
            break;
        case 0xFE:
            return m_curPagePtr2;
            break;
        default:
            return &m_page_bitmaps[0];
    }
}

void another_world_vm_state::setDataBuffer(uint8_t type, uint16_t offset){
    m_use_video2 = (type==0x01);
    switch (type){
        case CINEMATIC:
            m_polygonData = (uint8_t *) membank("video1_bank")->base();
            break;
        case VIDEO_2:
            m_polygonData = memregion("video2")->base();
            break;
    }

    m_data_offset = offset;
}

void another_world_vm_state::loadScreen(uint8_t screen_id) {
    const uint8_t *src = memregion("screens")->base() + screen_id * 0x8000;
    bitmap_ind16* dest = &m_page_bitmaps[0];
    int h = 200;
    while (h--) {
        int x = 0;
        int w = 40;
        while (w--) {
            uint8_t p[] = {
                *(src + 8000 * 3),
                *(src + 8000 * 2),
                *(src + 8000 * 1),
                *(src + 8000 * 0)
            };
            for(int j = 0; j < 4; ++j) {
                uint8_t value = 0;
                for (int i = 0; i < 8; ++i) {
                    value <<= 1;
                    value |= (p[i & 3] & 0x80) ? 1 : 0;
                    p[i & 3] <<= 1;
                }
                dest->pix16(199-h, x++) = (value >> 4) & 0x0F;
                dest->pix16(199-h, x++) = value & 0x0F;
            }
            ++src;
        }
    }
}

void VMPolygon::readVertices(const uint8_t *p, uint16_t zoom, const uint8_t *ref, bool video_2, uint8_t level) {

    if (video_2) {
        printf("VIDEO 2: 0x%04lX-", (unsigned long int) (p - ref));
    } else {
        printf("CINE-%02X: 0x%04lX-", level, (unsigned long int) (p - ref));
    }

    bbox_w = (*p++) * zoom / DEFAULT_ZOOM;
    bbox_h = (*p++) * zoom / DEFAULT_ZOOM;
    
    numPoints = *p++;
    assert((numPoints & 1) == 0 && numPoints < MAX_POINTS);

    //Read all points, directly from bytecode segment
    for (int i = 0; i < numPoints; ++i) {
        VMPoint *pt = &points[i];
        pt->x = (*p++) * zoom / DEFAULT_ZOOM;
        pt->y = (*p++) * zoom / DEFAULT_ZOOM;
    }

    printf("0x%04lX \t: readVertices (numPoints = %d)\n", (unsigned long int) (p - ref - 1), numPoints);
}

/* This is a recursive method.
 * A shape can be given in two different ways:
 *
 * -> A list of screenspace vertices.
 * -> A list of objectspace vertices, based on a delta from the first vertex.
 */
void another_world_vm_state::readAndDrawPolygon(uint8_t color, uint16_t zoom, const VMPoint &pt) {
    if (!m_polygonData){
        printf("ERROR: m_polygonData is NULL!\n");
        return;
    }

    if (m_use_video2) {
        printf("VIDEO 2: 0x%04X \t: readAndDrawPolygon\n", m_data_offset);
    } else {
        printf("CINE-%02X: 0x%04X \t: readAndDrawPolygon\n", level_bank, m_data_offset);
    }
    uint8_t value = m_polygonData[m_data_offset++];

    if (value >= 0xC0) {
        if (color & 0x80) {
            color = value & 0x3F; //why?
        }

        m_polygon.readVertices(&m_polygonData[m_data_offset], zoom, m_polygonData, m_use_video2, level_bank);
        fillPolygon(color, pt);
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
void another_world_vm_state::readAndDrawPolygonHierarchy(uint16_t zoom, const VMPoint &pgc) {

    uint16_t initial = m_data_offset;
    VMPoint pt(pgc);
    pt.x -= m_polygonData[m_data_offset++] * zoom / DEFAULT_ZOOM;
    pt.y -= m_polygonData[m_data_offset++] * zoom / DEFAULT_ZOOM;

    int16_t children = m_polygonData[m_data_offset++];
    int16_t num_children = children;

    for ( ; children >= 0; --children) {
        uint16_t offset = m_polygonData[m_data_offset++];
        offset = offset << 8 | m_polygonData[m_data_offset++];
        
        VMPoint po(pt);
        po.x += m_polygonData[m_data_offset++] * zoom / DEFAULT_ZOOM;
        po.y += m_polygonData[m_data_offset++] * zoom / DEFAULT_ZOOM;

        uint16_t color = 0xFF;
        if (offset & 0x8000) {
            color = m_polygonData[m_data_offset++] & 0x7F;
            m_data_offset++; //and waste a byte...
        }

        uint16_t backup = m_data_offset;

        m_data_offset = (offset & 0x7FFF) * 2;
        readAndDrawPolygon(color, zoom, po);

        m_data_offset = backup;
    }

    if (m_use_video_2) {
        printf("VIDEO 2:");
    } else {
        printf("CINE-%02X:", level);
    }

    printf(" 0x%04X-0x%04X \t: readAndDrawPolygonHierarchy (children = %d)\n", initial, m_data_offset-1, num_children);
}

#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)

/* Blend a line in the current framebuffer
*/
void another_world_vm_state::drawLineBlend(int16_t x1, int16_t x2, uint8_t color) {
    int16_t xmax = MAX(x1, x2);
    int16_t xmin = MIN(x1, x2);

    for (int16_t x=xmin; x<=xmax; x++){
        color = m_curPagePtr1->pix16(m_hliney, x);
        m_curPagePtr1->pix16(m_hliney, x) = (color & 0x7) | 0x8;
    }
}

void another_world_vm_state::drawLineN(int16_t x1, int16_t x2, uint8_t color) {
    int16_t xmax = MAX(x1, x2);
    int16_t xmin = MIN(x1, x2);

    for (int16_t x=xmin; x<=xmax; x++){
        m_curPagePtr1->pix16(m_hliney, x) = color;
    }
}

void another_world_vm_state::drawLineP(int16_t x1, int16_t x2, uint8_t color) {
    int16_t xmax = MAX(x1, x2);
    int16_t xmin = MIN(x1, x2);

    for (int16_t x=xmin; x<=xmax; x++){
        color = m_page_bitmaps[0].pix16(m_hliney, x);
        m_curPagePtr1->pix16(m_hliney, x) = color;
    }
}

int32_t another_world_vm_state::calcStep(const VMPoint &p1, const VMPoint &p2, uint16_t &dy) {
    uint16_t v = 0x4000;
    int dx = p2.x - p1.x;
    dy = p2.y - p1.y;
    if (dy>0)
	v = 0x4000/dy;
    return dx * v * 4;
}

void another_world_vm_state::fillPolygon(uint8_t color, const VMPoint &pt) {
    
    if (m_polygon.bbox_w == 0 && m_polygon.bbox_h == 1 && m_polygon.numPoints == 4) {
        drawPoint(color, pt.x, pt.y);
        return;
    }

    int16_t xmin = pt.x - m_polygon.bbox_w / 2;
    int16_t xmax = pt.x + m_polygon.bbox_w / 2;
    int16_t ymin = pt.y - m_polygon.bbox_h / 2;
    int16_t ymax = pt.y + m_polygon.bbox_h / 2;

    if (xmin >= 320 || xmax < 0 || ymin >= 200 || ymax < 0)
        return;

    xmin *= (m_screen->width()/320);
    xmax *= (m_screen->width()/320);
    ymin *= (m_screen->height()/200);
    ymax *= (m_screen->height()/200);

    m_hliney = ymin;
    
    uint16_t i, j;
    int16_t x1, x2;

    i = 0;
    j = m_polygon.numPoints - 1;
    
    x2 = m_polygon.points[i].x * (m_screen->width()/320) + xmin;
    x1 = m_polygon.points[j].x * (m_screen->width()/320) + xmin;

    i++;
    j--;

    drawLine drawFct;
    if (color < 0x10) {
        drawFct = &another_world_vm_state::drawLineN;
    } else if (color > 0x10) {
        drawFct = &another_world_vm_state::drawLineP;
    } else {
        drawFct = &another_world_vm_state::drawLineBlend;
    }

    uint32_t cpt1 = ((uint32_t) x1) << 16;
    uint32_t cpt2 = ((uint32_t) x2) << 16;

    while (true) {
        m_polygon.numPoints -= 2;
        if (m_polygon.numPoints == 0) break;
        uint16_t h;
        int32_t step1 = calcStep(m_polygon.points[j + 1], m_polygon.points[j], h);
        int32_t step2 = calcStep(m_polygon.points[i - 1], m_polygon.points[i], h);
        h *= (m_screen->height()/200);

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
                    if (x1 <= (m_screen->width()-1) && x2 >= 0) {
                        if (x1 < 0) x1 = 0;
                        if (x2 > (m_screen->width()-1))
                            x2 = m_screen->width()-1;
                        (this->*drawFct)(x1, x2, color);
                    }
                }
                cpt1 += step1;
                cpt2 += step2;
                m_hliney++;
                if (m_hliney > (m_screen->height()-1)) return;
            }
        }
    }
}

void another_world_vm_state::updateDisplay(uint8_t pageId) {
    if (pageId != 0xFE) {
        if (pageId == 0xFF) {
            bitmap_ind16* tmp = m_curPagePtr2;
            m_curPagePtr2 = m_curPagePtr3;
            m_curPagePtr3 = tmp;
        } else {
            m_curPagePtr2 = getPagePtr(pageId);
        }
    }

    for (int x=0; x < m_screen->width(); x++){
        for (int y=0; y < m_screen->height(); y++){
            uint16_t color = m_curPagePtr2->pix16(y, x);
            m_screen_bitmap.pix16(y, x) = color;
        }
    }
}

void another_world_vm_state::drawPoint(uint8_t color, int16_t x, int16_t y) {
    x = (int16_t) (x * (m_screen->width()/320.0));
    y = (int16_t) (y * (m_screen->height()/200.0));
    if (x >= 0 && x <= 319 && y >= 0 && y <= 199) {
        for (int i=0; i<(m_screen->width()/320.0); i++){
            for (int j=0; j<(m_screen->height()/200.0); j++){
                m_curPagePtr1->pix16(y+j, x+i) = color;
            }
        }
    }
}

#define NUM_COLORS 16
void another_world_vm_state::changePalette(uint8_t paletteId){
    const uint8_t *colors = (const uint8_t *) membank("palette_bank")->base();
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

void another_world_vm_state::selectVideoPage(uint8_t pageId){
    m_curPagePtr1 = getPagePtr(pageId);
}

void another_world_vm_state::fillPage(uint8_t pageId, uint8_t color){
    bitmap_ind16* page = getPagePtr(pageId);

    for (int x=0; x < m_screen->width(); x++){
        for (int y=0; y < m_screen->height(); y++){
            page->pix16(y, x) = color;
        }
    }
}

void another_world_vm_state::copyVideoPage(uint8_t srcPageId, uint8_t dstPageId, uint16_t vscroll){
    if (srcPageId == dstPageId)
        return;

    bitmap_ind16* src;
    bitmap_ind16* dest;

    /* The actual meaning of this conditional needs to be clarified: */
    if (srcPageId >= 0xFE || !((srcPageId &= 0xBF) & 0x80)) {
        src = getPagePtr(srcPageId);
        dest = getPagePtr(dstPageId);
        for (int x=0; x < m_screen->width(); x++){
            for (int y=0; y < m_screen->height(); y++){
                dest->pix16(y, x) = src->pix16(y, x);
            }
        }
    } else {
        int h = m_screen->height();
        int src_y0 = 0;
        int dest_y0 = 0;
        src = getPagePtr(srcPageId & 3);
        dest = getPagePtr(dstPageId);
        if (vscroll >= -199 && vscroll <= 199) {
            if (vscroll < 0) {
                h += vscroll;
                src_y0 = -vscroll;
            } else {
                h -= vscroll;
                dest_y0 = vscroll;
            }
            for (int x=0; x < m_screen->width(); x++){
                for (int y=0; y < h; y++){
                    dest->pix16(dest_y0 + y, x) = src->pix16(src_y0 + y, x);
                }
            }
        }
    }
}

void another_world_vm_state::draw_string(uint16_t stringId, uint16_t x, uint16_t y, uint16_t color){
    x = 8 * (x-1);
    uint16_t x0 = x;

    uint8_t* index_ptr = memregion("strings")->base() + 0x1000 + 2*stringId;
    uint16_t str_index = index_ptr[1] << 8 | index_ptr[0];
    uint8_t* c = memregion("strings")->base() + str_index;

    for (; *c != '\0'; c++){
        if (*c == '\n'){
            y+=8;
            x=x0;
        } else {
            draw_charactere((uint8_t) *c, x+=8, y, (uint8_t) color);
        }
    }
}

void another_world_vm_state::draw_charactere(uint8_t character, uint16_t x, uint16_t y, uint8_t color){
    const uint8_t *font = memregion("chargen")->base();

    for (int j = 0; j < 8; j++) {
        uint8_t row = font[(character - ' ') * 8 + j];
        for (int i = 0; i < 8; i++) {
            if (row & 0x80) {
                drawPoint(color, x+i, y+j);
            }
            row <<= 1;
        }
    }
}

uint32_t another_world_vm_state::screen_update_aw(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
    copybitmap(bitmap, m_screen_bitmap, 0, 0, 0, 0, cliprect);    
    return 0;
}
