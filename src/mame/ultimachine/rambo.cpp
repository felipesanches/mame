// license:GPL-2.0+
// copyright-holders:Felipe Sanches
/*
    RAMBo (RepRap Arduino-compatible Mother Board) by UltiMachine
    for controlling desktop 3d printers
    http://reprap.org/wiki/Rambo

    driver by Felipe Correa da Silva Sanches <fsanches@metamaquina.com.br>

    This driver is based on the schematics of the version 1.1b:
    http://reprap.org/mediawiki/images/7/75/Rambo1-1-schematic.png

    3d printers currently supported by this driver:
    * Metamáquina 2

    3d printers known to use this board:
    * TODO: list them all here

    ATmega2560 at 16 MHz, five A4982 stepper motor drivers whose current limits
    are set by an AD5206 six-channel digital potentiometer on the SPI bus, three
    power MOSFETs (hot end, heated bed, fan), two thermistor inputs on the ADC
    and six endstop inputs.  An ATmega32U2 running the stock LUFA usb-to-serial
    firmware bridges USART0 to the host transparently, so this driver attaches
    an ordinary RS232 port instead.

    G-code over that link at 115200 8N1; the machine has no panel and no SD
    card.  The firmware allows 200 ms between characters before it asks for the
    line again (gcode.cpp, gcode_read_serial), so whatever is attached must send
    a whole line at a time.
*/

#include "emu.h"

#include "cpu/avr8/avr8.h"
#include "mm2_stepper_sound.h"

#include "machine/a4982.h"
#include "machine/ad5206.h"
#include "machine/nvram.h"
#include "machine/rescap.h"
#include "bus/rs232/rs232.h"

#include "speaker.h"

#include "metamaq2.lh"

#include <cmath>


namespace {

#define MASTER_CLOCK    16000000

/****************************************************\
* I/O devices                                        *
\****************************************************/

/*
    Mechanics of the machine the board is bolted to.  Steps per millimetre
    follow the firmware's Configuration.h: X and Y are GT2 belts on 16 tooth
    pulleys (200 full steps x 16 microsteps / 32 mm), Z is a pair of M8 threaded
    rods at 1.25 mm per turn, and the extruder's 650 steps/mm is the calibration
    figure the machine shipped with.
*/
struct mm2_axis
{
	double steps_per_mm;
	double travel_mm;       // from the MIN endstop to the MAX endstop
	double overtravel_mm;   // how far past each switch the frame lets it go
	double direction;       // +1 if a rising translator count moves the axis positive
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
		, m_motors(*this, "motors")
		, m_axis_out(*this, "axis_%u_um")
		, m_endstop_out(*this, "endstop_%u")
		, m_temp_out(*this, "temp_%u_c")
		, m_duty_out(*this, "duty_%u")
		, m_motor_out(*this, "motor_%u_on")
	{
	}

	void rambo(machine_config &config);

	// the five A4982 channels, in the order the firmware numbers them
	enum : int { AXIS_X, AXIS_Y, AXIS_Z, AXIS_E0, AXIS_E1, AXIS_COUNT };

	// the six endstops, in the order M119 reports them
	enum : int { ES_X_MIN, ES_X_MAX, ES_Y_MIN, ES_Y_MAX, ES_Z_MIN, ES_Z_MAX, ES_COUNT };

	// software-PWM outputs, indexed by the firmware's own pwm_pos[] slots
	enum : int { PWM_HOTEND, PWM_BED, PWM_FAN, PWM_COUNT };

private:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

	void rambo_prg_map(address_map &map) ATTR_COLD;
	void rambo_data_map(address_map &map) ATTR_COLD;

	// GPIO
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

	// mechanics
	template <int Axis> void step_taken(uint64_t position);
	void update_endstops();
	double axis_mm(int axis) const;
	TIMER_CALLBACK_MEMBER(view_tick);

	// heaters
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
	required_device<mm2_stepper_sound_device> m_motors;

	output_finder<AXIS_COUNT> m_axis_out;
	output_finder<ES_COUNT> m_endstop_out;
	output_finder<2> m_temp_out;
	output_finder<PWM_COUNT> m_duty_out;
	output_finder<AXIS_COUNT> m_motor_out;

	// where the machine actually is, in microsteps from the MIN endstop.  Not
	// the A4982's own count: the translator keeps stepping with the outputs off
	// or the carriage against the frame, and so does a real one.
	int64_t m_position[AXIS_COUNT]{};
	int64_t m_last_translator[AXIS_COUNT]{};
	uint8_t m_endstops = 0;

