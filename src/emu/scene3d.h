// license:BSD-3-Clause
// copyright-holders:Felipe Sanches
/***************************************************************************

    scene3d.h

    Scene renderer behind SCREEN_TYPE_3D: a scene graph read from an XML
    region, meshes read from a packed region, animated by output values.

***************************************************************************/

#ifndef MAME_EMU_SCENE3D_H
#define MAME_EMU_SCENE3D_H

#pragma once

#include "bitmap.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>


namespace util::xml { class data_node; }

struct scene3d_gl;

class scene3d_renderer
{
public:
	scene3d_renderer(device_t &device, memory_region *scene, memory_region *meshes);
	~scene3d_renderer();

	int camera_count() const { return int(m_cameras.size()); }
	void set_camera(int index) { m_camera = index; m_dirty = true; }

	void orbit(float dyaw, float dpitch);
	void zoom(float factor);
	void pan(float dx, float dy);
	void reset_view();

	int pick(float nx, float ny) const;
	const char *node_id(int index) const;
	void set_model_texture(const char *node_id, const bitmap_argb32 &tex);

	u32 render(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect);

	static void output_notifier(const char *outname, s32 value, void *param);

private:
	struct vec3
	{
		float x = 0.0f, y = 0.0f, z = 0.0f;

		vec3 operator+(const vec3 &o) const { return { x + o.x, y + o.y, z + o.z }; }
		vec3 operator-(const vec3 &o) const { return { x - o.x, y - o.y, z - o.z }; }
		vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
		bool operator==(const vec3 &o) const { return x == o.x && y == o.y && z == o.z; }
	};

	struct mesh
	{
		std::string name;
		vec3 origin;
		vec3 scale;
		u32 first;
		u32 count;
		vec3 bbmin, bbmax;
		int uaxis = -1, vaxis = -1;
		bool bounds_done = false;
	};

	struct node
	{
		std::string id;
		int parent;
		int mesh;
		vec3 pos, hpr;
		vec3 dpos, dhpr;
		vec3 tint;
		float scale;
		bool dynamic;
		float world[12];
	};

	struct motion
	{
		std::string signal;
		std::string target;
		int node;
		bool angular;
		float min, max;
		vec3 from, to;
	};

	struct camera
	{
		std::string target;
		int parent;
		int lookat;
		vec3 pos;
		vec3 offset;
		float fov;
	};

	bool load_meshes(memory_region *region);
	bool load_scene(memory_region *region);
	void parse_children(util::xml::data_node const &parent, int index);
	int find_node(const char *id) const;
	void bind_outputs();
	void output_change(const char *outname, s32 value);

	void update_world();
	void world_point(const float m[12], const vec3 &p, vec3 &out) const;
	void setup_view(int width, int height);
	void compute_mesh_bounds(mesh &me);
	void draw_node(bitmap_rgb32 &bitmap, const rectangle &cliprect, const node &n);
	bool project(const vec3 &world, float &sx, float &sy, float &sz) const;
	void raster_face(bitmap_rgb32 &bitmap, const rectangle &cliprect, const vec3 v[3], rgb_t colour,
			const bitmap_argb32 *tex, const float uv[3][2]);

	bool gl_available();
	bool gl_init(scene3d_gl &gl);
	bool gl_draw(scene3d_gl &gl, bitmap_rgb32 &bitmap, const rectangle &cliprect);
	bool gl_render(bitmap_rgb32 &bitmap, const rectangle &cliprect);

	device_t &m_device;
	bool m_loaded = false;
	bool m_bound = false;

	std::vector<mesh> m_meshes;
	std::unordered_map<std::string, int> m_mesh_index;
	const u8 *m_tris = nullptr;
	u32 m_tri_count = 0;

	std::vector<node> m_nodes;
	std::vector<motion> m_motions;
	std::vector<camera> m_cameras;
	std::unordered_map<int, bitmap_argb32> m_textures;
	std::unordered_map<std::string, std::vector<int> > m_signals;
	int m_light = -1;
	vec3 m_light_pos;
	int m_camera = 0;
	float m_orbit_yaw = 0.0f;
	float m_orbit_pitch = 0.0f;
	float m_orbit_zoom = 1.0f;
	float m_pan_x = 0.0f;
	float m_pan_y = 0.0f;

	bool m_dirty = true;
	bool m_have_frame = false;
	bool m_frame_live = false;
	int m_width = 0, m_height = 0;
	float m_view[3][4] = { };
	float m_focal = 0.0f;
	vec3 m_eye;
	vec3 m_light_world;

	std::unique_ptr<scene3d_gl> m_gl;
	bool m_gl_tried = false;

	std::vector<float> m_depth;
	bitmap_rgb32 m_cache;
	std::vector<float> m_cache_depth;
	bool m_cache_valid = false;
	float m_cache_view[3][4] = { };
	float m_cache_focal = 0.0f;
	int m_cache_width = 0, m_cache_height = 0;
	vec3 m_cache_light;
};

#endif // MAME_EMU_SCENE3D_H
