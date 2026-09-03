// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    A 3D view of the Metamaquina 2 printer, rendered into a screen bitmap.

    The geometry comes from a packed asset in a memory region: triangles
    grouped into rigid bodies, each carrying the constant part of its
    transform and an affine description of how the machine's axes move it.

    The machine is published as OpenSCAD sources:
    https://github.com/LibreSolid/Metamaquina2

***************************************************************************/

#ifndef MAME_ULTIMACHINE_MM2_VIEWPORT_H
#define MAME_ULTIMACHINE_MM2_VIEWPORT_H

#pragma once

#include "screen.h"

#include <vector>


class mm2_viewport_device : public device_t
{
public:
	mm2_viewport_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	// the region holding the packed geometry
	template <typename T> void set_mesh_region(T &&tag) { m_mesh_region.set_tag(std::forward<T>(tag)); }

	// machine state
	void set_position(double x_mm, double y_mm, double z_mm);
	void set_extrusion(double e_mm);
	void set_temperature(int which, double celsius);
	void set_fan(double duty);            // 0.0 to 1.0
	void set_motors(bool enabled);
	void set_endstops(bool z_min, bool z_max, bool y_min, bool y_max);

	// the view itself
	void orbit(double d_yaw, double d_pitch);
	void zoom(double factor);
	void reset_view();
	// the asset carries both the modelled M8 thread and a simplified helical prism
	void set_full_threads(bool full);
	void set_show_3d(bool on);

	// draw only the deposited material, framed on it and held still
	void set_part_only(bool on) { m_part_only = on; }

	size_t deposit_beads() const { return m_deposit.size(); }

	void toggle_deposit();
	void clear_deposit();

protected:
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	struct vec3
	{
		float x = 0.0f, y = 0.0f, z = 0.0f;

		vec3 operator+(const vec3 &o) const { return { x + o.x, y + o.y, z + o.z }; }
		vec3 operator-(const vec3 &o) const { return { x - o.x, y - o.y, z - o.z }; }
		vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
	};

	// One rigid body out of the packed asset.  Its world transform is
	//     post . D1 . mid . D2 . dequantise
	// where post, mid and the dequantisation are constant and D1 and D2 are
	// built from the machine's axis positions.
	struct body
	{
		float post[12];
		float mid[12];
		uint8_t op[2];
		float axis[2][3];
		float coef[2][3][4];
		vec3 origin;        // dequantisation offset
		vec3 scale;
		vec3 centre;        // in world space
		uint32_t first;
		uint32_t count;
		uint8_t detail;     // 0 always, 1 only with simplified threads, 2 only with full
		uint8_t uses;       // which machine axes reach it: bit 0 X, 1 Y, 2 Z
	};

	static constexpr uint8_t DETAIL_ALWAYS = 0, DETAIL_SIMPLE = 1, DETAIL_FULL = 2;

	static constexpr uint8_t USES_X = 1, USES_Y = 2, USES_Z = 4;

	static constexpr uint8_t OP_NONE = 0, OP_TRANSLATE = 1, OP_ROTATE = 2;

	// one bead of extruded plastic
	struct bead
	{
		vec3 from;
		vec3 to;
	};

	uint32_t screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect);
	uint32_t draw_part(bitmap_rgb32 &bitmap, const rectangle &cliprect);

	// geometry
	bool load_mesh();
	void body_matrix(const body &b, float out[12]) const;
	vec3 on_the_bed(const vec3 &machine) const;
	void draw_body(bitmap_rgb32 &bitmap, const rectangle &cliprect, const body &b);

	// rasterisation
	bool project(const vec3 &world, float &sx, float &sy, float &sz) const;
	void raster_face(bitmap_rgb32 &bitmap, const rectangle &cliprect, const vec3 v[3], rgb_t colour);
	void raster_bead(bitmap_rgb32 &bitmap, const rectangle &cliprect, const bead &b, rgb_t colour);
	void set_pixel(bitmap_rgb32 &bitmap, const rectangle &cliprect, int x, int y, float z, rgb_t colour);

	required_device<screen_device> m_screen;
	required_memory_region m_mesh_region;

	std::vector<body> m_bodies;
	const uint8_t *m_tris = nullptr;    // 22 bytes each: rgb, pad, 9 x u16
	uint32_t m_tri_count = 0;

	// machine state
	double m_x = 0.0, m_y = 0.0, m_z = 0.0, m_e = 0.0;
	double m_temp[2] = { 25.0, 25.0 };
	double m_fan = 0.0;
	bool m_motors = false;
	bool m_endstop_z_min = false, m_endstop_z_max = false;
	bool m_endstop_y_min = false, m_endstop_y_max = false;

	// deposited material
	// a bead is a sample of the nozzle, not a G-code segment; collinear samples
	// are merged in set_extrusion(), so this is a backstop, not a working limit
	static constexpr size_t DEPOSIT_LIMIT = 2000000;

	std::vector<bead> m_deposit;
	bool m_deposit_full = false;
	vec3 m_deposit_lo, m_deposit_hi;   // maintained as beads arrive, not swept
	vec3 m_last_nozzle;
	bool m_have_last_nozzle = false;
	bool m_show_deposit = true;
	bool m_full_threads = false;

	// camera
	double m_yaw = 0.0, m_pitch = 0.0, m_distance = 0.0;

	// per-frame scratch
	std::vector<float> m_depth;
	int m_width = 0, m_height = 0;

	// fixed bodies and those reached only by Z, rasterised once into a colour
	// and depth cache; invalidated by a camera change or a Z move
	bitmap_rgb32 m_cache;
	std::vector<float> m_cache_depth;
	bool m_cache_valid = false;
	double m_cache_yaw = 0.0, m_cache_pitch = 0.0, m_cache_distance = 0.0;
	int m_cache_width = 0, m_cache_height = 0;
	bool m_cache_full_threads = false;
	double m_cache_z = 0.0;

	// what the last presented frame was of, so an unchanged one can be skipped
	bool m_have_frame = false;
	double m_frame_x = 0.0, m_frame_y = 0.0, m_frame_z = 0.0;
	size_t m_frame_beads = 0;
	bool m_frame_deposit = false;

	bool m_show_3d = true;
	bool m_part_only = false;
	double m_part_zoom = 1.0;   // the part view fits itself; this scales that
	float m_view[3][4] = { };   // world to eye, a rotation and a translation
};

DECLARE_DEVICE_TYPE(MM2_VIEWPORT, mm2_viewport_device)

#endif // MAME_ULTIMACHINE_MM2_VIEWPORT_H
