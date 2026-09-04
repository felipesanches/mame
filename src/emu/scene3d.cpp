// license:BSD-3-Clause
// copyright-holders:Felipe Sanches
/***************************************************************************

    scene3d.cpp

    Scene renderer behind SCREEN_TYPE_3D.

***************************************************************************/

#include "emu.h"
#include "scene3d.h"

#include "render.h"
#include "screen.h"

#include "xmlfile.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>


namespace {

constexpr float PI_F = 3.14159265358979f;
constexpr float NEAR_PLANE = 0.01f;
constexpr rgb_t BACKGROUND(0x1a, 0x1c, 0x22);

constexpr size_t HEAD_BYTES = 16;
constexpr size_t NAME_BYTES = 32;
constexpr size_t MESH_BYTES = NAME_BYTES + 3 * 4 + 3 * 4 + 4 + 4;
constexpr size_t TRI_BYTES = 4 + 9 * 2;

float clampf(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }

u16 rd16(const u8 *p) { return u16(p[0]) | (u16(p[1]) << 8); }
u32 rd32(const u8 *p)
{
	return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}
float rdf(const u8 *p)
{
	const u32 bits = rd32(p);
	float out;
	std::memcpy(&out, &bits, sizeof(out));
	return out;
}

void mat_identity(float m[12])
{
	for (int i = 0; i < 12; i++)
		m[i] = 0.0f;
	m[0] = m[5] = m[10] = 1.0f;
}

void mat_mul(const float a[12], const float b[12], float out[12])
{
	float t[12];
	for (int r = 0; r < 3; r++)
	{
		for (int c = 0; c < 3; c++)
			t[r * 4 + c] = a[r * 4 + 0] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c];
		t[r * 4 + 3] = a[r * 4 + 0] * b[3] + a[r * 4 + 1] * b[7] + a[r * 4 + 2] * b[11] + a[r * 4 + 3];
	}
	std::memcpy(out, t, sizeof(t));
}

void mat_rotate(float m[12], float degrees, float x, float y, float z)
{
	const float a = degrees * PI_F / 180.0f;
	const float c = std::cos(a), sn = std::sin(a), C = 1.0f - c;
	m[0] = x * x * C + c;      m[1] = x * y * C - z * sn; m[2]  = x * z * C + y * sn; m[3]  = 0.0f;
	m[4] = y * x * C + z * sn; m[5] = y * y * C + c;      m[6]  = y * z * C - x * sn; m[7]  = 0.0f;
	m[8] = z * x * C - y * sn; m[9] = z * y * C + x * sn; m[10] = z * z * C + c;      m[11] = 0.0f;
}

void mat_hpr(float m[12], float h, float p, float r)
{
	float rh[12], rp[12], rr[12];
	mat_rotate(rh, h, 0.0f, 0.0f, 1.0f);
	mat_rotate(rp, p, 1.0f, 0.0f, 0.0f);
	mat_rotate(rr, r, 0.0f, 1.0f, 0.0f);
	mat_mul(rh, rp, m);
	mat_mul(m, rr, m);
}

bool parse_floats(const char *text, float *out, int count)
{
	if (!text)
		return false;
	for (int i = 0; i < count; i++)
	{
		char *end = nullptr;
		out[i] = std::strtof(text, &end);
		if (end == text)
			return false;
		text = end;
		while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r')
			text++;
		if (i + 1 < count)
		{
			if (*text != ',')
				return false;
			text++;
		}
	}
	return true;
}

}


scene3d_renderer::scene3d_renderer(device_t &device, memory_region *scene, memory_region *meshes)
	: m_device(device)
{
	load_meshes(meshes);
	m_loaded = load_scene(scene);
}