	// software PWM measurement
	uint8_t m_pwm_state[PWM_COUNT]{};
	attotime m_pwm_changed[PWM_COUNT];
	attotime m_pwm_high[PWM_COUNT];

	// thermal state, in degrees Celsius
	double m_temperature[2]{};
	double m_adc_residue[2]{};   // carried between conversions, so oversampling works

	emu_timer *m_thermal_timer = nullptr;
	emu_timer *m_view_timer = nullptr;
};

// The extruder motors are wired the other way round from the axis motors
// (EXT0_INVERSE in Configuration.h): E0_DIR low pushes filament in, hence the
// negative direction below.
static constexpr mm2_axis AXES[rambo_state::AXIS_COUNT] = {
	{  100.0, 200.0, 3.0, +1.0 },   // X, belt
	{  100.0, 200.0, 3.0, +1.0 },   // Y, belt
	{ 2560.0, 150.0, 2.0, +1.0 },   // Z, two M8 screws driven from one channel
	{  650.0,   0.0, 0.0, -1.0 },   // E0, extruder
	{  650.0,   0.0, 0.0, -1.0 }    // E1, not fitted
};

// Power-on pose: somewhere plausible mid-machine rather than on a switch.
static constexpr double POWER_ON_POSE[3] = { 100.0, 100.0, 130.0 };

/****************************************************\
* Address maps                                       *
\****************************************************/

void rambo_state::rambo_prg_map(address_map &map)
{
	map(0x00000, 0x3ffff).rom();
}

void rambo_state::rambo_data_map(address_map &map)
{
	map(0x0200, 0x21FF).ram();  /* ATMEGA2560 Internal SRAM */
}

/****************************************************\
* Mechanics                                          *
\****************************************************/

// One accepted STEP edge.  The translator counts every edge even with the
// output FETs off, so the tracked position follows it only while the axis can
// actually turn, and is clamped to the travel the frame allows.
template <int Axis>
void rambo_state::step_taken(uint64_t position)
{
	const int64_t now = int64_t(position);
	const int64_t delta = now - m_last_translator[Axis];
	m_last_translator[Axis] = now;

	// the sound device is handed the two winding currents as of this microstep
	// rather than the step rate
	m_motors->step_edge(Axis, m_stepper[Axis]->coil_current(0), m_stepper[Axis]->coil_current(1));

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

/*
    Normally-closed microswitches to ground with the AVR's internal pull-up, so
    an untriggered endstop reads LOW and a pressed one HIGH; Configuration.h
    leaves every ENDSTOP_*_INVERTING false.

      X_MIN = PB6   Y_MIN = PB5   Z_MIN = PB4
      X_MAX = PA2   Y_MAX = PA1   Z_MAX = PC7
*/
void rambo_state::update_endstops()
{
	// half a millimetre of switch travel, about what a KW11-3Z gives
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
	// PA1 = Y_MAX, PA2 = X_MAX
	return (BIT(m_endstops, ES_Y_MAX) << 1) | (BIT(m_endstops, ES_X_MAX) << 2);
}

uint8_t rambo_state::port_b_r()
{
	// PB4 = Z_MIN, PB5 = Y_MIN, PB6 = X_MIN
	return (BIT(m_endstops, ES_Z_MIN) << 4) | (BIT(m_endstops, ES_Y_MIN) << 5) | (BIT(m_endstops, ES_X_MIN) << 6);
}

uint8_t rambo_state::port_c_r()
{
	// PC7 = Z_MAX
	return BIT(m_endstops, ES_Z_MAX) << 7;
}

/****************************************************\
* GPIO                                               *
\****************************************************/

// PA4..PA7 are the four /ENABLE lines, active low
void rambo_state::port_a_w(uint8_t data)
{
	m_stepper[AXIS_E0]->enable_w(BIT(data, 4));
	m_stepper[AXIS_Z]->enable_w(BIT(data, 5));
	m_stepper[AXIS_Y]->enable_w(BIT(data, 6));
	m_stepper[AXIS_X]->enable_w(BIT(data, 7));
	update_endstops();
}

// PC0..PC4 are the five STEP lines
void rambo_state::port_c_w(uint8_t data)
{
	m_stepper[AXIS_X]->step_w(BIT(data, 0));
	m_stepper[AXIS_Y]->step_w(BIT(data, 1));
	m_stepper[AXIS_Z]->step_w(BIT(data, 2));
	m_stepper[AXIS_E0]->step_w(BIT(data, 3));
	m_stepper[AXIS_E1]->step_w(BIT(data, 4));
}

// PD7 is the digipot chip select
void rambo_state::port_d_w(uint8_t data)
{
	m_digipot->cs_w(BIT(data, 7));
}

// PE5 drives the heated bed MOSFET
void rambo_state::port_e_w(uint8_t data)
{
	pwm_level(PWM_BED, BIT(data, 5));
}

// PG0 = X_MS2, PG1 = X_MS1, PG2 = Y_MS2
void rambo_state::port_g_w(uint8_t data)
{
	m_stepper[AXIS_X]->ms2_w(BIT(data, 0));
	m_stepper[AXIS_X]->ms1_w(BIT(data, 1));
	m_stepper[AXIS_Y]->ms2_w(BIT(data, 2));
}

// PH4 = HEATER_1, PH5 = FAN, PH6 = HEATER_0.  HEATER_1 belongs to a second
// extruder this machine does not have, and the firmware drives it from the
// heated bed's own pwm_pos[] slot, so it simply follows PE5.
void rambo_state::port_h_w(uint8_t data)
{
	pwm_level(PWM_FAN, BIT(data, 5));
	pwm_level(PWM_HOTEND, BIT(data, 6));
}

// PK1 = E1_MS1, PK2 = E1_MS2, PK3 = E0_MS1, PK4 = E0_MS2,
// PK5 = Z_MS2, PK6 = Z_MS1, PK7 = Y_MS1
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

// PL0 = Y_DIR, PL1 = X_DIR, PL2 = Z_DIR, PL6 = E0_DIR, PL7 = E1_DIR
void rambo_state::port_l_w(uint8_t data)
{
	m_stepper[AXIS_Y]->dir_w(BIT(data, 0));
	m_stepper[AXIS_X]->dir_w(BIT(data, 1));
	m_stepper[AXIS_Z]->dir_w(BIT(data, 2));
	m_stepper[AXIS_E0]->dir_w(BIT(data, 6));
	m_stepper[AXIS_E1]->dir_w(BIT(data, 7));
}

/****************************************************\
* Heaters and thermistors                            *
\****************************************************/

// The firmware does not use the ATmega's hardware PWM: it toggles the three
// MOSFET pins from the TIMER0_COMPB interrupt against an 8-bit counter, a
// 15.26 Hz software PWM, so the duty cycle has to be measured from the pins.
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

/*
    First-order thermal model:

        C dT/dt = P.duty - k (T - Tambient)
*/
TIMER_CALLBACK_MEMBER(rambo_state::thermal_tick)
{
	static constexpr double AMBIENT = 25.0;
	static constexpr double INTERVAL = 0.1;   // seconds

	// Hot end: a UB5C-5RF1 5 ohm wirewound resistor (nozzle.scad's own bill of
	// materials) on the 12 V heater rail, so 28.8 W; C from the brass heater
	// block drawn in nozzle.scad; k estimated so that holding 230 C idles at
	// about a third of the heater.  The bed's three are estimates throughout.
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

TIMER_CALLBACK_MEMBER(rambo_state::view_tick)
{
	// A motor that is energised but not stepping still carries current and is
	// audible, and an axis that has not moved emits no step edges at all, so the
	// standing state is pushed here on the same 500 Hz tick.
	for (int axis = 0; axis < AXIS_COUNT; axis++)
		m_motors->set_holding(axis, m_stepper[axis]->outputs_enabled(),
							  m_stepper[axis]->coil_current(0), m_stepper[axis]->coil_current(1),
							  m_stepper[axis]->current_limit());
}

/*
    Each thermistor sits between its input pin and ground with a 4.7 kohm
    pull-up to the 5 V rail (RAMBo 1.1b R31 for the hot end, R38 for the bed):

        ADC = 1023 . Rt / (Rt + 4700)

    Hot end: EPCOS B57560G0104F000, 100 kohm -- the firmware's sensor type 1
    table misprints the part number as B57560G0107F000.  Modelled from the
    manufacturer's R/T characteristic No. 8404, tabulated in the B57540
    datasheet of March 2006, carried as a Steinhart-Hart fit because three
    coefficients invert in closed form and reproduce every tabulated point from
    -55 to 250 C to better than a quarter of a degree.  A single beta is not
    good enough: fitted at 25 C it is nine degrees out at 220 C.

    Bed: 15 kohm, beta 3528, which is all the machine's Configuration.h says
    about it.
*/
double rambo_state::thermistor_resistance(int channel, double celsius)
{
	const double kelvin = celsius + 273.15;

	if (channel == 0)
	{
		// TDK/EPCOS R/T characteristic 8404, as a Steinhart-Hart fit:
		//     1/T = A + B.ln(R) + C.ln(R)^3
		// inverted in closed form; y is strictly greater than |x|/2, so both cube
		// roots are of positive numbers and there is exactly one real root.
		static constexpr double A = 7.1577566465e-04;
		static constexpr double B = 2.1755533222e-04;
		static constexpr double C = 8.7241270924e-08;

		const double x = (A - 1.0 / kelvin) / C;
		const double y = std::sqrt((B / (3.0 * C)) * (B / (3.0 * C)) * (B / (3.0 * C))
								   + x * x * 0.25);
		return std::exp(std::cbrt(y - x * 0.5) - std::cbrt(y + x * 0.5));
	}

	// the bed, specified only by a beta
	static constexpr double BED_R25 = 15000.0;
	static constexpr double BED_BETA = 3528.0;
	return BED_R25 * std::exp(BED_BETA * (1.0 / kelvin - 1.0 / 298.15));
}

uint16_t rambo_state::thermistor_code(int channel)
{
	// R31 for the hot end, R38 for the bed, both 4.7 kohm to the 5 V rail
	static constexpr double PULLUP = RES_K(4.7);

	// the fit is only valid over the range the datasheet tabulates, and the
	// firmware clamps its setpoint at 240 C anyway
	const double celsius = std::clamp(m_temperature[channel], -55.0, 250.0);
	const double rt = thermistor_resistance(channel, celsius);

	const double exact = 1023.0 * RES_VOLTAGE_DIVIDER(PULLUP, rt);

	// The firmware sums thirty-two conversions and shifts right by three, which
	// gains two bits only because a real converter's samples do not all agree.
	// Carrying the rounding residue into the next conversion recovers those bits
	// deterministically: the resolution is reproduced, not the noise.
	const double wanted = std::clamp(exact + m_adc_residue[channel], 0.0, 1023.0);
	const double rounded = std::floor(wanted + 0.5);
	m_adc_residue[channel] = std::clamp(wanted - rounded, -1.0, 1.0);

	return uint16_t(rounded);
}

// 115200 8N1, the firmware's own setting, so a terminal or null_modem needs no
// further configuration.
static DEVICE_INPUT_DEFAULTS_START( host_serial )
	DEVICE_INPUT_DEFAULTS( "RS232_RXBAUD", 0xff, RS232_BAUD_115200 )
	DEVICE_INPUT_DEFAULTS( "RS232_TXBAUD", 0xff, RS232_BAUD_115200 )
	DEVICE_INPUT_DEFAULTS( "RS232_DATABITS", 0xff, RS232_DATABITS_8 )
	DEVICE_INPUT_DEFAULTS( "RS232_PARITY", 0xff, RS232_PARITY_NONE )
	DEVICE_INPUT_DEFAULTS( "RS232_STOPBITS", 0xff, RS232_STOPBITS_1 )
DEVICE_INPUT_DEFAULTS_END

/****************************************************\
* Machine definition                                 *
\****************************************************/

void rambo_state::machine_start()
{
	m_axis_out.resolve();
	m_endstop_out.resolve();
	m_temp_out.resolve();
	m_duty_out.resolve();
	m_motor_out.resolve();

	// the on-die EEPROM is where the firmware keeps its settings, so back the
	// region with NVRAM
	m_nvram->set_base(m_eeprom->base(), m_eeprom->bytes());

	m_thermal_timer = timer_alloc(FUNC(rambo_state::thermal_tick), this);
	m_view_timer = timer_alloc(FUNC(rambo_state::view_tick), this);

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

	// The five /ENABLE lines are pulled to VCC through 100 kohm (RAMBo 1.1b R60,
	// R59, R61, R62 and R63), so until the firmware drives PA3..PA7 the output
	// FETs are off and every motor is unheld.
	for (int axis = 0; axis < AXIS_COUNT; axis++)
		m_stepper[axis]->enable_w(1);

	m_endstops = 0xff;   // force update_endstops() to publish every output
	update_endstops();

	for (int channel = 0; channel < PWM_COUNT; channel++)
	{
		m_pwm_state[channel] = 0;
		m_pwm_changed[channel] = machine().time();
		m_pwm_high[channel] = attotime::zero;
	}

	// room temperature
	m_temperature[0] = m_temperature[1] = 25.0;

	m_thermal_timer->adjust(attotime::from_msec(100), 0, attotime::from_msec(100));
	m_view_timer->adjust(attotime::from_msec(2), 0, attotime::from_msec(2));
}

void rambo_state::rambo(machine_config &config)
{
	ATMEGA2560(config, m_maincpu, MASTER_CLOCK);
	m_maincpu->set_addrmap(AS_PROGRAM, &rambo_state::rambo_prg_map);
	m_maincpu->set_addrmap(AS_DATA, &rambo_state::rambo_data_map);
	m_maincpu->set_eeprom_tag("eeprom");
	NVRAM(config, m_nvram, nvram_device::DEFAULT_ALL_1);

	/*
	    A factory RAMBo is fused low=0xFF high=0xD8 extended=0xFD lock=0x0F
	    (ultimachine/RAMBo, ArduinoAddons/Arduino_1.x.x/rambo/boards.txt), which
	    puts an 8 KiB boot section at 0x3E000 and clears BOOTRST.  No dump of the
	    bootloader programmed into a Metamáquina 2 exists, so BOOTRST is left
	    unprogrammed here and the application starts directly, as it does on a
	    board whose bootloader has been erased.
	*/
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

	/*
	    The digipot's six ladders are wired in parallel between a 3.3 kohm
	    resistor from the 5 V rail and ground, so their A terminals sit at
	    5 . (10k/6) / (3.3k + 10k/6) = 1.678 V, and a full-scale wiper gives
	    1.678 / (8 . 0.1) = 2.10 A.  The firmware programs the four axes it uses
	    from MOTOR_CURRENT in Configuration.h: 100, 100, 135 and 110 counts, or
	    0.82, 0.82, 1.11 and 0.90 A.
	*/
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

	// RDAC 5 and 6 feed X and Y, RDAC 4 feeds Z, RDAC 1 and 2 the two extruders
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

	// TEMP_0 (hot end) = ADC0 = PF0, TEMP_BED = ADC2 = PF2.  An unconnected
	// input converts to 500 C and 678 C, which trips the firmware's
	// thermistor-defect check and puts the machine into dry-run mode.
	m_maincpu->adc_in<0>().set([this]() { return thermistor_code(0); });
	m_maincpu->adc_in<2>().set([this]() { return thermistor_code(1); });

	SPEAKER(config, "mono").front_center();
	MM2_STEPPER_SOUND(config, m_motors).add_route(ALL_OUTPUTS, "mono", 1.0);

	config.set_default_layout(layout_metamaq2);

	/* The ATMEGA32U2 that bridges USART0 to USB is a transparent wire */
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
	/* SOURCE(https://github.com/Metamaquina/Repetier-Firmware/tree/MM2_2013_06_19) */
	ROMX_LOAD("repetier-fw-metamaquina2-2013-06-19.bin", 0x0000, 0x1000e, CRC(4279b178) SHA1(e4d3c9d6421287c980639c2df32d07b754adc8fc), ROM_BIOS(0))

	ROM_SYSTEM_BIOS( 1, "20130624", "June 24th, 2013" )
	/* SOURCE(https://github.com/Metamaquina/Repetier-Firmware/tree/MM2_RC2_RAMBo_rev10e_2013_06_24) */
	ROMX_LOAD("repetier-fw-metamaquina2-2013-06-24_mm2rc2_rambo_rev10e.bin", 0x0000, 0xcebc, CRC(82400a3c) SHA1(0781ce29406ce69b63edb93d776b9c081bed841e), ROM_BIOS(1))

	ROM_SYSTEM_BIOS( 2, "20130625", "June 25th, 2013" )
	/* SOURCE(https://github.com/Metamaquina/Repetier-Firmware/tree/MM2_2013_06_25) */
	ROMX_LOAD("repetier-fw-metamaquina2-2013-06-25.bin", 0x0000, 0x10076, CRC(e7e4db38) SHA1(0c307bb0a0ee4e9d38253936e7030d0efb3c1845), ROM_BIOS(2))

	ROM_SYSTEM_BIOS( 3, "20130709", "July 9th, 2013" )
	/* the tag this was built from is missing from the firmware repository */
	ROMX_LOAD("repetier-fw-metamaquina2-2013-07-09.bin", 0x0000, 0x10078, CRC(9a45509f) SHA1(3a2e6516b45cc0ea1aef039335b02208847aaebf), ROM_BIOS(3))

	ROM_SYSTEM_BIOS( 4, "20130712", "July 12th, 2013" )
	/* SOURCE(https://github.com/Metamaquina/Repetier-Firmware/tree/MM2_2013_07_12) */
	ROMX_LOAD("repetier-fw-metamaquina2-2013-07-12.bin", 0x0000, 0x10184, CRC(9aeac87c) SHA1(c1441096553c214c12a34da87fa42cc3f0eaf74d), ROM_BIOS(4))

	ROM_SYSTEM_BIOS( 5, "20130717", "July 17th, 2013" )
	/* SOURCE(https://github.com/Metamaquina/Repetier-Firmware/tree/MM2_2013_07_17) */
	ROMX_LOAD("repetier-fw-metamaquina2-2013-07-17.bin", 0x0000, 0x10180, CRC(7c053ed0) SHA1(7abeabcbfdb411b6e681e2d0c9398c40b142f76b), ROM_BIOS(5))

	ROM_SYSTEM_BIOS( 6, "20130806", "August 6th, 2013" )
	/* SOURCE(https://github.com/Metamaquina/Repetier-Firmware/tree/MM2_2013_08_06) */
	ROMX_LOAD("repetier-fw-metamaquina2-2013-08-06.bin", 0x0000, 0x1017e, CRC(6aaf5a14) SHA1(93cebee8ab9eda9d81e70504b407268a198577f0), ROM_BIOS(6))

	ROM_SYSTEM_BIOS( 7, "20130809", "August 9th, 2013" )
	/* SOURCE(https://github.com/Metamaquina/Repetier-Firmware/tree/MM2_2013_08_09) */
	ROMX_LOAD("repetier-fw-metamaquina2-2013-08-09.bin", 0x0000, 0x1018a, CRC(ee53a011) SHA1(666d09fe69220a172528fe8d1c358e3ddaaa743a), ROM_BIOS(7))

	ROM_SYSTEM_BIOS( 8, "20130822", "August 22nd, 2013" )
	/* SOURCE(https://github.com/Metamaquina/Repetier-Firmware/tree/MM2_2013_08_22) */
	ROMX_LOAD("repetier-fw-metamaquina2-2013-08-22.bin", 0x0000, 0x1018a, CRC(70a5a3c9) SHA1(20e52ea7bf40e71020b815b9fb6385d880677927), ROM_BIOS(8))

	ROM_SYSTEM_BIOS( 9, "20130913", "September 13th, 2013" )
	/* source code for this one is unavailable as it was an unreleased internal development build */
	ROMX_LOAD("repetier-fw-metamaquina2-2013-09-13-devel.bin", 0x0000, 0x101bc, CRC(5e7c7933) SHA1(5b9bfe919daf705ad7a9a2de3cf4c51e3338ec47), ROM_BIOS(9))

	ROM_SYSTEM_BIOS( 10, "20130920", "September 20th, 2013" )
	/* SOURCE(https://github.com/Metamaquina/Repetier-Firmware/tree/MM2_2013_09_20) */
	ROMX_LOAD("repetier-fw-metamaquina2-2013-09-20.bin", 0x0000, 0x10384, CRC(48378e58) SHA1(513f0a0c65219875cc467420cc091e3489b58919), ROM_BIOS(10))

	ROM_SYSTEM_BIOS( 11, "20131015", "October 15th, 2013" )
	/* SOURCE(https://github.com/Metamaquina/Repetier-Firmware/tree/MM2_2013_10_15) */
	ROMX_LOAD("repetier-fw-metamaquina2-2013-10-15.bin", 0x0000, 0x102c8, CRC(520134bd) SHA1(dfe2251aad06972f237eb4920ce14ccb32da5af0), ROM_BIOS(11))

	/*
	    The boot section is undumped.  A real RAMBo carries
	    stk500boot_v2_mega2560, but the only copies available are vendor build
	    artefacts rather than a dump read off a factory-programmed part, so
	    nothing is loaded here.
	*/

	/* on-die 4kbyte eeprom */
	ROM_REGION( 0x1000, "eeprom", ROMREGION_ERASEFF )
ROM_END

} // anonymous namespace


//   YEAR  NAME      PARENT  COMPAT  MACHINE  INPUT  CLASS        INIT        COMPANY        FULLNAME                            FLAGS
COMP(2012, metamaq2, 0,      0,      rambo,   0,     rambo_state, empty_init, "Metamaquina", "Metamaquina 2 desktop 3d printer", MACHINE_NOT_WORKING)
