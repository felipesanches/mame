// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/***************************************************************************

    Metamaquina 2 3D viewport: a software rasteriser that draws the machine's
    packed CAD geometry into an ordinary MAME screen bitmap.

***************************************************************************/

#include "emu.h"
#include "render.h"
#include "mm2_viewport.h"

#include "screen.h"

#include <algorithm>
#include <cmath>
#include <cstring>


//**************************************************************************
//  CONSTANTS
//**************************************************************************

/*
    Firmware coordinates against the packed asset's design coordinates:

      firmware X 0..200  ->  design x  -100 .. +100
      firmware Y 0..200  ->  design y  +100 .. -100    the bed moves, so the
                                                       sign inverts
      firmware Z 0..150  ->  design z, not a length but the angle of the two M8
                             screws that lift the beam: -288 degrees per
                             millimetre, from the 1.25 mm lead of a
                             right-handed M8 thread
*/
namespace {

constexpr float BUILD_X = 200.0f;
constexpr float BUILD_Y = 200.0f;

// the top of the glass, which is the datum firmware Z counts from
constexpr float BUILD_SURFACE_Z = 103.1f;

// the machine stands 20 mm forward of the centre of its own frame
constexpr float STAGE_OFFSET = -20.0f;

constexpr float DEGREES_PER_MM = -288.0f;

constexpr rgb_t COL_FILAMENT(0xe0, 0x50, 0x30);

constexpr float PI_F = 3.14159265358979f;

float clampf(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }

// the packed asset is little-endian
uint16_t rd16(const uint8_t *p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t rd32(const uint8_t *p)
{
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
float rdf(const uint8_t *p)
{
	const uint32_t bits = rd32(p);
	float out;
	std::memcpy(&out, &bits, sizeof(out));
	return out;
}

// 3x4 row-major matrix helpers
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

void mat_translate(float m[12], float x, float y, float z)
{
	mat_identity(m);
	m[3] = x;
	m[7] = y;
	m[11] = z;
}

void mat_rotate(float m[12], float degrees, const float axis[3])
{
	float x = axis[0], y = axis[1], z = axis[2];
	const float n = std::sqrt(x * x + y * y + z * z);
	if (n > 1e-9f) { x /= n; y /= n; z /= n; }
	const float a = degrees * PI_F / 180.0f;
	const float c = std::cos(a), sn = std::sin(a), C = 1.0f - c;
	m[0] = x * x * C + c;     m[1] = x * y * C - z * sn; m[2]  = x * z * C + y * sn; m[3]  = 0.0f;
	m[4] = y * x * C + z * sn; m[5] = y * y * C + c;     m[6]  = y * z * C - x * sn; m[7]  = 0.0f;
	m[8] = z * x * C - y * sn; m[9] = z * y * C + x * sn; m[10] = z * z * C + c;     m[11] = 0.0f;
}

} // anonymous namespace


//**************************************************************************
//  DEVICE
//**************************************************************************

DEFINE_DEVICE_TYPE(MM2_VIEWPORT, mm2_viewport_device, "mm2_viewport", "Metamaquina 2 3D viewport")

mm2_viewport_device::mm2_viewport_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, MM2_VIEWPORT, tag, owner, clock)
	, m_screen(*this, "screen")
	, m_mesh_region(*this, finder_base::DUMMY_TAG)
{
}

void mm2_viewport_device::device_add_mconfig(machine_config &config)
{
	SCREEN(config, m_screen, SCREEN_TYPE_LCD);
	m_screen->set_refresh_hz(20);
	m_screen->set_size(800, 600);
	m_screen->set_visarea_full();
	m_screen->set_screen_update(FUNC(mm2_viewport_device::screen_update));
}

// Packed asset: a header, a table of rigid bodies, then the triangles.  Vertices
// are 16-bit indices into each body's own bounding box, so dequantisation is a
// scale and an offset that folds into the body's matrix.
bool mm2_viewport_device::load_mesh()
{
	const uint8_t *const base = m_mesh_region->base();
	const size_t length = m_mesh_region->bytes();
	if (length < 16 || std::memcmp(base, "MM2M", 4))
	{
		logerror("%s: mesh region is not a packed Metamaquina 2 asset\n", tag());
		return false;
	}
	if (rd32(base + 4) != 1)
	{
		logerror("%s: mesh asset version %u, expected 1\n", tag(), rd32(base + 4));
		return false;
	}

	const uint32_t body_count = rd32(base + 8);
	m_tri_count = rd32(base + 12);

	// post + mid + two driver slots + origin/scale/centre + range + detail
	static constexpr size_t BODY_BYTES = 12 * 4 + 12 * 4 + 2 * (4 + 3 * 4 + 12 * 4)
			+ 3 * 4 + 3 * 4 + 3 * 4 + 4 + 4 + 4;
	static constexpr size_t TRI_BYTES = 4 + 9 * 2;

	if (length < 16 + body_count * BODY_BYTES + size_t(m_tri_count) * TRI_BYTES)
	{
		logerror("%s: mesh asset is short: %u bodies and %u triangles need more than %u bytes\n",
				tag(), body_count, m_tri_count, uint32_t(length));
		return false;
	}

	m_bodies.clear();
	m_bodies.reserve(body_count);
	const uint8_t *p = base + 16;
	for (uint32_t i = 0; i < body_count; i++)
	{
		body b;
		for (int j = 0; j < 12; j++) b.post[j] = rdf(p + j * 4);
		p += 48;
		for (int j = 0; j < 12; j++) b.mid[j] = rdf(p + j * 4);
		p += 48;
		for (int slot = 0; slot < 2; slot++)
		{
			b.op[slot] = *p;
			p += 4;
			for (int j = 0; j < 3; j++) b.axis[slot][j] = rdf(p + j * 4);
			p += 12;
			for (int r = 0; r < 3; r++)
				for (int c = 0; c < 4; c++)
					b.coef[slot][r][c] = rdf(p + (r * 4 + c) * 4);
			p += 48;
		}
		b.origin = { rdf(p), rdf(p + 4), rdf(p + 8) };            p += 12;
		b.scale  = { rdf(p), rdf(p + 4), rdf(p + 8) };            p += 12;
		b.centre = { rdf(p), rdf(p + 4), rdf(p + 8) };            p += 12;
		b.first = rd32(p);                                        p += 4;
		b.count = rd32(p);                                        p += 4;
		b.detail = *p;                                            p += 4;

		// which of the three machine axes reach this body: a non-zero coefficient
		// on one of them is what makes the body move with it
		b.uses = 0;
		for (int slot = 0; slot < 2; slot++)
		{
			if (b.op[slot] == OP_NONE)
				continue;
			const int rows = (b.op[slot] == OP_ROTATE) ? 1 : 3;
			for (int r = 0; r < rows; r++)
				for (int c = 0; c < 3; c++)
					if (b.coef[slot][r][c] != 0.0f)
						b.uses |= 1 << c;
		}

		m_bodies.push_back(b);
	}
	m_tris = p;

	logerror("%s: %u bodies, %u triangles\n", tag(), body_count, m_tri_count);
	return true;
}

// The transform for one body at the machine's current pose: the packer fitted
// every motion into affine coefficients over the design's three drivers.
void mm2_viewport_device::body_matrix(const body &b, float out[12]) const
{
	// the design's drivers, from the firmware's coordinates
	const float drv[3] = {
			float(m_x) - BUILD_X * 0.5f,
			BUILD_Y * 0.5f - float(m_y),
			float(m_z) * DEGREES_PER_MM };

	float m[12];
	std::memcpy(m, b.post, sizeof(m));

	for (int slot = 0; slot < 2; slot++)
	{
		if (slot == 1)
			mat_mul(m, b.mid, m);

		if (b.op[slot] == OP_TRANSLATE)
		{
			float t[12];
			float v[3];
			for (int r = 0; r < 3; r++)
			{
				v[r] = b.coef[slot][r][3];
				for (int c = 0; c < 3; c++)
					v[r] += b.coef[slot][r][c] * drv[c];
			}
			mat_translate(t, v[0], v[1], v[2]);
			mat_mul(m, t, m);
		}
		else if (b.op[slot] == OP_ROTATE)
		{
			float angle = b.coef[slot][0][3];
			for (int c = 0; c < 3; c++)
				angle += b.coef[slot][0][c] * drv[c];
			float r[12];
			mat_rotate(r, angle, b.axis[slot]);
			mat_mul(m, r, m);
		}
	}

	// fold the dequantisation in, so a vertex is just the raw 16-bit triple
	float dq[12];
	mat_identity(dq);
	dq[0] = b.scale.x;  dq[3]  = b.origin.x;
	dq[5] = b.scale.y;  dq[7]  = b.origin.y;
	dq[10] = b.scale.z; dq[11] = b.origin.z;
	mat_mul(m, dq, out);
}

void mm2_viewport_device::device_start()
{
	if (!load_mesh())
		logerror("%s: nothing to draw\n", tag());

	save_item(NAME(m_x));
	save_item(NAME(m_y));
	save_item(NAME(m_z));
	save_item(NAME(m_e));
	save_item(NAME(m_temp));
	save_item(NAME(m_fan));
	save_item(NAME(m_motors));
	save_item(NAME(m_endstop_z_min));
	save_item(NAME(m_endstop_z_max));
	save_item(NAME(m_endstop_y_min));
	save_item(NAME(m_endstop_y_max));
	save_item(NAME(m_yaw));
	save_item(NAME(m_pitch));
	save_item(NAME(m_distance));
	save_item(NAME(m_show_deposit));
	save_item(NAME(m_full_threads));
}

void mm2_viewport_device::device_reset()
{
	reset_view();
	clear_deposit();
	m_cache_valid = false;
	m_have_frame = false;
}


//**************************************************************************
//  MACHINE STATE
//**************************************************************************

void mm2_viewport_device::set_position(double x_mm, double y_mm, double z_mm)
{
	m_x = x_mm;
	m_y = y_mm;
	m_z = z_mm;
}

// Called from the driver's 500 Hz view tick, so a bead is a sample of where the
// nozzle was, not a G-code segment: a sample carrying on in the same direction
// as the last extends it instead of adding to the list.  Beads are kept in
// firmware coordinates, because on this printer the bed is what moves in Y.
void mm2_viewport_device::set_extrusion(double e_mm)
{
	// a bead is worth recording once the nozzle has moved this far, squared
	static constexpr float MIN_TRAVEL2 = 0.0004f;   // 0.02 mm
	// cos of the angle within which a sample is treated as carrying straight on
	static constexpr float COLLINEAR = 0.99995f;    // about half a degree

	const vec3 nozzle{ float(m_x), float(m_y), float(m_z) };
	const bool extruding = (e_mm - m_e) > 0.0001;
	m_e = e_mm;

	if (!m_have_last_nozzle || !extruding)
	{
		// travelling, not printing: carry the anchor along, lay nothing
		m_last_nozzle = nozzle;
		m_have_last_nozzle = true;
		return;
	}

	const vec3 d = nozzle - m_last_nozzle;
	const float travelled = d.x * d.x + d.y * d.y + d.z * d.z;
	if (travelled <= MIN_TRAVEL2)
	{
		// too small to record; the anchor STAYS, so that slow printing accumulates
		// into a bead instead of being thrown away a tick at a time
		return;
	}

	bool merged = false;
	if (!m_deposit.empty())
	{
		bead &last = m_deposit.back();
		const bool joins = (last.to.x == m_last_nozzle.x)
				&& (last.to.y == m_last_nozzle.y) && (last.to.z == m_last_nozzle.z);
		if (joins)
		{
			const vec3 before = last.to - last.from;
			const float was = std::sqrt(before.x * before.x + before.y * before.y
									   + before.z * before.z);
			const float now = std::sqrt(travelled);
			if (was > 0.0f)
			{
				const float along = (before.x * d.x + before.y * d.y + before.z * d.z)
						/ (was * now);
				if (along > COLLINEAR)
				{
					last.to = nozzle;
					merged = true;
				}
			}
		}
	}

	if (!merged)
	{
		if (m_deposit.size() < DEPOSIT_LIMIT)
		{
			m_deposit.push_back({ m_last_nozzle, nozzle });
		}
		else if (!m_deposit_full)
		{
			m_deposit_full = true;
			logerror("deposit limit of %u beads reached; the rest of this print "
					 "will not be drawn\n", unsigned(DEPOSIT_LIMIT));
		}
	}

	// the bounding box is maintained here rather than swept every frame, so that
	// framing the part view stays O(1) as the print grows
	if (m_deposit.size() == 1 && !merged)
	{
		m_deposit_lo = m_deposit_hi = m_deposit.front().from;
	}
	for (const vec3 &p : { m_last_nozzle, nozzle })
	{
		m_deposit_lo.x = std::min(m_deposit_lo.x, p.x);
		m_deposit_lo.y = std::min(m_deposit_lo.y, p.y);
		m_deposit_lo.z = std::min(m_deposit_lo.z, p.z);
		m_deposit_hi.x = std::max(m_deposit_hi.x, p.x);
		m_deposit_hi.y = std::max(m_deposit_hi.y, p.y);
		m_deposit_hi.z = std::max(m_deposit_hi.z, p.z);
	}

	m_last_nozzle = nozzle;
}


void mm2_viewport_device::set_temperature(int which, double celsius)
{
	if (unsigned(which) < 2)
		m_temp[which] = celsius;
}

void mm2_viewport_device::set_fan(double duty)
{
	m_fan = clampf(float(duty), 0.0f, 1.0f);
}

void mm2_viewport_device::set_motors(bool enabled)
{
	m_motors = enabled;
}

void mm2_viewport_device::set_endstops(bool z_min, bool z_max, bool y_min, bool y_max)
{
	m_endstop_z_min = z_min;
	m_endstop_z_max = z_max;
	m_endstop_y_min = y_min;
	m_endstop_y_max = y_max;
}


//**************************************************************************
//  CAMERA
//**************************************************************************

void mm2_viewport_device::reset_view()
{
	m_part_zoom = 1.0;
	m_have_frame = false;
	// nearly front on, looking in through the machine's open front
	m_yaw = -0.30;
	m_pitch = 0.22;
	m_distance = 980.0;

	// the part view starts higher up, where the layers read
	if (m_part_only)
		m_pitch = 0.42;
}

void mm2_viewport_device::orbit(double d_yaw, double d_pitch)
{
	m_yaw += d_yaw;
	m_pitch = std::clamp(m_pitch + d_pitch, -1.4, 1.4);
}

void mm2_viewport_device::zoom(double factor)
{
	if (m_part_only)
	{
		// the part view fits itself to what has been printed, so the wheel scales
		// that fit rather than setting an absolute distance
		m_part_zoom = std::clamp(m_part_zoom * factor, 0.15, 6.0);
	}
	else
	{
		m_distance = std::clamp(m_distance * factor, 220.0, 3000.0);
	}
	m_have_frame = false;
}

void mm2_viewport_device::set_show_3d(bool on)
{
	if (m_show_3d != on)
	{
		m_show_3d = on;
		m_have_frame = false;
	}
}

void mm2_viewport_device::set_full_threads(bool full)
{
	m_full_threads = full;
}

void mm2_viewport_device::toggle_deposit()
{
	m_show_deposit = !m_show_deposit;
}

void mm2_viewport_device::clear_deposit()
{
	m_deposit.clear();
	m_deposit_full = false;
	m_deposit_lo = vec3{ 0.0f, 0.0f, 0.0f };
	m_deposit_hi = vec3{ 0.0f, 0.0f, 0.0f };
	m_have_last_nozzle = false;
	m_have_frame = false;
}


//**************************************************************************
//  GEOMETRY
//**************************************************************************

// Firmware coordinates to the design's, at the pose the bed is in now.  X and Z
// are a straight offset; Y carries the bed's motion, because a bead of plastic
// is stuck to the glass and the glass is what travels.
mm2_viewport_device::vec3 mm2_viewport_device::on_the_bed(const vec3 &print) const
{
	return { print.x - BUILD_X * 0.5f,
			 STAGE_OFFSET + print.y - float(m_y),
			 BUILD_SURFACE_Z + print.z };
}

void mm2_viewport_device::draw_body(bitmap_rgb32 &bitmap, const rectangle &cliprect, const body &b)
{
	if (b.detail != DETAIL_ALWAYS
			&& b.detail != (m_full_threads ? DETAIL_FULL : DETAIL_SIMPLE))
		return;

	float m[12];
	body_matrix(b, m);

	const uint8_t *tri = m_tris + size_t(b.first) * 22;
	for (uint32_t i = 0; i < b.count; i++, tri += 22)
	{
		const rgb_t colour(tri[0], tri[1], tri[2]);
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


//**************************************************************************
//  RASTERISER
//**************************************************************************

// A perspective camera orbiting the middle of the machine, and a triangle
// rasteriser with a depth buffer.
bool mm2_viewport_device::project(const vec3 &world, float &sx, float &sy, float &sz) const
{
	const float ex = m_view[0][0] * world.x + m_view[0][1] * world.y + m_view[0][2] * world.z + m_view[0][3];
	const float ey = m_view[1][0] * world.x + m_view[1][1] * world.y + m_view[1][2] * world.z + m_view[1][3];
	const float ez = m_view[2][0] * world.x + m_view[2][1] * world.y + m_view[2][2] * world.z + m_view[2][3];

	if (ez < 1.0f)
		return false;

	const float focal = float(m_height) * 1.35f;
	sx = float(m_width) * 0.5f + focal * ex / ez;
	sy = float(m_height) * 0.5f - focal * ey / ez;
	sz = ez;
	return true;
}

void mm2_viewport_device::set_pixel(bitmap_rgb32 &bitmap, const rectangle &cliprect, int x, int y, float z, rgb_t colour)
{
	if (x < cliprect.min_x || x > cliprect.max_x || y < cliprect.min_y || y > cliprect.max_y)
		return;

	float &depth = m_depth[y * m_width + x];
	if (z >= depth)
		return;

	depth = z;
	bitmap.pix(y, x) = colour;
}

void mm2_viewport_device::raster_face(bitmap_rgb32 &bitmap, const rectangle &cliprect, const vec3 v[3], rgb_t colour)
{
	float px[3], py[3], pz[3];
	for (int i = 0; i < 3; i++)
	{
		if (!project(v[i], px[i], py[i], pz[i]))
			return;
	}

	// Drop back faces.  An STL facet winds counter-clockwise seen from outside the
	// solid, and the y flip in project() reverses the handedness, so a front face
	// has NEGATIVE signed area.
	const float area = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
	if (area >= 0.0f)
		return;

	int min_x = std::max(cliprect.min_x, int(std::floor(std::min({ px[0], px[1], px[2] }))));
	int max_x = std::min(cliprect.max_x, int(std::ceil (std::max({ px[0], px[1], px[2] }))));
	int min_y = std::max(cliprect.min_y, int(std::floor(std::min({ py[0], py[1], py[2] }))));
	int max_y = std::min(cliprect.max_y, int(std::ceil (std::max({ py[0], py[1], py[2] }))));
	if (min_x > max_x || min_y > max_y)
		return;

	// flat shading from a fixed light over the viewer's left shoulder
	const vec3 e0 = v[1] - v[0];
	const vec3 e1 = v[2] - v[0];
	vec3 n{ e0.y * e1.z - e0.z * e1.y, e0.z * e1.x - e0.x * e1.z, e0.x * e1.y - e0.y * e1.x };
	const float nl = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
	if (nl > 0.0001f)
		n = n * (1.0f / nl);
	const float lambert = clampf(0.35f + 0.65f * std::fabs(-0.4f * n.x - 0.5f * n.y + 0.77f * n.z), 0.0f, 1.0f);
	const rgb_t shade(uint8_t(colour.r() * lambert), uint8_t(colour.g() * lambert), uint8_t(colour.b() * lambert));

	// a triangle covering about a pixel is most of this model -- a bolt head, a
	// washer -- and scanning a box for it costs more than drawing it
	if (min_x == max_x && min_y == max_y)
	{
		set_pixel(bitmap, cliprect, min_x, min_y, (pz[0] + pz[1] + pz[2]) * (1.0f / 3.0f), shade);
		return;
	}

	// incremental edge functions: the barycentric weights are affine in the pixel
	// coordinates, so stepping across a scanline is an addition
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
		uint32_t *const line = &bitmap.pix(y, 0);
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

// One bead of plastic, drawn as a depth-tested line.
void mm2_viewport_device::raster_bead(bitmap_rgb32 &bitmap, const rectangle &cliprect, const bead &b, rgb_t colour)
{
	float ax, ay, az, bx, by, bz;
	if (!project(b.from, ax, ay, az) || !project(b.to, bx, by, bz))
		return;

	const int steps = std::max(1, int(std::max(std::fabs(bx - ax), std::fabs(by - ay))));
	if (steps > 4000)
		return;

	for (int i = 0; i <= steps; i++)
	{
		const float t = float(i) / float(steps);
		const int x = int(ax + (bx - ax) * t + 0.5f);
		const int y = int(ay + (by - ay) * t + 0.5f);
		const float z = az + (bz - az) * t;
		// a bead is a couple of tenths of a millimetre wide, so give it two pixels
		set_pixel(bitmap, cliprect, x, y, z - 0.5f, colour);
		set_pixel(bitmap, cliprect, x, y + 1, z - 0.5f, colour);
	}
}

// The printed object on its own: beads drawn in the coordinates they were laid
// down in, so the object stands still instead of riding the bed as Y moves, and
// the camera fitted to their bounding box every frame.
uint32_t mm2_viewport_device::draw_part(bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	const bool same = m_have_frame
			&& (m_cache_yaw == m_yaw) && (m_cache_pitch == m_pitch)
			&& (m_cache_distance == m_part_zoom)
			&& (m_cache_width == m_width) && (m_cache_height == m_height)
			&& (m_frame_beads == m_deposit.size());
	if (same)
		return UPDATE_HAS_NOT_CHANGED;

	m_depth.assign(size_t(m_width) * m_height, 1e30f);
	bitmap.fill(rgb_t(0x14, 0x16, 0x1b), cliprect);

	// with nothing printed yet, frame the build area rather than a void
	vec3 lo{ 0.0f, 0.0f, 0.0f }, hi{ BUILD_X, BUILD_Y, 1.0f };
	if (!m_deposit.empty())
	{
		lo = m_deposit_lo;
		hi = m_deposit_hi;
	}

	const vec3 centre{ (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f };
	const vec3 span{ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z };
	const float radius = std::max(8.0f,
			0.5f * std::sqrt(span.x * span.x + span.y * span.y + span.z * span.z));

	// the projection's focal length is 1.35 screen heights, so a sphere of
	// this radius fills the frame at 2.7 radii; 2.8 leaves a little margin
	const double distance = 2.8 * radius * m_part_zoom;

	const float cy = std::cos(float(m_yaw)), sy = std::sin(float(m_yaw));
	const float cp = std::cos(float(m_pitch)), sp = std::sin(float(m_pitch));
	const vec3 fwd{ -cp * sy, cp * cy, -sp };
	const vec3 right{ cy, sy, 0.0f };
	const vec3 up{ -sp * sy, sp * cy, cp };
	const vec3 eye{ centre.x - fwd.x * float(distance),
					centre.y - fwd.y * float(distance),
					centre.z - fwd.z * float(distance) };

	m_view[0][0] = right.x; m_view[0][1] = right.y; m_view[0][2] = right.z;
	m_view[1][0] = up.x;    m_view[1][1] = up.y;    m_view[1][2] = up.z;
	m_view[2][0] = fwd.x;   m_view[2][1] = fwd.y;   m_view[2][2] = fwd.z;
	m_view[0][3] = -(right.x * eye.x + right.y * eye.y + right.z * eye.z);
	m_view[1][3] = -(up.x * eye.x + up.y * eye.y + up.z * eye.z);
	m_view[2][3] = -(fwd.x * eye.x + fwd.y * eye.y + fwd.z * eye.z);

	// the outline of the build surface, so a small part is somewhere
	static constexpr rgb_t COL_PLATE(0x3a, 0x3f, 0x4a);
	const vec3 corner[4] = { { 0.0f, 0.0f, 0.0f }, { BUILD_X, 0.0f, 0.0f },
							 { BUILD_X, BUILD_Y, 0.0f }, { 0.0f, BUILD_Y, 0.0f } };
	for (int i = 0; i < 4; i++)
		raster_bead(bitmap, cliprect, { corner[i], corner[(i + 1) & 3] }, COL_PLATE);

	const float height = std::max(hi.z - lo.z, 0.2f);
	for (const bead &b : m_deposit)
	{
		// colour runs with height, so the layers separate
		const float t = std::clamp(((b.from.z + b.to.z) * 0.5f - lo.z) / height, 0.0f, 1.0f);
		const rgb_t colour(uint8_t(0x90 + 0x6f * t), uint8_t(0x28 + 0x62 * t), uint8_t(0x20 + 0x38 * t));
		raster_bead(bitmap, cliprect, b, colour);
	}

	m_cache_yaw = m_yaw;
	m_cache_pitch = m_pitch;
	m_cache_distance = m_part_zoom;
	m_cache_width = m_width;
	m_cache_height = m_height;
	m_frame_beads = m_deposit.size();
	m_have_frame = true;
	return 0;
}


uint32_t mm2_viewport_device::screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	m_width = bitmap.width();
	m_height = bitmap.height();

	// A screen nobody is looking at still gets its update called: the video
	// manager walks every screen and only then asks which are live.
	if (!machine().render().is_live(screen))
	{
		// nothing may ever have been drawn into this bitmap, and something may
		// composite it anyway (a snapshot), so give it a ground once
		if (m_have_frame)
			return UPDATE_HAS_NOT_CHANGED;
		bitmap.fill(rgb_t(0x14, 0x16, 0x1b), cliprect);
		m_have_frame = true;
		return 0;
	}

	if (m_part_only)
		return draw_part(bitmap, cliprect);

	if (!m_show_3d)
	{
		if (m_have_frame)
			return UPDATE_HAS_NOT_CHANGED;
		bitmap.fill(rgb_t(0x1a, 0x1c, 0x22), cliprect);
		m_have_frame = true;
		m_cache_valid = false;
		return 0;
	}

	// nothing that reaches this screen has moved, so the frame already in the
	// bitmap is still the right one
	const bool same = m_have_frame && m_cache_valid
			&& (m_cache_yaw == m_yaw) && (m_cache_pitch == m_pitch)
			&& (m_cache_distance == m_distance)
			&& (m_cache_width == m_width) && (m_cache_height == m_height)
			&& (m_cache_full_threads == m_full_threads) && (m_cache_z == m_z)
			&& (m_frame_x == m_x) && (m_frame_y == m_y)
			&& (m_frame_beads == m_deposit.size())
			&& (m_frame_deposit == m_show_deposit);
	if (same)
		return UPDATE_HAS_NOT_CHANGED;

	// orbit the middle of the machine at eye level -- the machine, not the scene:
	// the spool stand beside it would pull the framing off to the right
	const float target_x = 20.0f;
	const float target_y = STAGE_OFFSET;
	const float target_z = 190.0f;

	const float cy = std::cos(float(m_yaw)), sy = std::sin(float(m_yaw));
	const float cp = std::cos(float(m_pitch)), sp = std::sin(float(m_pitch));

	// eye basis: up is right x fwd, so the three stay orthonormal at every pitch
	const vec3 fwd{ -cp * sy, cp * cy, -sp };
	const vec3 right{ cy, sy, 0.0f };
	const vec3 up{ -sp * sy, sp * cy, cp };
	const vec3 eye{ target_x - fwd.x * float(m_distance),
					target_y - fwd.y * float(m_distance),
					target_z - fwd.z * float(m_distance) };

	m_view[0][0] = right.x; m_view[0][1] = right.y; m_view[0][2] = right.z;
	m_view[1][0] = up.x;    m_view[1][1] = up.y;    m_view[1][2] = up.z;
	m_view[2][0] = fwd.x;   m_view[2][1] = fwd.y;   m_view[2][2] = fwd.z;
	m_view[0][3] = -(right.x * eye.x + right.y * eye.y + right.z * eye.z);
	m_view[1][3] = -(up.x * eye.x + up.y * eye.y + up.z * eye.z);
	m_view[2][3] = -(fwd.x * eye.x + fwd.y * eye.y + fwd.z * eye.z);

	// The cache holds only bodies fixed with respect to the machine's own axes,
	// so nothing the firmware does can invalidate it.
	const bool stale = !m_cache_valid
			|| (m_cache_yaw != m_yaw) || (m_cache_pitch != m_pitch)
			|| (m_cache_distance != m_distance)
			|| (m_cache_width != m_width) || (m_cache_height != m_height)
			|| (m_cache_full_threads != m_full_threads)
			|| (m_cache_z != m_z);

	if (stale)
	{
		m_cache.allocate(m_width, m_height);
		m_cache_depth.assign(size_t(m_width) * m_height, 1e30f);
		m_cache.fill(rgb_t(0x1a, 0x1c, 0x22), cliprect);

		// draw into the cache by lending it the working depth buffer
		m_depth.swap(m_cache_depth);
		for (const body &b : m_bodies)
			if (!(b.uses & (USES_X | USES_Y)))
				draw_body(m_cache, cliprect, b);
		m_depth.swap(m_cache_depth);

		m_cache_yaw = m_yaw;
		m_cache_pitch = m_pitch;
		m_cache_distance = m_distance;
		m_cache_width = m_width;
		m_cache_height = m_height;
		m_cache_full_threads = m_full_threads;
		m_cache_z = m_z;
		m_cache_valid = true;
	}

	// start this frame from the machine that never moves
	m_depth = m_cache_depth;
	for (int y = cliprect.top(); y <= cliprect.bottom(); y++)
		std::copy(&m_cache.pix(y, cliprect.left()), &m_cache.pix(y, cliprect.right()) + 1,
				  &bitmap.pix(y, cliprect.left()));

	for (const body &b : m_bodies)
		if (b.uses & (USES_X | USES_Y))
			draw_body(bitmap, cliprect, b);

	// the deposited plastic sits on the bed, so it moves with Y and cannot go in
	// the cache
	if (m_show_deposit)
	{
		for (const bead &b : m_deposit)
			raster_bead(bitmap, cliprect, { on_the_bed(b.from), on_the_bed(b.to) }, COL_FILAMENT);
	}

	m_frame_x = m_x;
	m_frame_y = m_y;
	m_frame_z = m_z;
	m_frame_beads = m_deposit.size();
	m_frame_deposit = m_show_deposit;
	m_have_frame = true;

	return 0;
}
