// license:GPL-2.0+
// copyright-holders:Felipe Sanches

#include "emu.h"

#include "cpu/avr8/avr8.h"

#include "machine/a4982.h"
#include "machine/ad5206.h"
#include "machine/nvram.h"
#include "machine/rescap.h"
#include "bus/rs232/rs232.h"

#include <cmath>


namespace {

#define MASTER_CLOCK    16000000

struct mm2_axis
{
	double steps_per_mm;
	double travel_mm;
	double overtravel_mm;
	double direction;
};

class rambo_state : public driver_device
{
public:
	rambo_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_rs232(*this, "rs232")
		, m_stepper(*this, "stepper%u", 0U)
		, m_digipot(*this, "digipot")
		, m_eeprom(*this, "eeprom")
		, m_nvram(*this, "nvram")
		, m_axis_out(*this, "axis_%u_um")
		, m_endstop_out(*this, "endstop_%u")
		, m_temp_out(*this, "temp_%u_c")
		, m_duty_out(*this, "duty_%u")
		, m_motor_out(*this, "motor_%u_on")
	{
	}

	void rambo(machine_config &config);

	enum : int { AXIS_X, AXIS_Y, AXIS_Z, AXIS_E0, AXIS_E1, AXIS_COUNT };

	enum : int { ES_X_MIN, ES_X_MAX, ES_Y_MIN, ES_Y_MAX, ES_Z_MIN, ES_Z_MAX, ES_COUNT };

	enum : int { PWM_HOTEND, PWM_BED, PWM_FAN, PWM_COUNT };

private:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

	void rambo_prg_map(address_map &map) ATTR_COLD;
	void rambo_data_map(address_map &map) ATTR_COLD;

	uint8_t port_a_r();
	uint8_t port_b_r();
	uint8_t port_c_r();
	void port_a_w(uint8_t data);
	void port_c_w(uint8_t data);
	void port_d_w(uint8_t data);
	void port_e_w(uint8_t data);
	void port_g_w(uint8_t data);
	void port_h_w(uint8_t data);
	void port_k_w(uint8_t data);
	void port_l_w(uint8_t data);

	template <int Axis> void step_taken(uint64_t position);
	void update_endstops();
	double axis_mm(int axis) const;

	void pwm_level(int channel, int state);
	TIMER_CALLBACK_MEMBER(thermal_tick);
	static double thermistor_resistance(int channel, double celsius);
	uint16_t thermistor_code(int channel);

	required_device<atmega2560_device> m_maincpu;
	required_device<rs232_port_device> m_rs232;
	required_device_array<a4982_device, AXIS_COUNT> m_stepper;
	required_device<ad5206_device> m_digipot;
	required_memory_region m_eeprom;
	required_device<nvram_device> m_nvram;

	output_finder<AXIS_COUNT> m_axis_out;
	output_finder<ES_COUNT> m_endstop_out;
	output_finder<2> m_temp_out;
	output_finder<PWM_COUNT> m_duty_out;
	output_finder<AXIS_COUNT> m_motor_out;

	int64_t m_position[AXIS_COUNT]{};
	int64_t m_last_translator[AXIS_COUNT]{};
	uint8_t m_endstops = 0;

	uint8_t m_pwm_state[PWM_COUNT]{};
	attotime m_pwm_changed[PWM_COUNT];
	attotime m_pwm_high[PWM_COUNT];

	double m_temperature[2]{};
	double m_adc_residue[2]{};

