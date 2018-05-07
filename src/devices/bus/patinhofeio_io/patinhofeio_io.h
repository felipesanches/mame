#ifndef MAME_BUS_PATINHO_FEIO_IO_H
#define MAME_BUS_PATINHO_FEIO_IO_H

#pragma once

#define MCFG_PATINHO_FEIO_IO_PORT_ADD(_tag, _slot_intf, _def_slot) \
	MCFG_DEVICE_ADD(_tag, PATINHO_FEIO_IO_PORT, 0) \
	MCFG_DEVICE_SLOT_INTERFACE(_slot_intf, _def_slot, false)

//Aparentemente define cada um dos pinos de io, n entendi direitinho como funciona ainda...
//E não corri atras ainda pra ver a definição da interface de entrada e saída do patinho, então também n to ligado em quantos pinos tem
//nem a função de cada um, isso vai ficar para depois, por enquanto, definindo um dispositivo de IO com um só pino
#define MCFG_PATINHO_FEIO_IO_PIN0_HANDLER(_devcb) \
	devcb = &downcast<patinho_feio_io_port_device &>(*device).set_pin0_handler(DEVCB_##_devcb);

//No codigo do centronics tem essa linha, que não aparece em nenhum dos dois outros (rs232 e hp80_io), pq?
//DECLARE_DEVICE_TYPE(CENTRONICS, centronics_device)
//Seria o caso de usar algo tipo
DECLARE_DEVICE_TYPE(PATINHO_FEIO_IO, patinho_feio_io_device);
//Pq logo após já tem a declaração do device patinho_feio_io_device, então sei la pq essa linha ta la

class device_patinho_feio_io_port_interface;

class patinho_feio_io_device : public device_t,
	public device_slot_interface
{
	friend class device_patinho_feio_io_port_interface;

public:
	patinho_feio_io_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	//Não faço a MINIMA ideia do que isso faz, mas é padrão nos 3 codigos em que eu to me inspirando, acredito que seja essencial
	template <class Object> devcb_base &set_pin0_handler(Object &&cb) { return m_pin0_handler.set_callback(std::forward<Object>(cb)); }

	//Padrão em todos, acredito ser o que declara as funções que permitem escrever na linha?, faz sentido?
	DECLARE_WRITE_LINE_MEMBER( write_data0 );


protected:
	virtual void device_config_complete() override;
	virtual void device_start() override;

	devcb_write_line m_pin0_handler;

private:
	device_patinho_feio_io_port_interface *m_dev;
};


//Declaração da classe de interface de IO, n entendo pq tem uma diferença entre io_device, e io_interface, 
//tecnicamente eles n deveriam fazer a mesma coisa?
//Pelo menos na centronics, o io_device só tem uma declaração de write_line_member para WRITE para cada pino, enquanto
//no io_interface, tem uma declaração de write_line_member para input e output pra cada um, mas pq existe a declaração de write,
//no io_device, se só tivesse as declarações de input e output no io_interface, e o io_device tivesse uma interface,
//(O que ele tem, logo ali em cima da pra ver que *m_dev É uma io_interface), faria sentido, mas assim n faz muito
//sentido na minha cabeça

//Além do fato de input e output serem ambos DECLARE_****WRITE****, o que faz menos sentido ainda.
class device_patinho_feio_io_interface : public device_slot_card_interface
{
	friend class patinho_feio_io_device;

public:
	virtual ~device_patinho_feio_io_interface();

	DECLARE_WRITE_LINE_MEMBER( output_pin0 ) { m_slot->m_pin0_handler(state); }

protected:
	device_patinho_feio_io_interface(const machine_config &mconfig, device_t &device);

	virtual DECLARE_WRITE_LINE_MEMBER( input_pin0 ) { }

	device_patinho_feio_io *m_slot;
};

void patinho_feio_io_devices(device_slot_interface &device);