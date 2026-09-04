// license:GPL-2.0+
// copyright-holders:Felipe Sanches

#ifndef MAME_ULTIMACHINE_MM2_VIEWPORT_H
#define MAME_ULTIMACHINE_MM2_VIEWPORT_H

#pragma once

#include "screen.h"

#include <vector>


class mm2_viewport_device : public device_t
{
public:
	mm2_viewport_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	template <typename T> void set_mesh_region(T &&tag) { m_mesh_region.set_tag(std::forward<T>(tag)); }

	void set_position(double x_mm, double y_mm, double z_mm);
	void set_extrusion(double e_mm);
	void set_temperature(int which, double celsius);
	void set_fan(double duty);            // 0.0 to 1.0
	void set_motors(bool enabled);
	void set_endstops(bool z_min, bool z_max, bool y_min, bool y_max);

	void orbit(double d_yaw, double d_pitch);
	void zoom(double factor);
	void reset_view();
	void set_show_3d(bool on);

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

	struct mesh
	{
		vec3 origin;
		vec3 scale;
		uint32_t first;
		uint32_t count;
	};

	struct instance
	{
		uint32_t mesh;
		float xform[12];
	};

	struct body
	{
		float post[12];
		float mid[12];
		uint8_t op[2];
		float axis[2][3];
		float coef[2][3][4];
		vec3 centre;
		uint32_t first;
		uint32_t count;
		uint8_t uses;
	};

	static constexpr uint8_t USES_X = 1, USES_Y = 2, USES_Z = 4;

	static constexpr uint8_t OP_NONE = 0, OP_TRANSLATE = 1, OP_ROTATE = 2;

	struct bead
	{
		vec3 from;
		vec3 to;
	};

	uint32_t screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect);
	uint32_t draw_part(bitmap_rgb32 &bitmap, const rectangle &cliprect);

	bool load_mesh();
	void body_matrix(const body &b, float out[12]) const;
	vec3 on_the_bed(const vec3 &machine) const;
	void draw_body(bitmap_rgb32 &bitmap, const rectangle &cliprect, const body &b);

	bool project(const vec3 &world, float &sx, float &sy, float &sz) const;
	void raster_face(bitmap_rgb32 &bitmap, const rectangle &cliprect, const vec3 v[3], rgb_t colour);
	void raster_bead(bitmap_rgb32 &bitmap, const rectangle &cliprect, const bead &b, rgb_t colour);
	void set_pixel(bitmap_rgb32 &bitmap, const rectangle &cliprect, int x, int y, float z, rgb_t colour);

	required_device<screen_device> m_screen;
	required_memory_region m_mesh_region;

	std::vector<body> m_bodies;
	std::vector<mesh> m_meshes;
	std::vector<instance> m_instances;
	const uint8_t *m_tris = nullptr;    // 22 bytes each: rgb, pad, 9 x u16
	uint32_t m_tri_count = 0;

	double m_x = 0.0, m_y = 0.0, m_z = 0.0, m_e = 0.0;
	double m_temp[2] = { 25.0, 25.0 };
	double m_fan = 0.0;
	bool m_motors = false;
	bool m_endstop_z_min = false, m_endstop_z_max = false;
	bool m_endstop_y_min = false, m_endstop_y_max = false;

	static constexpr size_t DEPOSIT_LIMIT = 2000000;

	std::vector<bead> m_deposit;
	bool m_deposit_full = false;
	vec3 m_deposit_lo, m_deposit_hi;
	vec3 m_last_nozzle;
	bool m_have_last_nozzle = false;
	bool m_show_deposit = true;

	double m_yaw = 0.0, m_pitch = 0.0, m_distance = 0.0;

	std::vector<float> m_depth;
	int m_width = 0, m_height = 0;

	bitmap_rgb32 m_cache;
	std::vector<float> m_cache_depth;
	bool m_cache_valid = false;
	double m_cache_yaw = 0.0, m_cache_pitch = 0.0, m_cache_distance = 0.0;
	int m_cache_width = 0, m_cache_height = 0;
	double m_cache_z = 0.0;

	bool m_have_frame = false;
	double m_frame_x = 0.0, m_frame_y = 0.0, m_frame_z = 0.0;
	size_t m_frame_beads = 0;
	bool m_frame_deposit = false;

	bool m_show_3d = true;
	bool m_part_only = false;
	double m_part_zoom = 1.0;
	float m_view[3][4] = { };
};

DECLARE_DEVICE_TYPE(MM2_VIEWPORT, mm2_viewport_device)

#endif // MAME_ULTIMACHINE_MM2_VIEWPORT_H