	emu_timer *m_thermal_timer = nullptr;
};

static constexpr mm2_axis AXES[rambo_state::AXIS_COUNT] = {
	{  100.0, 200.0, 3.0, +1.0 },
	{  100.0, 200.0, 3.0, +1.0 },
	{ 2560.0, 150.0, 2.0, +1.0 },
	{  650.0,   0.0, 0.0, -1.0 },
	{  650.0,   0.0, 0.0, -1.0 }
};

static constexpr double POWER_ON_POSE[3] = { 100.0, 100.0, 130.0 };

void rambo_state::rambo_prg_map(address_map &map)
{
	map(0x00000, 0x3ffff).rom();
}

void rambo_state::rambo_data_map(address_map &map)
{
	map(0x0200, 0x21FF).ram();
}

template <int Axis>
void rambo_state::step_taken(uint64_t position)
{
	const int64_t now = int64_t(position);
	const int64_t delta = now - m_last_translator[Axis];
	m_last_translator[Axis] = now;

	if (!m_stepper[Axis]->outputs_enabled())
		return;

	m_position[Axis] += delta;

	if (AXES[Axis].travel_mm > 0.0)
	{
		const int64_t low = int64_t(-AXES[Axis].overtravel_mm * AXES[Axis].steps_per_mm);
		const int64_t high = int64_t((AXES[Axis].travel_mm + AXES[Axis].overtravel_mm) * AXES[Axis].steps_per_mm);
		m_position[Axis] = std::clamp(m_position[Axis], low, high);
	}

	update_endstops();
}

double rambo_state::axis_mm(int axis) const
{
	return AXES[axis].direction * double(m_position[axis]) / AXES[axis].steps_per_mm;
}

void rambo_state::update_endstops()
{
	static constexpr double TRIP = 0.5;

	uint8_t state = 0;
	for (int axis = AXIS_X; axis <= AXIS_Z; axis++)
	{
		const double mm = axis_mm(axis);
		if (mm <= TRIP)
			state |= 1 << (axis * 2);
		if (mm >= AXES[axis].travel_mm - TRIP)
			state |= 1 << (axis * 2 + 1);
	}

	if (state != m_endstops)
	{
		m_endstops = state;
		for (int i = 0; i < ES_COUNT; i++)
			m_endstop_out[i] = BIT(state, i);
	}

	for (int axis = 0; axis < AXIS_COUNT; axis++)
	{
		m_axis_out[axis] = int32_t(1000.0 * axis_mm(axis));
		m_motor_out[axis] = m_stepper[axis]->outputs_enabled() ? 1 : 0;
	}
}

uint8_t rambo_state::port_a_r()
{
	return (BIT(m_endstops, ES_Y_MAX) << 1) | (BIT(m_endstops, ES_X_MAX) << 2);
}

uint8_t rambo_state::port_b_r()
{
	return (BIT(m_endstops, ES_Z_MIN) << 4) | (BIT(m_endstops, ES_Y_MIN) << 5) | (BIT(m_endstops, ES_X_MIN) << 6);
}

uint8_t rambo_state::port_c_r()
{
	return BIT(m_endstops, ES_Z_MAX) << 7;
}

void rambo_state::port_a_w(uint8_t data)
{
	m_stepper[AXIS_E0]->enable_w(BIT(data, 4));
	m_stepper[AXIS_Z]->enable_w(BIT(data, 5));
	m_stepper[AXIS_Y]->enable_w(BIT(data, 6));
	m_stepper[AXIS_X]->enable_w(BIT(data, 7));
	update_endstops();
}

void rambo_state::port_c_w(uint8_t data)
{
	m_stepper[AXIS_X]->step_w(BIT(data, 0));
	m_stepper[AXIS_Y]->step_w(BIT(data, 1));
	m_stepper[AXIS_Z]->step_w(BIT(data, 2));
	m_stepper[AXIS_E0]->step_w(BIT(data, 3));
	m_stepper[AXIS_E1]->step_w(BIT(data, 4));
}

void rambo_state::port_d_w(uint8_t data)
{
	m_digipot->cs_w(BIT(data, 7));
}

void rambo_state::port_e_w(uint8_t data)
{
	pwm_level(PWM_BED, BIT(data, 5));
}

void rambo_state::port_g_w(uint8_t data)
{
	m_stepper[AXIS_X]->ms2_w(BIT(data, 0));
	m_stepper[AXIS_X]->ms1_w(BIT(data, 1));
	m_stepper[AXIS_Y]->ms2_w(BIT(data, 2));
}

void rambo_state::port_h_w(uint8_t data)
{
	pwm_level(PWM_FAN, BIT(data, 5));
	pwm_level(PWM_HOTEND, BIT(data, 6));
}

void rambo_state::port_k_w(uint8_t data)
{
	m_stepper[AXIS_E1]->ms1_w(BIT(data, 1));
	m_stepper[AXIS_E1]->ms2_w(BIT(data, 2));
	m_stepper[AXIS_E0]->ms1_w(BIT(data, 3));
	m_stepper[AXIS_E0]->ms2_w(BIT(data, 4));
	m_stepper[AXIS_Z]->ms2_w(BIT(data, 5));
	m_stepper[AXIS_Z]->ms1_w(BIT(data, 6));
	m_stepper[AXIS_Y]->ms1_w(BIT(data, 7));
}

void rambo_state::port_l_w(uint8_t data)
{
	m_stepper[AXIS_Y]->dir_w(BIT(data, 0));
	m_stepper[AXIS_X]->dir_w(BIT(data, 1));
	m_stepper[AXIS_Z]->dir_w(BIT(data, 2));
	m_stepper[AXIS_E0]->dir_w(BIT(data, 6));
	m_stepper[AXIS_E1]->dir_w(BIT(data, 7));
}

void rambo_state::pwm_level(int channel, int state)
{
	if (state == m_pwm_state[channel])
		return;

	const attotime now = machine().time();
	if (m_pwm_state[channel])
		m_pwm_high[channel] += now - m_pwm_changed[channel];

	m_pwm_state[channel] = state;
	m_pwm_changed[channel] = now;
}

TIMER_CALLBACK_MEMBER(rambo_state::thermal_tick)
{
	static constexpr double AMBIENT = 25.0;
	static constexpr double INTERVAL = 0.1;   // seconds

	static constexpr double POWER[2]    = {  28.8, 200.0 };   // W
	static constexpr double MASS[2]     = {   5.2, 400.0 };   // J/K
	static constexpr double LOSS[2]     = { 0.045,   1.9 };   // W/K

	const attotime now = machine().time();
	for (int channel = 0; channel < PWM_COUNT; channel++)
	{
		if (m_pwm_state[channel])
		{
			m_pwm_high[channel] += now - m_pwm_changed[channel];
			m_pwm_changed[channel] = now;
		}

		double duty = m_pwm_high[channel].as_double() / INTERVAL;
		duty = std::clamp(duty, 0.0, 1.0);
		m_pwm_high[channel] = attotime::zero;
		m_duty_out[channel] = int32_t(duty * 255.0 + 0.5);

		if (channel == PWM_FAN)
		{
			continue;
		}

		const double dT = (POWER[channel] * duty - LOSS[channel] * (m_temperature[channel] - AMBIENT))
						  * INTERVAL / MASS[channel];
		m_temperature[channel] += dT;
		m_temp_out[channel] = int32_t(m_temperature[channel] * 10.0 + 0.5);
	}
}

double rambo_state::thermistor_resistance(int channel, double celsius)
{
	const double kelvin = celsius + 273.15;

	if (channel == 0)
	{
		static constexpr double A = 7.1577566465e-04;
		static constexpr double B = 2.1755533222e-04;
		static constexpr double C = 8.7241270924e-08;

		const double x = (A - 1.0 / kelvin) / C;
		const double y = std::sqrt((B / (3.0 * C)) * (B / (3.0 * C)) * (B / (3.0 * C))
								   + x * x * 0.25);
		return std::exp(std::cbrt(y - x * 0.5) - std::cbrt(y + x * 0.5));
	}

	static constexpr double BED_R25 = 15000.0;
	static constexpr double BED_BETA = 3528.0;
	return BED_R25 * std::exp(BED_BETA * (1.0 / kelvin - 1.0 / 298.15));
}

uint16_t rambo_state::thermistor_code(int channel)
{
	static constexpr double PULLUP = RES_K(4.7);

	const double celsius = std::clamp(m_temperature[channel], -55.0, 250.0);
	const double rt = thermistor_resistance(channel, celsius);

	const double exact = 1023.0 * RES_VOLTAGE_DIVIDER(PULLUP, rt);

	const double wanted = std::clamp(exact + m_adc_residue[channel], 0.0, 1023.0);
	const double rounded = std::floor(wanted + 0.5);
	m_adc_residue[channel] = std::clamp(wanted - rounded, -1.0, 1.0);

	return uint16_t(rounded);
}

static DEVICE_INPUT_DEFAULTS_START( host_serial )
	DEVICE_INPUT_DEFAULTS( "RS232_RXBAUD", 0xff, RS232_BAUD_115200 )
	DEVICE_INPUT_DEFAULTS( "RS232_TXBAUD", 0xff, RS232_BAUD_115200 )
	DEVICE_INPUT_DEFAULTS( "RS232_DATABITS", 0xff, RS232_DATABITS_8 )
	DEVICE_INPUT_DEFAULTS( "RS232_PARITY", 0xff, RS232_PARITY_NONE )
	DEVICE_INPUT_DEFAULTS( "RS232_STOPBITS", 0xff, RS232_STOPBITS_1 )
DEVICE_INPUT_DEFAULTS_END

void rambo_state::machine_start()
{
	m_axis_out.resolve();
	m_endstop_out.resolve();
	m_temp_out.resolve();
	m_duty_out.resolve();
	m_motor_out.resolve();

	m_nvram->set_base(m_eeprom->base(), m_eeprom->bytes());

	m_thermal_timer = timer_alloc(FUNC(rambo_state::thermal_tick), this);

	save_item(NAME(m_position));
	save_item(NAME(m_last_translator));
	save_item(NAME(m_endstops));
	save_item(NAME(m_pwm_state));
	save_item(NAME(m_pwm_changed));
	save_item(NAME(m_pwm_high));
	save_item(NAME(m_temperature));
}

void rambo_state::machine_reset()
{
	for (int axis = 0; axis < AXIS_COUNT; axis++)
	{
		m_position[axis] = (axis < 3) ? int64_t(POWER_ON_POSE[axis] * AXES[axis].steps_per_mm) : 0;
		m_last_translator[axis] = 0;
	}

	for (int axis = 0; axis < AXIS_COUNT; axis++)
		m_stepper[axis]->enable_w(1);

	m_endstops = 0xff;
	update_endstops();

	for (int channel = 0; channel < PWM_COUNT; channel++)
	{
		m_pwm_state[channel] = 0;
		m_pwm_changed[channel] = machine().time();
		m_pwm_high[channel] = attotime::zero;
	}

	m_temperature[0] = m_temperature[1] = 25.0;

	m_thermal_timer->adjust(attotime::from_msec(100), 0, attotime::from_msec(100));
}

void rambo_state::rambo(machine_config &config)
{
	ATMEGA2560(config, m_maincpu, MASTER_CLOCK);
	m_maincpu->set_addrmap(AS_PROGRAM, &rambo_state::rambo_prg_map);
	m_maincpu->set_addrmap(AS_DATA, &rambo_state::rambo_data_map);
	m_maincpu->set_eeprom_tag("eeprom");
	NVRAM(config, m_nvram, nvram_device::DEFAULT_ALL_1);

	m_maincpu->set_low_fuses(0xff);
	m_maincpu->set_high_fuses(0xd9);
	m_maincpu->set_extended_fuses(0xfd);
	m_maincpu->set_lock_bits(0x0f);

	m_maincpu->gpio_in<atmega2560_device::GPIOA>().set(FUNC(rambo_state::port_a_r));
	m_maincpu->gpio_in<atmega2560_device::GPIOB>().set(FUNC(rambo_state::port_b_r));
	m_maincpu->gpio_in<atmega2560_device::GPIOC>().set(FUNC(rambo_state::port_c_r));

	m_maincpu->gpio_out<atmega2560_device::GPIOA>().set(FUNC(rambo_state::port_a_w));
	m_maincpu->gpio_out<atmega2560_device::GPIOC>().set(FUNC(rambo_state::port_c_w));
	m_maincpu->gpio_out<atmega2560_device::GPIOD>().set(FUNC(rambo_state::port_d_w));
	m_maincpu->gpio_out<atmega2560_device::GPIOE>().set(FUNC(rambo_state::port_e_w));
	m_maincpu->gpio_out<atmega2560_device::GPIOG>().set(FUNC(rambo_state::port_g_w));
	m_maincpu->gpio_out<atmega2560_device::GPIOH>().set(FUNC(rambo_state::port_h_w));
	m_maincpu->gpio_out<atmega2560_device::GPIOK>().set(FUNC(rambo_state::port_k_w));
	m_maincpu->gpio_out<atmega2560_device::GPIOL>().set(FUNC(rambo_state::port_l_w));

	AD5206(config, m_digipot);
	m_digipot->set_resistance(10000.0);
	m_digipot->set_terminal_voltages(1.678, 0.0);
	m_maincpu->spi_out().set(m_digipot, FUNC(ad5206_device::write));

	for (int axis = 0; axis < AXIS_COUNT; axis++)
	{
		A4982(config, m_stepper[axis]);
		m_stepper[axis]->set_sense_resistor(0.1);
	}
	m_stepper[AXIS_X]->step_cb().set(FUNC(rambo_state::step_taken<AXIS_X>));
	m_stepper[AXIS_Y]->step_cb().set(FUNC(rambo_state::step_taken<AXIS_Y>));
	m_stepper[AXIS_Z]->step_cb().set(FUNC(rambo_state::step_taken<AXIS_Z>));
	m_stepper[AXIS_E0]->step_cb().set(FUNC(rambo_state::step_taken<AXIS_E0>));
	m_stepper[AXIS_E1]->step_cb().set(FUNC(rambo_state::step_taken<AXIS_E1>));

	m_digipot->wiper_cb<4>().set([this] (uint8_t) {
		m_stepper[AXIS_X]->set_vref(m_digipot->wiper_voltage(4));
	});
	m_digipot->wiper_cb<5>().set([this] (uint8_t) {
		m_stepper[AXIS_Y]->set_vref(m_digipot->wiper_voltage(5));
	});
	m_digipot->wiper_cb<3>().set([this] (uint8_t) {
		m_stepper[AXIS_Z]->set_vref(m_digipot->wiper_voltage(3));
	});
	m_digipot->wiper_cb<0>().set([this] (uint8_t) {
		m_stepper[AXIS_E0]->set_vref(m_digipot->wiper_voltage(0));
	});
	m_digipot->wiper_cb<1>().set([this] (uint8_t) {
		m_stepper[AXIS_E1]->set_vref(m_digipot->wiper_voltage(1));
	});

	m_maincpu->adc_in<0>().set([this]() { return thermistor_code(0); });
	m_maincpu->adc_in<2>().set([this]() { return thermistor_code(1); });

	RS232_PORT(config, m_rs232, default_rs232_devices, nullptr);
	m_rs232->set_option_device_input_defaults("terminal", DEVICE_INPUT_DEFAULTS_NAME(host_serial));
	m_rs232->set_option_device_input_defaults("null_modem", DEVICE_INPUT_DEFAULTS_NAME(host_serial));
	m_rs232->set_option_device_input_defaults("pty", DEVICE_INPUT_DEFAULTS_NAME(host_serial));
	m_maincpu->txd<0>().set(m_rs232, FUNC(rs232_port_device::write_txd));
	m_rs232->rxd_handler().set(m_maincpu, FUNC(atmega2560_device::rxd_w<0>));

}

ROM_START( metamaq2 )
	ROM_REGION( 0x40000, "maincpu", 0 )
	ROM_DEFAULT_BIOS("20131015")

