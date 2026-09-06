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
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#define SCENE3D_HAVE_DL 1
#endif


using egl_enum = unsigned int;
using egl_int = int;
using gl_enum = unsigned int;
using gl_uint = unsigned int;
using gl_int = int;
using gl_sizei = int;
using gl_bitfield = unsigned int;
using gl_boolean = unsigned char;
using gl_float = float;
using gl_char = char;
using gl_sizeiptr = std::intptr_t;

namespace {

constexpr egl_enum EGL_PLATFORM_SURFACELESS_MESA = 0x31DD;
constexpr egl_int EGL_NONE_ = 0x3038;
constexpr egl_int EGL_SURFACE_TYPE = 0x3033;
constexpr egl_int EGL_PBUFFER_BIT = 0x0001;
constexpr egl_int EGL_RENDERABLE_TYPE = 0x3040;
constexpr egl_int EGL_OPENGL_ES3_BIT = 0x0040;
constexpr egl_int EGL_RED_SIZE = 0x3024;
constexpr egl_int EGL_GREEN_SIZE = 0x3023;
constexpr egl_int EGL_BLUE_SIZE = 0x3022;
constexpr egl_int EGL_DEPTH_SIZE = 0x3025;
constexpr egl_int EGL_WIDTH = 0x3057;
constexpr egl_int EGL_HEIGHT = 0x3056;
constexpr egl_enum EGL_OPENGL_ES_API = 0x30A0;
constexpr egl_int EGL_CONTEXT_CLIENT_VERSION = 0x3098;

constexpr gl_enum GL_FRAGMENT_SHADER = 0x8B30;
constexpr gl_enum GL_VERTEX_SHADER = 0x8B31;
constexpr gl_enum GL_COMPILE_STATUS = 0x8B81;
constexpr gl_enum GL_LINK_STATUS = 0x8B82;
constexpr gl_enum GL_ARRAY_BUFFER = 0x8892;
constexpr gl_enum GL_STATIC_DRAW = 0x88E4;
constexpr gl_enum GL_FLOAT = 0x1406;
constexpr gl_enum GL_FRAMEBUFFER = 0x8D40;
constexpr gl_enum GL_RENDERBUFFER = 0x8D41;
constexpr gl_enum GL_COLOR_ATTACHMENT0 = 0x8CE0;
constexpr gl_enum GL_DEPTH_ATTACHMENT = 0x8D00;
constexpr gl_enum GL_RGBA8 = 0x8058;
constexpr gl_enum GL_DEPTH_COMPONENT24 = 0x81A6;
constexpr gl_enum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr gl_enum GL_DEPTH_TEST = 0x0B71;
constexpr gl_enum GL_CULL_FACE = 0x0B44;
constexpr gl_enum GL_BACK = 0x0405;
constexpr gl_enum GL_CCW = 0x0901;
constexpr gl_enum GL_TRIANGLES = 0x0004;
constexpr gl_enum GL_RGBA = 0x1908;
constexpr gl_enum GL_UNSIGNED_BYTE = 0x1401;
constexpr gl_bitfield GL_COLOR_BUFFER_BIT = 0x00004000;
constexpr gl_bitfield GL_DEPTH_BUFFER_BIT = 0x00000100;
constexpr gl_enum GL_RENDERER = 0x1F01;
constexpr gl_enum GL_VERSION = 0x1F02;

const char *const GL_VERTEX_SRC =
	"#version 300 es\n"
	"in vec3 p;in vec3 nrm;in vec3 cen;in vec3 col;\n"
	"uniform mat4 u_mvp;uniform mat4 u_model;uniform mat3 u_nrm;uniform vec3 u_tint;\n"
	"flat out vec3 v_col;flat out vec3 v_wn;flat out vec3 v_wc;\n"
	"void main(){gl_Position=u_mvp*vec4(p,1.0);v_col=col*u_tint;\n"
	"v_wn=normalize(u_nrm*nrm);v_wc=(u_model*vec4(cen,1.0)).xyz;}\n";
const char *const GL_FRAGMENT_SRC =
	"#version 300 es\n"
	"precision highp float;\n"
	"flat in vec3 v_col;flat in vec3 v_wn;flat in vec3 v_wc;\n"
	"uniform int u_haslight;uniform vec3 u_light;\n"
	"out vec4 o;\n"
	"void main(){vec3 dir=vec3(-0.4,-0.5,0.77);\n"
	"if(u_haslight!=0){dir=normalize(u_light-v_wc);}\n"
	"float lam=clamp(0.35+0.65*abs(dot(normalize(v_wn),dir)),0.0,1.0);\n"
	"o=vec4(clamp(v_col,0.0,1.0)*lam,1.0);}\n";

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


struct scene3d_gl
{
	void *libegl = nullptr;
	void *libgl = nullptr;
	void *display = nullptr;
	void *surface = nullptr;
	void *context = nullptr;

