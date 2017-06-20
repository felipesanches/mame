#ifndef __ANOTHERWORLD_VM_VIDEO__
#define __ANOTHERWORLD_VM_VIDEO__

#include "cpu/anotherworld/anotherworld.h"
#include "sound/anotherw_vm.h"
#include "screen.h"

/******************************
 * Video-related declarations *
 ******************************/

struct VMPoint {
    int16_t x, y;

    VMPoint() : x(0), y(0) {}
    VMPoint(int16_t xx, int16_t yy) : x(xx), y(yy) {}
    VMPoint(const VMPoint &p) : x(p.x), y(p.y) {}
};

#define COLOR_BLACK 0xFF
#define DEFAULT_ZOOM 0x40

#ifndef CINEMATIC
#define CINEMATIC 0
#endif

#ifndef VIDEO_2
#define VIDEO_2 1
#endif

struct VMPolygon {
    enum {
        MAX_POINTS = 50
    };

    uint16_t bbox_w, bbox_h;
    uint8_t numPoints;
    VMPoint points[MAX_POINTS];

    void readVertices(const uint8_t *p, uint16_t zoom);
};

/*******************************
 * Driver-related declarations *
 *******************************/

class another_world_vm_state : public driver_device
{
public:
    another_world_vm_state(const machine_config &mconfig, device_type type, const char *tag)
        : driver_device(mconfig, type, tag),
        m_maincpu(*this, "maincpu"),
        m_screen(*this, "screen"),
        m_palette(*this, "palette"),
        m_mixer(*this, "samples")
    { }

    virtual void machine_start() override;
    virtual void video_start() override;
    DECLARE_MACHINE_START(anotherw);
    DECLARE_DRIVER_INIT(another_world_vm);
    DECLARE_PALETTE_INIT(anotherw);

    DECLARE_WRITE8_MEMBER(mus_mark_w);
    DECLARE_READ8_MEMBER(input_r);
    DECLARE_READ8_MEMBER(keyboard_r);

    bitmap_ind16* m_curPagePtr1;
    bitmap_ind16* m_curPagePtr2;
    bitmap_ind16* m_curPagePtr3;

    bitmap_ind16 m_screen_bitmap;
    bitmap_ind16 m_page_bitmaps[4];
    
    uint8_t* m_polygonData;
    uint16_t m_data_offset;
    VMPolygon m_polygon;
    int16_t m_hliney;

    required_device<another_world_cpu_device> m_maincpu;
    required_device<screen_device> m_screen;
    required_device<palette_device> m_palette;
    required_device<anotherw_vm_sound_device> m_mixer;

    void setupPart(uint16_t resourceId);

    uint32_t screen_update_aw(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);
    void draw_string(uint16_t stringId, uint16_t x, uint16_t y, uint16_t color);
    void draw_charactere(uint8_t character, uint16_t x, uint16_t y, uint8_t color);
    void selectVideoPage(uint8_t pageId);
    void fillPage(uint8_t pageId, uint8_t color);
    void copyVideoPage(uint8_t srcPageId, uint8_t dstPageId, uint16_t vscroll);
    void changePalette(uint8_t paletteId);
    void setDataBuffer(uint8_t type, uint16_t offset);
    void loadScreen(uint8_t screen_id);
    void readAndDrawPolygon(uint8_t color, uint16_t zoom, const VMPoint &pt);
    void fillPolygon(uint16_t color, const VMPoint &pt);
    void drawPoint(uint8_t color, int16_t x, int16_t y);
    void readAndDrawPolygonHierarchy(uint16_t zoom, const VMPoint &pgc);
    int32_t calcStep(const VMPoint &p1, const VMPoint &p2, uint16_t &dy);
    void drawLineBlend(int16_t x1, int16_t x2, uint8_t color);
    void drawLineN(int16_t x1, int16_t x2, uint8_t color);
    void drawLineP(int16_t x1, int16_t x2, uint8_t color);
    void updateDisplay(uint8_t pageId);
    bitmap_ind16* getPagePtr(uint8_t pageId);

    void playSound(uint16_t resNum, uint8_t freq, uint8_t vol, uint8_t channel);
};

typedef void (another_world_vm_state::*drawLine)(int16_t x1, int16_t x2, uint8_t col);


#endif //#ifndef __ANOTHERWORLD_VM_VIDEO__
