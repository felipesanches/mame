#include "emu.h"
#include "patinhofeio_io.h"

DEFINE_DEVICE_TYPE(PATINHO_FEIO_IO, patinho_feio_io_device, "patinho_feio_io", "Patinho Feio IO Port")

patinho_feio_io_device::patinho_feio_io_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, PATINHO_FEIO_IO, tag, owner, clock),
	device_slot_interface(mconfig, *this),

	m_pin0_handler(*this),
	
	m_dev(nullptr)
{
}

void patinho_feio_io_device::device_config_complete()
{
	m_dev = dynamic_cast<patinho_feio_io_device *>(get_card_device());
}

void patinho_feio_io_device::device_start()
{
	m_pin0_handler.resolve_safe();
	
	// pull up
	m_pin0_handler(1);
}

WRITE_LINE_MEMBER( patinho_feio_io_device::write_pin0 ) { if (m_dev) m_dev->input_pin0(state); }

device_patinho_feio_io_port_interface::device_patinho_feio_io_port_interface(const machine_config &mconfig, device_t &device)
	: device_slot_card_interface(mconfig, device)
{
	m_slot = dynamic_cast<patinho_feio_io_device *>(device.owner());
}

device_patinho_feio_io_port_interface::~device_patinho_feio_io_port_interface()
{
}

/*
A partir daqui o codigo do centronics define cada device que seria possivelmente plugado nessa porta, seria o caso de 
colocar aqui a teletype, decwriter, synth do guido, etc

#include "comxpl80.h"
#include "epson_ex800.h"
#include "epson_lx800.h"
#include "epson_lx810l.h"
#include "nec_p72.h"
#include "printer.h"
#include "covox.h"

SLOT_INTERFACE_START(centronics_devices)
	SLOT_INTERFACE("pl80", COMX_PL80)
	SLOT_INTERFACE("ex800", EPSON_EX800)
	SLOT_INTERFACE("lx800", EPSON_LX800)
	SLOT_INTERFACE("lx810l", EPSON_LX810L)
	SLOT_INTERFACE("ap2000", EPSON_AP2000)
	SLOT_INTERFACE("p72", NEC_P72)
	SLOT_INTERFACE("printer", CENTRONICS_PRINTER)
	SLOT_INTERFACE("covox", CENTRONICS_COVOX)
	SLOT_INTERFACE("covox_stereo", CENTRONICS_COVOX_STEREO)
SLOT_INTERFACE_END
*/