	ROM_SYSTEM_BIOS( 0, "20130619", "June 19th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-06-19.bin", 0x0000, 0x1000e, CRC(4279b178) SHA1(e4d3c9d6421287c980639c2df32d07b754adc8fc), ROM_BIOS(0))

	ROM_SYSTEM_BIOS( 1, "20130624", "June 24th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-06-24_mm2rc2_rambo_rev10e.bin", 0x0000, 0xcebc, CRC(82400a3c) SHA1(0781ce29406ce69b63edb93d776b9c081bed841e), ROM_BIOS(1))

	ROM_SYSTEM_BIOS( 2, "20130625", "June 25th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-06-25.bin", 0x0000, 0x10076, CRC(e7e4db38) SHA1(0c307bb0a0ee4e9d38253936e7030d0efb3c1845), ROM_BIOS(2))

	ROM_SYSTEM_BIOS( 3, "20130709", "July 9th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-07-09.bin", 0x0000, 0x10078, CRC(9a45509f) SHA1(3a2e6516b45cc0ea1aef039335b02208847aaebf), ROM_BIOS(3))

	ROM_SYSTEM_BIOS( 4, "20130712", "July 12th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-07-12.bin", 0x0000, 0x10184, CRC(9aeac87c) SHA1(c1441096553c214c12a34da87fa42cc3f0eaf74d), ROM_BIOS(4))