	void *(*eglGetProcAddress)(const char *) = nullptr;
	void *(*eglGetPlatformDisplay)(egl_enum, void *, const egl_int *) = nullptr;
	unsigned (*eglInitialize)(void *, egl_int *, egl_int *) = nullptr;
	unsigned (*eglBindAPI)(egl_enum) = nullptr;
	unsigned (*eglChooseConfig)(void *, const egl_int *, void **, egl_int, egl_int *) = nullptr;
	void *(*eglCreatePbufferSurface)(void *, void *, const egl_int *) = nullptr;
	void *(*eglCreateContext)(void *, void *, void *, const egl_int *) = nullptr;
	unsigned (*eglMakeCurrent)(void *, void *, void *, void *) = nullptr;
	egl_int (*eglGetError)() = nullptr;

	const unsigned char *(*glGetString)(gl_enum) = nullptr;
	gl_uint (*glCreateShader)(gl_enum) = nullptr;
	void (*glShaderSource)(gl_uint, gl_sizei, const gl_char *const *, const gl_int *) = nullptr;
	void (*glCompileShader)(gl_uint) = nullptr;
	void (*glGetShaderiv)(gl_uint, gl_enum, gl_int *) = nullptr;
	void (*glGetShaderInfoLog)(gl_uint, gl_sizei, gl_sizei *, gl_char *) = nullptr;
	gl_uint (*glCreateProgram)() = nullptr;
	void (*glAttachShader)(gl_uint, gl_uint) = nullptr;
	void (*glBindAttribLocation)(gl_uint, gl_uint, const gl_char *) = nullptr;
	void (*glLinkProgram)(gl_uint) = nullptr;
	void (*glGetProgramiv)(gl_uint, gl_enum, gl_int *) = nullptr;
	void (*glUseProgram)(gl_uint) = nullptr;
	gl_int (*glGetUniformLocation)(gl_uint, const gl_char *) = nullptr;
	void (*glGenBuffers)(gl_sizei, gl_uint *) = nullptr;
	void (*glBindBuffer)(gl_enum, gl_uint) = nullptr;
	void (*glBufferData)(gl_enum, gl_sizeiptr, const void *, gl_enum) = nullptr;
	void (*glEnableVertexAttribArray)(gl_uint) = nullptr;
	void (*glVertexAttribPointer)(gl_uint, gl_int, gl_enum, gl_boolean, gl_sizei, const void *) = nullptr;
	void (*glUniformMatrix4fv)(gl_int, gl_sizei, gl_boolean, const gl_float *) = nullptr;
	void (*glUniformMatrix3fv)(gl_int, gl_sizei, gl_boolean, const gl_float *) = nullptr;
	void (*glUniform3f)(gl_int, gl_float, gl_float, gl_float) = nullptr;
	void (*glUniform1i)(gl_int, gl_int) = nullptr;
	void (*glGenFramebuffers)(gl_sizei, gl_uint *) = nullptr;
	void (*glBindFramebuffer)(gl_enum, gl_uint) = nullptr;
	void (*glGenRenderbuffers)(gl_sizei, gl_uint *) = nullptr;
	void (*glBindRenderbuffer)(gl_enum, gl_uint) = nullptr;
	void (*glRenderbufferStorage)(gl_enum, gl_enum, gl_sizei, gl_sizei) = nullptr;
	void (*glFramebufferRenderbuffer)(gl_enum, gl_enum, gl_enum, gl_uint) = nullptr;
	gl_enum (*glCheckFramebufferStatus)(gl_enum) = nullptr;
	void (*glViewport)(gl_int, gl_int, gl_sizei, gl_sizei) = nullptr;
	void (*glClearColor)(gl_float, gl_float, gl_float, gl_float) = nullptr;
	void (*glClear)(gl_bitfield) = nullptr;
	void (*glEnable)(gl_enum) = nullptr;
	void (*glCullFace)(gl_enum) = nullptr;
	void (*glFrontFace)(gl_enum) = nullptr;
	void (*glDrawArrays)(gl_enum, gl_int, gl_sizei) = nullptr;
	void (*glReadPixels)(gl_int, gl_int, gl_sizei, gl_sizei, gl_enum, gl_enum, void *) = nullptr;
	void (*glFinish)() = nullptr;