bool scene3d_renderer::load_meshes(memory_region *region)
{
	if (!region)
	{
		m_device.logerror("scene3d: no mesh region\n");
		return false;
	}

	const u8 *const base = region->base();
	const size_t length = region->bytes();
	if (length < HEAD_BYTES || std::memcmp(base, "MM2M", 4))
	{
		m_device.logerror("scene3d: region %s is not a mesh pack\n", region->name());
		return false;
	}
	if (rd32(base + 4) != 4)
	{
		m_device.logerror("scene3d: mesh pack version %u, expected 4\n", rd32(base + 4));
		return false;
	}

	const u32 mesh_count = rd32(base + 8);
	const u32 tri_count = rd32(base + 12);
	if (length < HEAD_BYTES + size_t(mesh_count) * MESH_BYTES + size_t(tri_count) * TRI_BYTES)
	{
		m_device.logerror("scene3d: mesh pack is short: %u meshes and %u triangles need more than %u bytes\n",
				mesh_count, tri_count, u32(length));
		return false;
	}

	const u8 *p = base + HEAD_BYTES;
	m_meshes.reserve(mesh_count);
	for (u32 i = 0; i < mesh_count; i++)
	{
		mesh m;
		m.name.assign(reinterpret_cast<const char *>(p), strnlen(reinterpret_cast<const char *>(p), NAME_BYTES));
		p += NAME_BYTES;
		m.origin = { rdf(p), rdf(p + 4), rdf(p + 8) };            p += 12;
		m.scale  = { rdf(p), rdf(p + 4), rdf(p + 8) };            p += 12;
		m.first = rd32(p);                                        p += 4;
		m.count = rd32(p);                                        p += 4;
		if (m.first > tri_count || m.count > tri_count - m.first)
		{
			m_device.logerror("scene3d: mesh %s claims triangles %u..%u of %u\n",
					m.name, m.first, m.first + m.count, tri_count);
			m_meshes.clear();
			return false;
		}
		m_meshes.push_back(std::move(m));
	}
	m_tris = p;
	m_tri_count = tri_count;

	osd_printf_verbose("scene3d: %u meshes, %u triangles in %s\n", mesh_count, tri_count, region->name());
	return true;
}

bool scene3d_renderer::load_scene(memory_region *region)
{
	if (!region)
	{
		m_device.logerror("scene3d: no scene region\n");
		return false;
	}

	const char *const text = reinterpret_cast<const char *>(region->base());
	const std::string document(text, strnlen(text, region->bytes()));
	util::xml::file::ptr const xml = util::xml::file::string_read(document.c_str(), nullptr);
	util::xml::data_node const *const root = xml ? xml->get_child("mame3dlayout") : nullptr;
	if (!root)
	{
		m_device.logerror("scene3d: region %s is not a mame3dlayout document\n", region->name());
		return false;
	}

	node top;
	top.parent = -1;
	top.mesh = -1;
	top.tint = { 1.0f, 1.0f, 1.0f };
	top.scale = 1.0f;
	top.dynamic = false;
	m_nodes.push_back(top);
	parse_children(*root, 0);

	std::vector<bool> targeted(m_nodes.size(), false);
	for (auto it = m_motions.begin(); it != m_motions.end(); )
	{
		it->node = find_node(it->target.c_str());
		if (it->node < 0)
		{
			m_device.logerror("scene3d: motion target %s not found\n", it->target);
			it = m_motions.erase(it);
			continue;
		}
		targeted[it->node] = true;
		++it;
	}
	for (size_t i = 0; i < m_motions.size(); i++)
		m_signals[m_motions[i].signal].push_back(int(i));

	for (size_t i = 1; i < m_nodes.size(); i++)
		m_nodes[i].dynamic = targeted[i] || m_nodes[m_nodes[i].parent].dynamic;

	for (camera &c : m_cameras)
	{
		c.lookat = find_node(c.target.c_str());
		if (c.lookat < 0)
		{
			m_device.logerror("scene3d: camera target %s not found\n", c.target);
			c.lookat = 0;
		}
	}
	if (m_cameras.empty())
	{
		camera c;
		c.parent = 0;
		c.lookat = std::max(0, find_node("static"));
		c.pos = { 0.0f, 40.0f, 5.0f };
		c.offset = { 0.0f, 0.0f, 2.0f };
		c.fov = 50.0f;
		m_cameras.push_back(c);
	}

	osd_printf_verbose("scene3d: %u nodes, %u motions, %u cameras in %s\n",
			u32(m_nodes.size()), u32(m_motions.size()), u32(m_cameras.size()), region->name());
	return true;
}