	ROM_SYSTEM_BIOS( 5, "20130717", "July 17th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-07-17.bin", 0x0000, 0x10180, CRC(7c053ed0) SHA1(7abeabcbfdb411b6e681e2d0c9398c40b142f76b), ROM_BIOS(5))

	ROM_SYSTEM_BIOS( 6, "20130806", "August 6th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-08-06.bin", 0x0000, 0x1017e, CRC(6aaf5a14) SHA1(93cebee8ab9eda9d81e70504b407268a198577f0), ROM_BIOS(6))

	ROM_SYSTEM_BIOS( 7, "20130809", "August 9th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-08-09.bin", 0x0000, 0x1018a, CRC(ee53a011) SHA1(666d09fe69220a172528fe8d1c358e3ddaaa743a), ROM_BIOS(7))

	ROM_SYSTEM_BIOS( 8, "20130822", "August 22nd, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-08-22.bin", 0x0000, 0x1018a, CRC(70a5a3c9) SHA1(20e52ea7bf40e71020b815b9fb6385d880677927), ROM_BIOS(8))

	ROM_SYSTEM_BIOS( 9, "20130913", "September 13th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-09-13-devel.bin", 0x0000, 0x101bc, CRC(5e7c7933) SHA1(5b9bfe919daf705ad7a9a2de3cf4c51e3338ec47), ROM_BIOS(9))

	ROM_SYSTEM_BIOS( 10, "20130920", "September 20th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-09-20.bin", 0x0000, 0x10384, CRC(48378e58) SHA1(513f0a0c65219875cc467420cc091e3489b58919), ROM_BIOS(10))

	ROM_SYSTEM_BIOS( 11, "20131015", "October 15th, 2013" )
	ROMX_LOAD("repetier-fw-metamaquina2-2013-10-15.bin", 0x0000, 0x102c8, CRC(520134bd) SHA1(dfe2251aad06972f237eb4920ce14ccb32da5af0), ROM_BIOS(11))

	ROM_REGION( 0x1000, "eeprom", ROMREGION_ERASEFF )
ROM_END

} // anonymous namespace


//   YEAR  NAME      PARENT  COMPAT  MACHINE  INPUT  CLASS        INIT        COMPANY        FULLNAME                            FLAGS
COMP(2012, metamaq2, 0,      0,      rambo,   0,     rambo_state, empty_init, "Metamaquina", "Metamaquina 2 desktop 3d printer", MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