	gl_uint program = 0;
	gl_int u_mvp = -1, u_model = -1, u_nrm = -1, u_tint = -1, u_haslight = -1, u_light = -1;
	gl_uint fbo = 0, colour_rb = 0, depth_rb = 0;
	int fbo_width = 0, fbo_height = 0;

	std::map<int, std::pair<gl_uint, gl_sizei> > mesh_cache;
	std::vector<uint8_t> pixels;

	std::thread worker;
	std::mutex mtx;
	std::condition_variable cv;
	std::function<void()> job;
	bool have_job = false, done = false, stop = false, started = false;

	void start()
	{
		worker = std::thread([this] ()
		{
			std::unique_lock<std::mutex> lock(mtx);
			while (true)
			{
				cv.wait(lock, [this] () { return have_job || stop; });
				if (stop)
					break;
				std::function<void()> j = std::move(job);
				have_job = false;
				lock.unlock();
				j();
				lock.lock();
				done = true;
				cv.notify_all();
			}
		});
		started = true;
	}

	void exec(std::function<void()> f)
	{
		std::unique_lock<std::mutex> lock(mtx);
		job = std::move(f);
		have_job = true;
		done = false;
		cv.notify_all();
		cv.wait(lock, [this] () { return done; });
	}

	~scene3d_gl()
	{
		if (started)
		{
			{
				std::unique_lock<std::mutex> lock(mtx);
				stop = true;
				cv.notify_all();
			}
			worker.join();
		}
	}
};


scene3d_renderer::scene3d_renderer(device_t &device, memory_region *scene, memory_region *meshes)
	: m_device(device)
{
	load_meshes(meshes);
	m_loaded = load_scene(scene);
}

scene3d_renderer::~scene3d_renderer()
{
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


namespace {

void mat4_from_34(const float m[12], float out[16])
{
	std::memcpy(out, m, 12 * sizeof(float));
	out[12] = out[13] = out[14] = 0.0f;
	out[15] = 1.0f;
}

void mat4_mul(const float a[16], const float b[16], float out[16])
{
	float t[16];
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			t[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c]
					+ a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
	std::memcpy(out, t, sizeof(t));
}

void normal_matrix(const float m[12], float out[9])
{
	const float a = m[0], b = m[1], c = m[2];
	const float d = m[4], e = m[5], f = m[6];
	const float g = m[8], h = m[9], i = m[10];
	const float c00 = e * i - f * h, c01 = -(d * i - f * g), c02 = d * h - e * g;
	const float c10 = -(b * i - c * h), c11 = a * i - c * g, c12 = -(a * h - b * g);
	const float c20 = b * f - c * e, c21 = -(a * f - c * d), c22 = a * e - b * d;
	const float det = a * c00 + b * c01 + c * c02;
	if (std::fabs(det) < 1e-20f)
	{
		out[0] = out[4] = out[8] = 1.0f;
		out[1] = out[2] = out[3] = out[5] = out[6] = out[7] = 0.0f;
		return;
	}
	const float id = 1.0f / det;
	out[0] = c00 * id; out[1] = c01 * id; out[2] = c02 * id;
	out[3] = c10 * id; out[4] = c11 * id; out[5] = c12 * id;
	out[6] = c20 * id; out[7] = c21 * id; out[8] = c22 * id;
}

}


bool scene3d_renderer::gl_available()
{
	if (m_gl_tried)
		return bool(m_gl);
	m_gl_tried = true;

	if (std::getenv("SCENE3D_SOFT"))
	{
		osd_printf_verbose("scene3d: SCENE3D_SOFT set, using the software renderer\n");
		return false;
	}

#ifndef SCENE3D_HAVE_DL
	osd_printf_verbose("scene3d: no runtime GL loader on this platform, using the software renderer\n");
	return false;
#else
	auto gl = std::make_unique<scene3d_gl>();
	gl->start();
	bool ok = false;
	gl->exec([&] () { ok = gl_init(*gl); });
	if (!ok)
		return false;
	m_gl = std::move(gl);
	return true;
#endif
}

bool scene3d_renderer::gl_init(scene3d_gl &gl)
{
#ifdef SCENE3D_HAVE_DL
	gl.libegl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
	gl.libgl = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
	if (!gl.libegl || !gl.libgl)
	{
		osd_printf_verbose("scene3d: no EGL/GLES runtime, using the software renderer\n");
		return false;
	}

	gl.eglGetProcAddress = reinterpret_cast<decltype(gl.eglGetProcAddress)>(dlsym(gl.libegl, "eglGetProcAddress"));
	if (!gl.eglGetProcAddress)
		return false;

	bool ok = true;
	auto egl = [&] (const char *name) { void *p = gl.eglGetProcAddress(name); if (!p) ok = false; return p; };
	auto ges = [&] (const char *name)
	{
		void *p = dlsym(gl.libgl, name);
		if (!p) p = gl.eglGetProcAddress(name);
		if (!p) ok = false;
		return p;
	};
#define EGLSYM(f) gl.f = reinterpret_cast<decltype(gl.f)>(egl(#f))
#define GLSYM(f) gl.f = reinterpret_cast<decltype(gl.f)>(ges(#f))
	EGLSYM(eglGetPlatformDisplay); EGLSYM(eglInitialize); EGLSYM(eglBindAPI);
	EGLSYM(eglChooseConfig); EGLSYM(eglCreatePbufferSurface); EGLSYM(eglCreateContext);
	EGLSYM(eglMakeCurrent); EGLSYM(eglGetError);
	GLSYM(glGetString); GLSYM(glCreateShader); GLSYM(glShaderSource); GLSYM(glCompileShader);
	GLSYM(glGetShaderiv); GLSYM(glGetShaderInfoLog); GLSYM(glCreateProgram); GLSYM(glAttachShader);
	GLSYM(glBindAttribLocation); GLSYM(glLinkProgram); GLSYM(glGetProgramiv); GLSYM(glUseProgram);
	GLSYM(glGetUniformLocation); GLSYM(glGenBuffers); GLSYM(glBindBuffer); GLSYM(glBufferData);
	GLSYM(glEnableVertexAttribArray); GLSYM(glVertexAttribPointer); GLSYM(glUniformMatrix4fv);
	GLSYM(glUniformMatrix3fv); GLSYM(glUniform3f); GLSYM(glUniform1i); GLSYM(glGenFramebuffers);
	GLSYM(glBindFramebuffer); GLSYM(glGenRenderbuffers); GLSYM(glBindRenderbuffer);
	GLSYM(glRenderbufferStorage); GLSYM(glFramebufferRenderbuffer); GLSYM(glCheckFramebufferStatus);
	GLSYM(glViewport); GLSYM(glClearColor); GLSYM(glClear); GLSYM(glEnable); GLSYM(glCullFace);
	GLSYM(glFrontFace); GLSYM(glDrawArrays); GLSYM(glReadPixels); GLSYM(glFinish);
#undef EGLSYM
#undef GLSYM
	if (!ok)
	{
		osd_printf_verbose("scene3d: EGL/GLES entry points missing, using the software renderer\n");
		return false;
	}

	gl.display = gl.eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, nullptr, nullptr);
	egl_int major = 0, minor = 0;
	if (!gl.display || !gl.eglInitialize(gl.display, &major, &minor))
	{
		osd_printf_verbose("scene3d: eglInitialize failed, using the software renderer\n");
		return false;
	}
	gl.eglBindAPI(EGL_OPENGL_ES_API);

	const egl_int config_attr[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE_ };
	void *config = nullptr;
	egl_int configs = 0;
	if (!gl.eglChooseConfig(gl.display, config_attr, &config, 1, &configs) || configs < 1)
	{
		osd_printf_verbose("scene3d: eglChooseConfig found no ES3 config, using the software renderer\n");
		return false;
	}
	const egl_int pbuffer_attr[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE_ };
	gl.surface = gl.eglCreatePbufferSurface(gl.display, config, pbuffer_attr);
	const egl_int context_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE_ };
	gl.context = gl.eglCreateContext(gl.display, config, nullptr, context_attr);
	if (!gl.surface || !gl.context || !gl.eglMakeCurrent(gl.display, gl.surface, gl.surface, gl.context))
	{
		osd_printf_verbose("scene3d: could not make an ES3 context current (egl 0x%x), using the software renderer\n",
				unsigned(gl.eglGetError()));
		return false;
	}