void scene3d_renderer::parse_children(util::xml::data_node const &parent, int index)
{
	for (util::xml::data_node const *child = parent.get_first_child(); child; child = child->get_next_sibling())
	{
		const char *const name = child->get_name();
		if (!name)
			continue;

		vec3 position, hpr;
		const char *const pos = child->get_attribute_string("position", child->get_attribute_string("pos", nullptr));
		parse_floats(pos, &position.x, 3);
		parse_floats(child->get_attribute_string("hpr", nullptr), &hpr.x, 3);

		if (!std::strcmp(name, "group") || !std::strcmp(name, "model"))
		{
			node n;
			n.id = child->get_attribute_string("id", "");
			n.parent = index;
			n.mesh = -1;
			n.pos = position;
			n.hpr = hpr;
			n.tint = { 1.0f, 1.0f, 1.0f };
			n.scale = 1.0f;
			n.dynamic = false;

			if (name[0] == 'm')
			{
				const char *const mesh_name = child->get_attribute_string("name", "");
				auto found = m_mesh_index.find(mesh_name);
				if (found == m_mesh_index.end())
				{
					int mesh = -1;
					for (size_t i = 0; i < m_meshes.size() && mesh < 0; i++)
						if (m_meshes[i].name == mesh_name)
							mesh = int(i);
					if (mesh < 0)
						m_device.logerror("scene3d: model %s is not in the mesh pack\n", mesh_name);
					found = m_mesh_index.emplace(mesh_name, mesh).first;
				}
				n.mesh = found->second;
				if (n.mesh < 0)
					continue;

				float colour[4];
				if (parse_floats(child->get_attribute_string("color", nullptr), colour, 3))
					n.tint = { colour[0], colour[1], colour[2] };
				parse_floats(child->get_attribute_string("scale", nullptr), &n.scale, 1);
			}

			m_nodes.push_back(n);
			parse_children(*child, int(m_nodes.size()) - 1);
		}
		else if (!std::strcmp(name, "light"))
		{
			if (m_light < 0)
			{
				m_light = index;
				m_light_pos = position;
			}
		}
		else if (!std::strcmp(name, "camera"))
		{
			camera c;
			c.parent = index;
			c.lookat = 0;
			c.target = child->get_attribute_string("lookat", "");
			c.pos = position;
			parse_floats(child->get_attribute_string("lookat_offset", nullptr), &c.offset.x, 3);
			c.fov = 80.0f;
			parse_floats(child->get_attribute_string("fov", nullptr), &c.fov, 1);
			m_cameras.push_back(c);
		}
		else if (!std::strcmp(name, "motion"))
		{
			motion m;
			m.signal = child->get_attribute_string("signal", "");
			m.target = child->get_attribute_string("target", "");
			m.node = -1;
			const char *const type = child->get_attribute_string("type", "");
			m.angular = !std::strcmp(type, "angular");
			m.min = 0.0f;
			m.max = 1.0f;
			parse_floats(child->get_attribute_string("min", nullptr), &m.min, 1);
			parse_floats(child->get_attribute_string("max", nullptr), &m.max, 1);
			const bool ok = (m.angular || !std::strcmp(type, "linear")) && !m.signal.empty()
					&& parse_floats(child->get_attribute_string("from", nullptr), &m.from.x, 3)
					&& parse_floats(child->get_attribute_string("to", nullptr), &m.to.x, 3);
			if (ok)
				m_motions.push_back(std::move(m));
			else
				m_device.logerror("scene3d: motion on %s needs signal, type linear or angular, from and to\n", m.target);
		}
	}
}

int scene3d_renderer::find_node(const char *id) const
{
	if (!id || !*id)
		return -1;
	for (size_t i = 0; i < m_nodes.size(); i++)
		if (m_nodes[i].id == id)
			return int(i);
	return -1;
}


void scene3d_renderer::output_notifier(const char *outname, s32 value, void *param)
{
	static_cast<scene3d_renderer *>(param)->output_change(outname, value);
}

