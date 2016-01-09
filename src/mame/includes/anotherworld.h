#include "cpu/anotherworld/anotherworld.h"

/******************************
 * Audio-related declarations *
 ******************************/

class anotherw_sound_device : public device_t,
                              public device_sound_interface
{
public:
    // construction/destruction
    anotherw_sound_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock);

    struct MixerChunk {
        const uint8_t *data;
        uint16_t len;
        uint16_t loopPos;
        uint16_t loopLen;
    };

    // runtime configuration
    void playChannel(uint8_t channel, const MixerChunk *mc, uint16_t freq, uint8_t volume);
    void stopChannel(uint8_t channel);
    void setChannelVolume(uint8_t channel, uint8_t volume);
    void stopAll();

    static const uint16_t frequenceTable[];

protected:
    // device-level overrides
    virtual void device_start() override;
    virtual void device_reset() override;
    virtual void device_post_load() override;
    virtual void device_clock_changed() override;

    // device_sound_interface overrides
    virtual void sound_stream_update(sound_stream &stream, stream_sample_t **inputs, stream_sample_t **outputs, int samples) override;

    // a single channel
    class anotherw_channel
    {
    public:
        anotherw_channel();
        void mix(stream_sample_t *buffer, int samples);

        uint32_t m_sample;       // current sample number
        uint32_t m_count;        // total samples to play
        uint8_t m_active;
        uint8_t m_volume;
        MixerChunk m_chunk;
        uint32_t m_chunkPos;
        uint32_t m_chunkInc;
    };

    // internal state
    static const int ANOTHERW_CHANNELS = 4;

    anotherw_channel    m_channels[ANOTHERW_CHANNELS];
    sound_stream *      m_stream;
};

// device type definition
extern const device_type ANOTHERW_SOUND;

/******************************
 * Video-related declarations *
 ******************************/

struct Point {
    int16_t x, y;

    Point() : x(0), y(0) {}
    Point(int16_t xx, int16_t yy) : x(xx), y(yy) {}
    Point(const Point &p) : x(p.x), y(p.y) {}
};

#define COLOR_BLACK 0xFF
#define DEFAULT_ZOOM 0x40

enum {
    CINEMATIC=0,
    VIDEO_2=1
};

struct Polygon {
    enum {
        MAX_POINTS = 50
    };

    uint16_t bbox_w, bbox_h;
    uint8_t numPoints;
    Point points[MAX_POINTS];

    void readVertices(const uint8_t *p, uint16_t zoom);
};

/*******************************
 * Driver-related declarations *
 *******************************/

class another_world_state : public driver_device
{
public:
    another_world_state(const machine_config &mconfig, device_type type, const char *tag)
        : driver_device(mconfig, type, tag),
        m_videoram(*this, "videoram"),
        m_gfxdecode(*this, "gfxdecode"),
        m_maincpu(*this, "maincpu"),
        m_screen(*this, "screen"),
        m_palette(*this, "palette"),
        m_mixer(*this, "samples")
    { }

    virtual void machine_start() override;
    virtual void video_start() override;
    DECLARE_MACHINE_START(anotherw);
    DECLARE_DRIVER_INIT(another_world);
    DECLARE_PALETTE_INIT(anotherw);

    DECLARE_READ16_MEMBER(left_right_r);
    DECLARE_READ16_MEMBER(up_down_r);
    DECLARE_READ16_MEMBER(action_r);
    DECLARE_READ16_MEMBER(pos_mask_r);
    DECLARE_READ16_MEMBER(action_pos_mask_r);

    TILE_GET_INFO_MEMBER(get_char_tile_info);

    bitmap_ind16* m_curPagePtr1;
    bitmap_ind16* m_curPagePtr2;
    bitmap_ind16* m_curPagePtr3;

    bitmap_ind16 m_screen_bitmap;
    bitmap_ind16 m_page_bitmaps[4];
    
    tilemap_t *m_char_tilemap;
    uint8_t* m_polygonData;
    uint16_t m_data_offset;
    Polygon m_polygon;
    int16_t m_hliney;

    //Division precomputing lookup table
    uint16_t m_interpTable[0x400];

    required_shared_ptr<UINT8> m_videoram;
    required_device<gfxdecode_device> m_gfxdecode;
    required_device<another_world_cpu_device> m_maincpu;
    required_device<screen_device> m_screen;
    required_device<palette_device> m_palette;
    required_device<anotherw_sound_device> m_mixer;

    void setupPart(uint16_t resourceId);

    UINT32 screen_update_aw(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);
    void draw_charactere(uint8_t character, uint16_t x, uint16_t y, uint8_t color);
    void selectVideoPage(uint8_t pageId);
    void fillPage(uint8_t pageId, uint8_t color);
    void copyVideoPage(uint8_t srcPageId, uint8_t dstPageId, uint16_t vscroll);
    void changePalette(uint8_t paletteId);
    void setDataBuffer(uint8_t type, uint16_t offset);
    void readAndDrawPolygon(uint8_t color, uint16_t zoom, const Point &pt);
    void fillPolygon(uint16_t color, uint16_t zoom, const Point &pt);
    void drawPoint(uint8_t color, int16_t x, int16_t y);
    void readAndDrawPolygonHierarchy(uint16_t zoom, const Point &pgc);
    int32_t calcStep(const Point &p1, const Point &p2, uint16_t &dy);
    void drawLineBlend(int16_t x1, int16_t x2, uint8_t color);
    void drawLineN(int16_t x1, int16_t x2, uint8_t color);
    void drawLineP(int16_t x1, int16_t x2, uint8_t color);
    void updateDisplay(uint8_t pageId);
    bitmap_ind16* getPagePtr(uint8_t pageId);

    void playSound(uint16_t resNum, uint8_t freq, uint8_t vol, uint8_t channel);
};

typedef void (another_world_state::*drawLine)(int16_t x1, int16_t x2, uint8_t col);