	const auto compile = [&] (gl_enum type, const char *src)
	{
		gl_uint s = gl.glCreateShader(type);
		gl.glShaderSource(s, 1, &src, nullptr);
		gl.glCompileShader(s);
		gl_int status = 0;
		gl.glGetShaderiv(s, GL_COMPILE_STATUS, &status);
		if (!status)
		{
			char log[512] = { };
			gl.glGetShaderInfoLog(s, sizeof(log), nullptr, log);
			m_device.logerror("scene3d: shader compile failed: %s\n", log);
		}
		return s;
	};
	gl.program = gl.glCreateProgram();
	gl.glAttachShader(gl.program, compile(GL_VERTEX_SHADER, GL_VERTEX_SRC));
	gl.glAttachShader(gl.program, compile(GL_FRAGMENT_SHADER, GL_FRAGMENT_SRC));
	gl.glBindAttribLocation(gl.program, 0, "p");
	gl.glBindAttribLocation(gl.program, 1, "nrm");
	gl.glBindAttribLocation(gl.program, 2, "cen");
	gl.glBindAttribLocation(gl.program, 3, "col");
	gl.glLinkProgram(gl.program);
	gl_int linked = 0;
	gl.glGetProgramiv(gl.program, GL_LINK_STATUS, &linked);
	if (!linked)
	{
		osd_printf_verbose("scene3d: shader link failed, using the software renderer\n");
		return false;
	}
	gl.u_mvp = gl.glGetUniformLocation(gl.program, "u_mvp");
	gl.u_model = gl.glGetUniformLocation(gl.program, "u_model");
	gl.u_nrm = gl.glGetUniformLocation(gl.program, "u_nrm");
	gl.u_tint = gl.glGetUniformLocation(gl.program, "u_tint");
	gl.u_haslight = gl.glGetUniformLocation(gl.program, "u_haslight");
	gl.u_light = gl.glGetUniformLocation(gl.program, "u_light");