void scene3d_renderer::output_change(const char *outname, s32 value)
{
	const auto found = m_signals.find(outname);
	if (found == m_signals.end())
		return;

	for (int i : found->second)
	{
		const motion &m = m_motions[i];
		node &n = m_nodes[m.node];
		const float t = (m.max > m.min) ? clampf((float(value) - m.min) / (m.max - m.min), 0.0f, 1.0f) : 0.0f;
		const vec3 v = m.from + (m.to - m.from) * t;
		vec3 &target = m.angular ? n.dhpr : n.dpos;
		if (!(target == v))
		{
			target = v;
			m_dirty = true;
		}
	}
}

void scene3d_renderer::bind_outputs()
{
	m_bound = true;

	std::vector<std::string> unbound;
	for (const auto &signal : m_signals)
		unbound.push_back(signal.first);

	m_device.machine().output().notify_all(
			[this, &unbound] (const char *name, s32 value)
			{
				const auto found = std::find(unbound.begin(), unbound.end(), name);
				if (found != unbound.end())
				{
					unbound.erase(found);
					output_change(name, value);
				}
			});

	if (unbound.empty())
		return;

	std::sort(unbound.begin(), unbound.end());
	std::string list;
	for (const std::string &name : unbound)
		list += (list.empty() ? "" : ", ") + name;
	m_device.logerror("scene3d: no output publishes %s\n", list);
}


void scene3d_renderer::update_world()
{
	for (node &n : m_nodes)
	{
		const vec3 p = n.pos + n.dpos;
		const vec3 a = n.hpr + n.dhpr;
		float local[12];
		mat_hpr(local, a.x, a.y, a.z);
		for (int i = 0; i < 12; i++)
			if ((i & 3) != 3)
				local[i] *= n.scale;
		local[3] = p.x;
		local[7] = p.y;
		local[11] = p.z;
		if (n.parent < 0)
			std::copy(std::begin(local), std::end(local), n.world);
		else
			mat_mul(m_nodes[n.parent].world, local, n.world);
	}
}

void scene3d_renderer::world_point(const float m[12], const vec3 &p, vec3 &out) const
{
	out.x = m[0] * p.x + m[1] * p.y + m[2]  * p.z + m[3];
	out.y = m[4] * p.x + m[5] * p.y + m[6]  * p.z + m[7];
	out.z = m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11];
}

void scene3d_renderer::setup_view(int width, int height)
{
	const camera &c = m_cameras[std::clamp(m_camera, 0, int(m_cameras.size()) - 1)];

	vec3 eye, target;
	world_point(m_nodes[c.parent].world, c.pos, eye);
	world_point(m_nodes[c.lookat].world, c.offset, target);

	const auto normalise = [] (vec3 &v) -> float
	{
		const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		if (l > 1e-9f)
			v = v * (1.0f / l);
		return l;
	};
	const auto cross = [] (const vec3 &a, const vec3 &b) -> vec3
	{
		return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	};

	vec3 fwd = target - eye;
	if (normalise(fwd) <= 1e-9f)
		fwd = { 0.0f, 1.0f, 0.0f };
	vec3 right = cross(fwd, { 0.0f, 0.0f, 1.0f });
	if (normalise(right) <= 1e-6f)
	{
		right = cross(fwd, { 0.0f, 1.0f, 0.0f });
		normalise(right);
	}
	const vec3 up = cross(right, fwd);

	m_view[0][0] = right.x; m_view[0][1] = right.y; m_view[0][2] = right.z;
	m_view[1][0] = up.x;    m_view[1][1] = up.y;    m_view[1][2] = up.z;
	m_view[2][0] = fwd.x;   m_view[2][1] = fwd.y;   m_view[2][2] = fwd.z;
	m_view[0][3] = -(right.x * eye.x + right.y * eye.y + right.z * eye.z);
	m_view[1][3] = -(up.x * eye.x + up.y * eye.y + up.z * eye.z);
	m_view[2][3] = -(fwd.x * eye.x + fwd.y * eye.y + fwd.z * eye.z);

	const float half = clampf(c.fov, 1.0f, 179.0f) * 0.5f * PI_F / 180.0f;
	m_focal = float(width) * 0.5f / std::tan(half);

	if (m_light >= 0)
		world_point(m_nodes[m_light].world, m_light_pos, m_light_world);
}


