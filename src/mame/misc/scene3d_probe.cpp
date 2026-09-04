// license:BSD-3-Clause
// copyright-holders:Felipe Sanches
// Probe machines for SCREEN_TYPE_3D. No CPU: probe.lua publishes the outputs
// that wackygtr.3dlay binds and saves frames for check_frames.py.
//
//   s3dprobe   the layout as written, with its mesh pack
//   s3dunbnd   the same with one motion bound to a signal nothing publishes
//   s3dbadxml  a scene region that is not XML
//   s3dnoscn   no scene region at all
//   s3dnewufo  newufo.3dlay as written, its meshes stood in by boxes

#include "emu.h"
#include "screen.h"


namespace {

class scene3d_probe_state : public driver_device
{
public:
	scene3d_probe_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
	{ }

	void probe(machine_config &config) ATTR_COLD;
};

void scene3d_probe_state::probe(machine_config &config)
{
	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_3D));
	screen.set_scene_region("scene");
	screen.set_mesh_region("meshes");
	screen.set_size(640, 480);
	screen.set_visarea_full();
	screen.set_refresh_hz(20);
}

INPUT_PORTS_START( probe )
INPUT_PORTS_END

#define WACKYGTR_MESHES \
	ROM_REGION( 0x23760, "meshes", 0 ) \
	ROM_LOAD( "wackygtr_meshes.bin", 0, 0x23760, CRC(a1b24758) SHA1(1e9a4a50d07272bfc8c5f09b9d9404276216353d) )

ROM_START( s3dprobe )
	ROM_REGION( 0x1000, "scene", ROMREGION_ERASE00 )
	ROM_LOAD_OPTIONAL( "wackygtr.3dlay", 0, 0xd69, CRC(d73942f8) SHA1(8036776b877971195158c0043854b5d1fae54279) )
	WACKYGTR_MESHES
ROM_END

ROM_START( s3dunbnd )
	ROM_REGION( 0x1000, "scene", ROMREGION_ERASE00 )
	ROM_LOAD( "wackygtr_unbound.3dlay", 0, 0xd74, CRC(0f2dabfe) SHA1(8dda1dfbae51ad0e677a4b51f143a9978a667a58) )
	WACKYGTR_MESHES
ROM_END

ROM_START( s3dbadxml )
	ROM_REGION( 0x100, "scene", ROMREGION_ERASEFF )
	WACKYGTR_MESHES
ROM_END

ROM_START( s3dnoscn )
	WACKYGTR_MESHES
ROM_END

ROM_START( s3dnewufo )
	ROM_REGION( 0x1000, "scene", ROMREGION_ERASE00 )
	ROM_LOAD( "newufo.3dlay", 0, 0xf76, CRC(1773da4f) SHA1(627a6a0820825c741c64ae7628a2b1f4e9e01bfa) )
	ROM_REGION( 0x530, "meshes", 0 )
	ROM_LOAD( "newufo_meshes.bin", 0, 0x530, CRC(94639e8a) SHA1(b542172ef8f40c3efe233d13af1c405a203249ff) )
ROM_END

} // anonymous namespace


SYST( 2026, s3dprobe,  0,        0, probe, probe, scene3d_probe_state, empty_init, "MAME", "scene3d probe: wackygtr layout",            MACHINE_NO_SOUND_HW )
SYST( 2026, s3dunbnd,  s3dprobe, 0, probe, probe, scene3d_probe_state, empty_init, "MAME", "scene3d probe: unbound motion signal",      MACHINE_NO_SOUND_HW )
SYST( 2026, s3dbadxml, s3dprobe, 0, probe, probe, scene3d_probe_state, empty_init, "MAME", "scene3d probe: scene region is not XML",    MACHINE_NO_SOUND_HW )
SYST( 2026, s3dnoscn,  s3dprobe, 0, probe, probe, scene3d_probe_state, empty_init, "MAME", "scene3d probe: no scene region",            MACHINE_NO_SOUND_HW )
SYST( 2026, s3dnewufo, 0,        0, probe, probe, scene3d_probe_state, empty_init, "MAME", "scene3d probe: newufo layout, box meshes",  MACHINE_NO_SOUND_HW )