	const unsigned char *const version = gl.glGetString(GL_VERSION);
	const unsigned char *const renderer = gl.glGetString(GL_RENDERER);
	osd_printf_verbose("scene3d: GL backend active, GL_VERSION=%s GL_RENDERER=%s\n",
			version ? reinterpret_cast<const char *>(version) : "?",
			renderer ? reinterpret_cast<const char *>(renderer) : "?");
	return true;
#else
	return false;
#endif
}

bool scene3d_renderer::gl_render(bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	bool ok = false;
	m_gl->exec([&] () { ok = gl_draw(*m_gl, bitmap, cliprect); });
	if (!ok)
		m_gl.reset();
	return ok;
}

bool scene3d_renderer::gl_draw(scene3d_gl &gl, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	const int width = bitmap.width();
	const int height = bitmap.height();

	if (!gl.fbo || gl.fbo_width != width || gl.fbo_height != height)
	{
		if (!gl.fbo)
		{
			gl.glGenFramebuffers(1, &gl.fbo);
			gl.glGenRenderbuffers(1, &gl.colour_rb);
			gl.glGenRenderbuffers(1, &gl.depth_rb);
		}
		gl.glBindFramebuffer(GL_FRAMEBUFFER, gl.fbo);
		gl.glBindRenderbuffer(GL_RENDERBUFFER, gl.colour_rb);
		gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
		gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, gl.colour_rb);
		gl.glBindRenderbuffer(GL_RENDERBUFFER, gl.depth_rb);
		gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
		gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, gl.depth_rb);
		if (gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			osd_printf_verbose("scene3d: GL framebuffer incomplete, falling back to software\n");
			return false;
		}
		gl.fbo_width = width;
		gl.fbo_height = height;
	}
	gl.glBindFramebuffer(GL_FRAMEBUFFER, gl.fbo);
	gl.glViewport(0, 0, width, height);

	float proj[16] = { };
	const float near_z = NEAR_PLANE, far_z = 100000.0f;
	proj[0] = 2.0f * m_focal / float(width);
	proj[5] = 2.0f * m_focal / float(height);
	proj[10] = (far_z + near_z) / (far_z - near_z);
	proj[11] = -2.0f * far_z * near_z / (far_z - near_z);
	proj[14] = 1.0f;

	float view12[12];
	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 4; c++)
			view12[r * 4 + c] = m_view[r][c];
	float view[16];
	mat4_from_34(view12, view);

	gl.glUseProgram(gl.program);
	gl.glClearColor(BACKGROUND.r() / 255.0f, BACKGROUND.g() / 255.0f, BACKGROUND.b() / 255.0f, 1.0f);
	gl.glEnable(GL_DEPTH_TEST);
	gl.glEnable(GL_CULL_FACE);
	gl.glCullFace(GL_BACK);
	gl.glFrontFace(GL_CCW);
	gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	for (const node &n : m_nodes)
	{
		if (n.mesh < 0)
			continue;

		auto found = gl.mesh_cache.find(n.mesh);
		if (found == gl.mesh_cache.end())
		{
			const mesh &me = m_meshes[n.mesh];
			std::vector<float> data;
			data.reserve(size_t(me.count) * 3 * 12);
			const u8 *tri = m_tris + size_t(me.first) * TRI_BYTES;
			for (u32 j = 0; j < me.count; j++, tri += TRI_BYTES)
			{
				const float cr = tri[0] / 255.0f, cg = tri[1] / 255.0f, cb = tri[2] / 255.0f;
				vec3 q[3];
				for (int k = 0; k < 3; k++)
					q[k] = { float(rd16(tri + 4 + k * 6)), float(rd16(tri + 6 + k * 6)), float(rd16(tri + 8 + k * 6)) };
				const vec3 e0 = q[1] - q[0], e1 = q[2] - q[0];
				const vec3 nr{ e0.y * e1.z - e0.z * e1.y, e0.z * e1.x - e0.x * e1.z, e0.x * e1.y - e0.y * e1.x };
				const vec3 cen = (q[0] + q[1] + q[2]) * (1.0f / 3.0f);
				for (int k = 0; k < 3; k++)
				{
					const float vtx[12] = { q[k].x, q[k].y, q[k].z, nr.x, nr.y, nr.z,
							cen.x, cen.y, cen.z, cr, cg, cb };
					data.insert(data.end(), vtx, vtx + 12);
				}
			}
			gl_uint vbo = 0;
			gl.glGenBuffers(1, &vbo);
			gl.glBindBuffer(GL_ARRAY_BUFFER, vbo);
			gl.glBufferData(GL_ARRAY_BUFFER, gl_sizeiptr(data.size() * sizeof(float)), data.data(), GL_STATIC_DRAW);
			found = gl.mesh_cache.emplace(n.mesh, std::make_pair(vbo, gl_sizei(me.count * 3))).first;
		}
		if (!found->second.second)
			continue;

		const mesh &me = m_meshes[n.mesh];
		float dq[12], model34[16];
		mat_identity(dq);
		dq[0] = me.scale.x;  dq[3]  = me.origin.x;
		dq[5] = me.scale.y;  dq[7]  = me.origin.y;
		dq[10] = me.scale.z; dq[11] = me.origin.z;
		float model12[12];
		mat_mul(n.world, dq, model12);
		mat4_from_34(model12, model34);

		float mv[16], mvp[16], nrm[9];
		mat4_mul(view, model34, mv);
		mat4_mul(proj, mv, mvp);
		normal_matrix(model12, nrm);

		gl.glBindBuffer(GL_ARRAY_BUFFER, found->second.first);
		for (gl_uint a = 0; a < 4; a++)
		{
			gl.glEnableVertexAttribArray(a);
			gl.glVertexAttribPointer(a, 3, GL_FLOAT, 0, 48, reinterpret_cast<const void *>(std::uintptr_t(a) * 12));
		}
		gl.glUniformMatrix4fv(gl.u_mvp, 1, 1, mvp);
		gl.glUniformMatrix4fv(gl.u_model, 1, 1, model34);
		gl.glUniformMatrix3fv(gl.u_nrm, 1, 1, nrm);
		gl.glUniform3f(gl.u_tint, n.tint.x, n.tint.y, n.tint.z);
		gl.glUniform1i(gl.u_haslight, m_light >= 0 ? 1 : 0);
		gl.glUniform3f(gl.u_light, m_light_world.x, m_light_world.y, m_light_world.z);
		gl.glDrawArrays(GL_TRIANGLES, 0, found->second.second);
	}

	gl.glFinish();
	gl.pixels.resize(size_t(width) * height * 4);
	gl.glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, gl.pixels.data());

	for (int y = cliprect.top(); y <= cliprect.bottom(); y++)
	{
		const uint8_t *src = &gl.pixels[size_t(height - 1 - y) * width * 4];
		u32 *dst = &bitmap.pix(y, 0);
		for (int x = cliprect.left(); x <= cliprect.right(); x++)
		{
			const uint8_t *p = src + size_t(x) * 4;
			dst[x] = (u32(p[0]) << 16) | (u32(p[1]) << 8) | u32(p[2]);
		}
	}
	return true;
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

	if (gl_available() && gl_render(bitmap, cliprect))
	{
		m_dirty = false;
		m_have_frame = true;
		m_frame_live = true;
		return 0;
	}

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