bool scene3d_renderer::project(const vec3 &world, float &sx, float &sy, float &sz) const
{
	const float ex = m_view[0][0] * world.x + m_view[0][1] * world.y + m_view[0][2] * world.z + m_view[0][3];
	const float ey = m_view[1][0] * world.x + m_view[1][1] * world.y + m_view[1][2] * world.z + m_view[1][3];
	const float ez = m_view[2][0] * world.x + m_view[2][1] * world.y + m_view[2][2] * world.z + m_view[2][3];

	if (ez < NEAR_PLANE)
		return false;

	sx = float(m_width) * 0.5f + m_focal * ex / ez;
	sy = float(m_height) * 0.5f - m_focal * ey / ez;
	sz = ez;
	return true;
}

void scene3d_renderer::raster_face(bitmap_rgb32 &bitmap, const rectangle &cliprect, const vec3 v[3], rgb_t colour)
{
	float px[3], py[3], pz[3];
	for (int i = 0; i < 3; i++)
	{
		if (!project(v[i], px[i], py[i], pz[i]))
			return;
		px[i] = clampf(px[i], -1e6f, 1e6f);
		py[i] = clampf(py[i], -1e6f, 1e6f);
	}

	const float area = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
	if (area >= 0.0f)
		return;

	int min_x = std::max(cliprect.min_x, int(std::floor(std::min({ px[0], px[1], px[2] }))));
	int max_x = std::min(cliprect.max_x, int(std::ceil (std::max({ px[0], px[1], px[2] }))));
	int min_y = std::max(cliprect.min_y, int(std::floor(std::min({ py[0], py[1], py[2] }))));
	int max_y = std::min(cliprect.max_y, int(std::ceil (std::max({ py[0], py[1], py[2] }))));
	if (min_x > max_x || min_y > max_y)
		return;

	const vec3 e0 = v[1] - v[0];
	const vec3 e1 = v[2] - v[0];
	vec3 n{ e0.y * e1.z - e0.z * e1.y, e0.z * e1.x - e0.x * e1.z, e0.x * e1.y - e0.y * e1.x };
	const float nl = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
	if (nl > 0.0001f)
		n = n * (1.0f / nl);

	vec3 dir{ -0.4f, -0.5f, 0.77f };
	if (m_light >= 0)
	{
		dir = m_light_world - (v[0] + v[1] + v[2]) * (1.0f / 3.0f);
		const float dl = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
		if (dl > 1e-6f)
			dir = dir * (1.0f / dl);
	}
	const float lambert = clampf(0.35f + 0.65f * std::fabs(n.x * dir.x + n.y * dir.y + n.z * dir.z), 0.0f, 1.0f);
	const rgb_t shade(u8(colour.r() * lambert), u8(colour.g() * lambert), u8(colour.b() * lambert));

	if (min_x == max_x && min_y == max_y)
	{
		const float z = (pz[0] + pz[1] + pz[2]) * (1.0f / 3.0f);
		float &depth = m_depth[size_t(min_y) * m_width + min_x];
		if (z < depth)
		{
			depth = z;
			bitmap.pix(min_y, min_x) = shade;
		}
		return;
	}

	const float inv_area = 1.0f / area;
	const float w0_dx = -(py[1] - py[0]) * inv_area;
	const float w0_dy = (px[1] - px[0]) * inv_area;
	const float w1_dx = (py[2] - py[0]) * inv_area;
	const float w1_dy = -(px[2] - px[0]) * inv_area;

	const float fx0 = float(min_x) + 0.5f;
	const float fy0 = float(min_y) + 0.5f;
	float w0_row = ((px[1] - px[0]) * (fy0 - py[0]) - (fx0 - px[0]) * (py[1] - py[0])) * inv_area;
	float w1_row = ((fx0 - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (fy0 - py[0])) * inv_area;

	for (int y = min_y; y <= max_y; y++, w0_row += w0_dy, w1_row += w1_dy)
	{
		float w0 = w0_row;
		float w1 = w1_row;
		u32 *const line = &bitmap.pix(y, 0);
		float *const depth = &m_depth[size_t(y) * m_width];

		for (int x = min_x; x <= max_x; x++, w0 += w0_dx, w1 += w1_dx)
		{
			if (w0 < 0.0f || w1 < 0.0f || (w0 + w1) > 1.0f)
				continue;

			const float z = (1.0f - w0 - w1) * pz[0] + w1 * pz[1] + w0 * pz[2];
			if (z < depth[x])
			{
				depth[x] = z;
				line[x] = shade;
			}
		}
	}
}

void scene3d_renderer::draw_node(bitmap_rgb32 &bitmap, const rectangle &cliprect, const node &n)
{
	const mesh &me = m_meshes[n.mesh];

	float dq[12], m[12];
	mat_identity(dq);
	dq[0] = me.scale.x;  dq[3]  = me.origin.x;
	dq[5] = me.scale.y;  dq[7]  = me.origin.y;
	dq[10] = me.scale.z; dq[11] = me.origin.z;
	mat_mul(n.world, dq, m);

	const u8 *tri = m_tris + size_t(me.first) * TRI_BYTES;
	for (u32 j = 0; j < me.count; j++, tri += TRI_BYTES)
	{
		const rgb_t colour(u8(tri[0] * n.tint.x), u8(tri[1] * n.tint.y), u8(tri[2] * n.tint.z));
		vec3 v[3];
		for (int k = 0; k < 3; k++)
		{
			const float q0 = float(rd16(tri + 4 + k * 6));
			const float q1 = float(rd16(tri + 6 + k * 6));
			const float q2 = float(rd16(tri + 8 + k * 6));
			v[k].x = m[0] * q0 + m[1] * q1 + m[2]  * q2 + m[3];
			v[k].y = m[4] * q0 + m[5] * q1 + m[6]  * q2 + m[7];
			v[k].z = m[8] * q0 + m[9] * q1 + m[10] * q2 + m[11];
		}
		raster_face(bitmap, cliprect, v, colour);
	}
}


u32 scene3d_renderer::render(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	if (!m_bound)
		bind_outputs();

	const int width = bitmap.width();
	const int height = bitmap.height();
	const bool same_size = (width == m_width) && (height == m_height);

	if (!m_loaded || !screen.machine().render().is_live(screen))
	{
		if (m_have_frame && !m_frame_live && same_size)
			return UPDATE_HAS_NOT_CHANGED;
		bitmap.fill(BACKGROUND, cliprect);
		m_width = width;
		m_height = height;
		m_have_frame = true;
		m_frame_live = false;
		return 0;
	}

	if (m_have_frame && m_frame_live && !m_dirty && same_size)
		return UPDATE_HAS_NOT_CHANGED;

	m_width = width;
	m_height = height;
	update_world();
	setup_view(width, height);

	const bool stale = !m_cache_valid
			|| std::memcmp(m_cache_view, m_view, sizeof(m_view))
			|| (m_cache_focal != m_focal)
			|| (m_cache_width != width) || (m_cache_height != height)
			|| !(m_cache_light == m_light_world);

	if (stale)
	{
		if (m_cache.width() != width || m_cache.height() != height)
			m_cache.allocate(width, height);
		m_cache_depth.assign(size_t(width) * height, 1e30f);
		m_cache.fill(BACKGROUND);

		m_depth.swap(m_cache_depth);
		for (const node &n : m_nodes)
			if (n.mesh >= 0 && !n.dynamic)
				draw_node(m_cache, m_cache.cliprect(), n);
		m_depth.swap(m_cache_depth);

		std::memcpy(m_cache_view, m_view, sizeof(m_view));
		m_cache_focal = m_focal;
		m_cache_width = width;
		m_cache_height = height;
		m_cache_light = m_light_world;
		m_cache_valid = true;
	}

	m_depth = m_cache_depth;
	for (int y = cliprect.top(); y <= cliprect.bottom(); y++)
		std::copy(&m_cache.pix(y, cliprect.left()), &m_cache.pix(y, cliprect.right()) + 1,
				  &bitmap.pix(y, cliprect.left()));

	for (const node &n : m_nodes)
		if (n.mesh >= 0 && n.dynamic)
			draw_node(bitmap, cliprect, n);

	m_dirty = false;
	m_have_frame = true;
	m_frame_live = true;
	return 0;
}
