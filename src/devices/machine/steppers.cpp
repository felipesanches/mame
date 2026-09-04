// license:BSD-3-Clause
// copyright-holders:James Wallace
///////////////////////////////////////////////////////////////////////////
//                                                                       //
// steppers.c steppermotor emulation                                     //
//                                                                       //
// Emulates : Stepper motors driven with full step or half step          //
//            also emulates the index optic                              //
//                                                                       //
// 26-05-2012: J. Wallace - Implemented proper phase alignment, we no    //
//                          longer need reverse interfaces here, the     //
//                          layout will suffice. Added belt reel handler.//
// 09-04-2012: J. Wallace - Studied some old reel motors and added a     //
//                          number of new stepper types. I am yet to     //
//                          add them to drivers, but barring some init   //
//                          stuff, they should work.                     //
// 15-01-2012: J. Wallace - Total internal rewrite to remove the table   //
//                          hoodoo that stops anyone but me actually     //
//                          updating this. In theory, we should be able  //
//                          to adapt the phase code to any reel type by  //
//                          studying a game's startup                    //
//                          Documentation is much better now.            //
// 04-04-2011: J. Wallace - Added reverse spin (this is necessary for    //
//                          accuracy), and improved wraparound logic     //
//    03-2011:              New 2D array to remove reel bounce and       //
//                          make more realistic                          //
// 26-01-2007: J. Wallace - Rewritten to make it more flexible           //
//                          and to allow indices to be set in drivers    //
// 29-12-2006: J. Wallace - Added state save support                     //
// 05-03-2004: Re-Animator                                               //
//                                                                       //
// TODO:  add further types of stepper motors if needed (Konami/IGT?)    //
//        200 Step reels can alter their relative opto tab position,     //
//        may be worth adding the phase setting to the interface         //
//        There are reports that some games use a pulse that is too short//
//        to give a 'judder' effect for holds, etc. We'll need to time   //
//        the pulses to keep tack of this without going out of sync.     //
//        Check 20RM and Starpoint 200 step                              //
///////////////////////////////////////////////////////////////////////////

#include "emu.h"
#include "steppers.h"

#include <algorithm>
#include <cmath>


namespace {

constexpr double RADIAL = 2.0;
constexpr double TANGENTIAL = 200.0;
constexpr double DETENT_SNAP = 0.5;
constexpr double VOICE_GAIN = 0.06;
constexpr double CEILING = 0.6;
constexpr double REGIME_CHANGE = 0.25;
constexpr double SILENT_BELOW = 1e-5;
constexpr double RADIATE_HZ = 8000.0;
constexpr uint32_t QUEUE = 2048;

constexpr double R2 = 0.70710678118654752;

constexpr double PHASE_IA[8] = {  R2,  1.0,   R2,  0.0,  -R2, -1.0,  -R2,  0.0 };
constexpr double PHASE_IB[8] = {  R2,  0.0,  -R2, -1.0,  -R2,  0.0,   R2,  1.0 };

struct motor_spec
{
	double inertia;
	double torque;
	double current;
	double inductance;
	double supply;
	int teeth;
	double q;
	double gain;
};

const motor_spec MOTORS[stepper_device::MOTOR_TYPES] =
{
	{ 5.5e-6, 0.400, 1.20, 4.0e-3, 12.0, 50, 8.0, 1.50 },
	{ 1.2e-6, 0.020, 0.50, 1.0e-2, 12.0, 12, 6.0, 1.00 },
	{ 2.0e-6, 0.060, 0.60, 1.5e-2, 12.0, 50, 6.0, 1.00 }
};

} // anonymous namespace


struct stepper_device::acoustics
{
	struct lowpass
	{
		void configure(double freq, double rate)
		{
			a = 1.0 - std::exp(-2.0 * M_PI * std::clamp(freq, 10.0, rate * 0.45) / rate);
		}
		double run(double x) { y += a * (x - y); return y; }

		double a = 1.0;
		double y = 0.0;
	};

	struct resonator
	{
		void configure(double freq, double q, double rate)
		{
			freq = std::clamp(freq, 10.0, rate * 0.45);
			const double r = std::exp(-M_PI * freq / (q * rate));
			const double w = 2.0 * M_PI * freq / rate;

			b1 = 2.0 * r * std::cos(w);
			b2 = -r * r;
			a = (1.0 - r * r) * 0.5;
		}

		double run(double x)
		{
			const double y = a * (x - x2) + b1 * y1 + b2 * y2;
			x2 = x1; x1 = x;
			y2 = y1; y1 = y;
			return y;
		}

		double a = 0.0, b1 = 0.0, b2 = 0.0;
		double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
	};

	struct mode_spec
	{
		double hz = 0.0;
		double q = 0.0;
		double gain = 0.0;
		bool pinned = false;
	};

	void slew(double dt)
	{
		if (dt <= 0.0)
			return;

		const double reach = (supply / inductance) * std::min(dt, 1.0);
		act_ia += std::clamp(cmd_ia - act_ia, -reach, reach);
		act_ib += std::clamp(cmd_ib - act_ib, -reach, reach);
		radial = act_ia * act_ia + act_ib * act_ib;
	}

	double inertia = MOTORS[MOTOR_NEMA17].inertia;
	double torque = MOTORS[MOTOR_NEMA17].torque;
	double rated_current = MOTORS[MOTOR_NEMA17].current;
	double drive_current = MOTORS[MOTOR_NEMA17].current;
	double inductance = MOTORS[MOTOR_NEMA17].inductance;
	double supply = MOTORS[MOTOR_NEMA17].supply;
	int teeth = MOTORS[MOTOR_NEMA17].teeth;
	double rotor_q = MOTORS[MOTOR_NEMA17].q;
	double rotor_gain = MOTORS[MOTOR_NEMA17].gain;
	double load_inertia = 0.0;
	double extra_inertia = 0.0;
	double radiation = 0.5;
	mode_spec mode[ACOUSTIC_MODES];

	double when[QUEUE] = { 0.0 };
	double ia[QUEUE] = { 0.0 };
	double ib[QUEUE] = { 0.0 };
	uint32_t head = 0;
	uint32_t tail = 0;

	double cmd_ia = 0.0, cmd_ib = 0.0;
	double act_ia = 0.0, act_ib = 0.0;
	double last_slew = 0.0;
	double radial = 0.0;
	double carry = 0.0;
	double thunk = 0.0;
	double dc_x1 = 0.0, dc_y1 = 0.0;
	bool energised = false;
	bool prime = true;
	double tuned_at = -1.0;
	uint32_t dropped = 0;

	double active_gain[ACOUSTIC_MODES] = { 0.0 };
	resonator filter[ACOUSTIC_MODES];
	lowpass radiate;
};


DEFINE_DEVICE_TYPE(STEPPER, stepper_device, "stepper", "Stepper Motor")
DEFINE_DEVICE_TYPE(REEL, reel_device, "reel", "Fruit Machine Reel")

stepper_device::stepper_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: stepper_device(mconfig, STEPPER, tag, owner, clock)
{
}

stepper_device::stepper_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, type, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, m_max_steps(48*2)
	, m_optic_cb(*this)
	, m_stream(nullptr)
{
}

stepper_device::~stepper_device()
{
}

///////////////////////////////////////////////////////////////////////////

void stepper_device::update_optic()
{
	int pos   = m_step_pos,
		start = m_index_start,
		end = m_index_end;

	if (start > end) // cope with index patterns that wrap around
	{
		if ( (( pos > start ) || ( pos < end )) &&
		( ( m_pattern == m_index_patt || m_index_patt==0) ||
		( m_pattern == 0 &&
		(m_old_pattern == m_index_patt || m_index_patt==0)
		) ) )
		{
			m_optic = 1;
		}
		else m_optic = 0;
		}
	else
	{
		if ( (( pos > start ) && ( pos < end )) &&
		( ( m_pattern == m_index_patt || m_index_patt==0) ||
		( m_pattern == 0 &&
		(m_old_pattern == m_index_patt || m_index_patt==0)
		) ) )
		{
		m_optic = 1;
		}
		else m_optic = 0;
	}

	m_optic_cb(m_optic);
}
///////////////////////////////////////////////////////////////////////////

void stepper_device::device_start()
{
	/* register for state saving */
	save_item(NAME(m_index_start));
	save_item(NAME(m_index_end));
	save_item(NAME(m_index_patt));
	save_item(NAME(m_initphase));
	save_item(NAME(m_phase));
	save_item(NAME(m_old_phase));
	save_item(NAME(m_pattern));
	save_item(NAME(m_old_pattern));
	save_item(NAME(m_step_pos));
	save_item(NAME(m_abs_step_pos));
	save_item(NAME(m_max_steps));

	if (m_acoustics)
	{
		acoustics &a = *m_acoustics;

		m_stream = stream_alloc(0, 1, 48000);

		save_item(a.cmd_ia, "acoustic_cmd_ia");
		save_item(a.cmd_ib, "acoustic_cmd_ib");
		save_item(a.act_ia, "acoustic_act_ia");
		save_item(a.act_ib, "acoustic_act_ib");
		save_item(a.last_slew, "acoustic_last_slew");
		save_item(a.radial, "acoustic_radial");
		save_item(a.carry, "acoustic_carry");
		save_item(a.dc_x1, "acoustic_dc_x1");
		save_item(a.dc_y1, "acoustic_dc_y1");
		save_item(a.drive_current, "acoustic_drive_current");
		save_item(a.energised, "acoustic_energised");

		logerror("acoustics: rotor resonance %.1f Hz at %.2f A\n", resonance_hz(), a.drive_current);
	}
}

///////////////////////////////////////////////////////////////////////////

void stepper_device::device_reset()
{
	m_step_pos     = 0x00;
	m_abs_step_pos = 0x00;
	m_pattern      = 0x00;
	m_old_pattern  = 0x00;
	m_phase        = m_initphase;
	m_old_phase    = m_initphase;
	update_optic();

	if (m_acoustics)
	{
		acoustics &a = *m_acoustics;

		a.head = a.tail = 0;
		a.cmd_ia = a.cmd_ib = a.act_ia = a.act_ib = a.radial = 0.0;
		a.last_slew = 0.0;
		a.carry = a.thunk = a.dc_x1 = a.dc_y1 = 0.0;
		a.energised = false;
		a.prime = true;
		a.tuned_at = -1.0;
		a.dropped = 0;
		for (acoustics::resonator &r : a.filter)
			r = acoustics::resonator();
		a.radiate = acoustics::lowpass();
	}
}

///////////////////////////////////////////////////////////////////////////

void stepper_device::device_post_load()
{
	if (m_acoustics)
	{
		acoustics &a = *m_acoustics;

		a.head = a.tail = 0;
		a.carry = 0.0;
		a.prime = true;
	}
}

///////////////////////////////////////////////////////////////////////////

int stepper_device::update(uint8_t pattern)
{
	int changed = 0;

	/* This code probably makes more sense if you visualise what is being emulated, namely
	a spinning drum with two electromagnets inside. Essentially, the CPU
	activates a pair of windings on these magnets leads as necessary to attract and repel the drum to pull it round and
	display as appropriate. To attempt to visualise the rotation effect, take a look at the compass rose below, representing a side on view of the reel,
	the numbers indicate the phase information as used

	    7
	    N
	1 W   E 5
	    S
	    3

	For sake of accuracy, we're representing all possible phases of the motor, effectively moving the motor one half step at a time, so a 48 step motor becomes
	96 half steps. This is necessary because of some programs running the wiring in series with a distinct delay between the pair being completed. This causes
	a small movement that may trigger the optic tab.
	*/

	m_pattern = pattern;
	advance_phase();

	int steps = m_old_phase - m_phase;
	if (steps < -4)
	{
		steps = steps +8;
	}
	if (steps > 4)
	{
		steps = steps -8;
	}

	m_old_phase   = m_phase;
	m_old_pattern = m_pattern;

	int max = m_max_steps;
	int pos = 0;

	if (max!=0)
	{
		m_abs_step_pos += steps;
		pos = (m_step_pos + steps + max) % max;
	}

	if (pos != m_step_pos)
	{
		changed++;
	}

	m_step_pos = pos;
	update_optic();

	if (m_acoustics)
		excite_phase(steps != 0);

	return changed;
}
///////////////////////////////////////////////////////////////////////////

void stepper_device::advance_phase()
{
	//Standard drive table is 2,6,4,5,1,9,8,a
	//NOTE: This runs through the stator patterns in such a way as to drive the reel forward (downwards from the player's view, clockwise on our rose)
	//The Heber 'Pluto' controller runs this in reverse, this needs checking on real hardware
	switch (m_pattern)
	{             //Black  Blue  Red  Yellow
		case 0x02://  0     0     1     0
		m_phase = 7;
		break;
		case 0x06://  0     1     1     0
		m_phase = 6;
		break;
		case 0x04://  0     1     0     0
		m_phase = 5;
		break;
		case 0x05://  0     1     0     1
		m_phase = 4;
		break;
		case 0x01://  0     0     0     1
		m_phase = 3;
		break;
		case 0x09://  1     0     0     1
		m_phase = 2;
		break;
		case 0x08://  1     0     0     0
		m_phase = 1;
		break;
		case 0x0A://  1     0     1     0
		m_phase = 0;
		break;
		//          Black  Blue  Red  Yellow
		case 0x03://  0     0     1     1
		{
			if ((m_old_phase ==6)||(m_old_phase == 0)) // if the previous pattern had the drum in the northern quadrant, it will point north now
			{
				m_phase = 7;
			}
			else //otherwise it will line up due south
			{
				m_phase = 3;
			}
		}
		break;
		case 0x0C://  1     1     0     0
		{
			if ((m_old_phase ==6)||(m_old_phase == 4)) // if the previous pattern had the drum in the eastern quadrant, it will point east now
			{
				m_phase = 5;
			}
			else //otherwise it will line up due west
			{
				m_phase = 1;
			}
		}
		break;
	}
}

///////////////////////////////////////////////////////////////////////////

void reel_device::advance_phase()
{
	switch (m_type)
	{
		default:
		logerror("No reel type specified!\n");
		break;
		case BASIC_STEPPER :
		case STARPOINT_48STEP_REEL : /* STARPOINT RMxxx */
		case GAMESMAN_200STEP_REEL : /* Gamesman GMxxxx */
		case STARPOINT_144STEP_DICE :/* STARPOINT 1DCU DICE mechanism */
		case STARPOINT_200STEP_REEL :
		case SYS5_100STEP_REEL :
		stepper_device::advance_phase();
		break;

		case BARCREST_48STEP_REEL :
		case GAMESMAN_48STEP_REEL :
		case GAMESMAN_100STEP_REEL :
		//Standard drive table is 1,3,2,6,4,C,8,9
		//Gamesman 48 step uses this pattern shifted one place forward, though this shouldn't matter
		switch (m_pattern)
		{
			//             Yellow   Brown  Orange Black
			case 0x01://  0        0      0      1
			m_phase = 7;
			break;
			case 0x03://  0        0      1      1
			m_phase = 6;
			break;
			case 0x02://  0        0      1      0
			m_phase = 5;
			break;
			case 0x06://  0        1      1      0
			m_phase = 4;
			break;
			case 0x04://  0        1      0      0
			m_phase = 3;
			break;
			case 0x0C://  1        1      0      0
			m_phase = 2;
			break;
			case 0x08://  1        0      0      0
			m_phase = 1;
			break;//YOLB
			case 0x09://  1        0      0      1
			m_phase = 0;
			break;

			// The below values should not be used by anything sane, as they effectively ignore one stator side entirely
			//          Yellow   Brown  Orange Black
			case 0x05://   0       1       0     1
			{
				if ((m_old_phase ==6)||(m_old_phase == 0)) // if the previous pattern had the drum in the northern quadrant, it will point north now
				{
					m_phase = 7;
				}
				else //otherwise it will line up due south
				{
					m_phase = 3;
				}
			}
			break;

			case 0x0A://   1       0       1     0
			{
				if ((m_old_phase ==6)||(m_old_phase == 4)) // if the previous pattern had the drum in the eastern quadrant, it will point east now
				{
					m_phase = 5;
				}
				else //otherwise it will line up due west
				{
					m_phase = 1;
				}
			}
			break;
		}
		break;

		case MPU3_48STEP_REEL :
		/* The MPU3 harness is actually the same as the MPU4 setup, but with two active lines instead of four, and a slight change to the windings.
		   Inverters are used so if a pin is low, the higher bit of the pair is activated, and if high the lower bit is activated.
		*/
		switch (m_pattern)
		{
		//             Grey(1)    Yellow(2)   Grey (2) Yellow (2)
			case 0x02 :// 0          1          0         1
			m_phase = 6;
			break;
			case 0x03 :// 0          1          1         0
			m_phase = 4;
			break;
			case 0x01 :// 1          0          1         0
			m_phase = 2;
			break;
			case 0x00 :// 1          0          0         1
			m_phase = 0;
			break;
		}
		break;

		case ECOIN_200STEP_REEL :
		//While the 48 and 100 step models appear to be reverse driven Starpoint reels, the 200 step model seems bespoke, certainly in terms of wiring.
		//On a Proconn machine this same pattern is seen but running in reverse
		//Standard drive table is 8,c,4,6,2,3,1,9
		switch (m_pattern)
		{
			case 0x08://  0     0     1     0
			m_phase = 7;
			break;
			case 0x0c://  0     1     1     0
			m_phase = 6;
			break;
			case 0x04://  0     1     0     0
			m_phase = 5;
			break;
			case 0x06://  0     1     0     1
			m_phase = 4;
			break;
			case 0x02://  0     0     0     1
			m_phase = 3;
			break;
			case 0x03://  1     0     0     1
			m_phase = 2;
			break;
			case 0x01://  1     0     0     0
			m_phase = 1;
			break;
			case 0x09://  1     0     1     0
			m_phase = 0;
			break;
			case 0x0a://  0     0     1     1
			{
				if ((m_old_phase ==6)||(m_old_phase == 0)) // if the previous pattern had the drum in the northern quadrant, it will point north now
				{
					m_phase = 7;
				}
				else //otherwise it will line up due south
				{
					m_phase = 3;
				}
			}
			break;
			case 0x07://  1     1     0     0
			{
				if ((m_old_phase ==6)||(m_old_phase == 4)) // if the previous pattern had the drum in the eastern quadrant, it will point east now
				{
					m_phase = 5;
				}
				else //otherwise it will line up due west
				{
					m_phase = 1;
				}
			}
			break;
		}
		break;

		case SRU_200STEP_REEL :
		//Standard drive table is 2,3,1,9,8,c,4,6
		//Starpoint mechanism, custom for JPM?
		switch (m_pattern)
		{
			case 0x02:
			m_phase = 7;
			break;
			case 0x03:
			m_phase = 6;
			break;
			case 0x01:
			m_phase = 5;
			break;
			case 0x09:
			m_phase = 4;
			break;
			case 0x08:
			m_phase = 3;
			break;
			case 0x0c:
			m_phase = 2;
			break;
			case 0x04:
			m_phase = 1;
			break;
			case 0x06:
			m_phase = 0;
			break;
		}
		break;

		case PROJECT_48STEP_REEL :
		//Standard drive table is 8,c,4,5,1,3,2,a
		//This appears to be basically a rewired Gamesman (the reel PCB looks like it does some shuffling)
		//TODO: Not sure if this should be represented as a type here, or by defining it as a Gamesman in the driver and bitswapping.
		switch (m_pattern)
		{
			case 0x08://  0     0     1     0
			m_phase = 7;
			break;
			case 0x0c://  0     1     1     0
			m_phase = 6;
			break;
			case 0x04://  0     1     0     0
			m_phase = 5;
			break;
			case 0x05://  0     1     0     1
			m_phase = 4;
			break;
			case 0x01://  0     0     0     1
			m_phase = 3;
			break;
			case 0x03://  1     0     0     1
			m_phase = 2;
			break;
			case 0x02://  1     0     0     0
			m_phase = 1;
			break;
			case 0x0a://  1     0     1     0
			m_phase = 0;
			break;
			case 0x09://  0     0     1     1
			{
				if ((m_old_phase ==6)||(m_old_phase == 0)) // if the previous pattern had the drum in the northern quadrant, it will point north now
				{
					m_phase = 7;
				}
				else //otherwise it will line up due south
				{
					m_phase = 3;
				}
			}
			break;
			case 0x06://  1     1     0     0
			{
				if ((m_old_phase ==6)||(m_old_phase == 4)) // if the previous pattern had the drum in the eastern quadrant, it will point east now
				{
					m_phase = 5;
				}
				else //otherwise it will line up due west
				{
					m_phase = 1;
				}
			}
			break;
		}
		break;
	}
}
///////////////////////////////////////////////////////////////////////////

reel_device::reel_device(const machine_config &mconfig, const char *tag, device_t *owner, uint8_t type, int16_t start_index, int16_t end_index
	, int16_t index_pattern, uint8_t init_phase, int16_t max_steps)
	: stepper_device(mconfig, tag, owner)
{
	set_reel_type(type);
	set_start_index(start_index);
	set_end_index(end_index);
	set_index_pattern(index_pattern);
	set_init_phase(init_phase);
	set_max_steps(max_steps);
}
///////////////////////////////////////////////////////////////////////////

reel_device::reel_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: stepper_device(mconfig, REEL, tag, owner, clock)
{
}

///////////////////////////////////////////////////////////////////////////

void reel_device::device_start()
{
	stepper_device::device_start();

	save_item(NAME(m_type));
}

///////////////////////////////////////////////////////////////////////////

stepper_device::acoustics &stepper_device::acoustic()
{
	if (!m_acoustics)
		m_acoustics = std::make_unique<acoustics>();

	return *m_acoustics;
}

stepper_device &stepper_device::set_motor_type(motor_type type)
{
	const motor_spec &m = MOTORS[type];
	acoustics &a = acoustic();

	a.inertia = m.inertia;
	a.torque = m.torque;
	a.rated_current = m.current;
	a.drive_current = m.current;
	a.inductance = m.inductance;
	a.supply = m.supply;
	a.teeth = m.teeth;
	a.rotor_q = m.q;
	a.rotor_gain = m.gain;

	return *this;
}

stepper_device &stepper_device::set_rotor(double inertia_kgm2, double holding_torque_nm, int rotor_teeth)
{
	acoustics &a = acoustic();

	a.inertia = inertia_kgm2;
	a.torque = holding_torque_nm;
	a.teeth = rotor_teeth;

	return *this;
}

stepper_device &stepper_device::set_rotor_damping(double q, double gain)
{
	acoustics &a = acoustic();

	a.rotor_q = q;
	a.rotor_gain = gain;

	return *this;
}

stepper_device &stepper_device::set_linear_load(double moving_kg, double mm_per_rev, int motors)
{
	const double r = (mm_per_rev / 1000.0) / (2.0 * M_PI);

	acoustic().load_inertia = (moving_kg * r * r) / std::max(motors, 1);

	return *this;
}

stepper_device &stepper_device::set_belt_load(double moving_kg, int pulley_teeth, double belt_pitch_mm, int motors)
{
	return set_linear_load(moving_kg, double(pulley_teeth) * belt_pitch_mm, motors);
}

stepper_device &stepper_device::set_screw_load(double moving_kg, double lead_mm, int motors)
{
	return set_linear_load(moving_kg, lead_mm, motors);
}

stepper_device &stepper_device::set_inertial_load(double extra_kgm2)
{
	acoustic().extra_inertia = extra_kgm2;

	return *this;
}

stepper_device &stepper_device::set_winding(double henries, double supply_volts)
{
	acoustics &a = acoustic();

	a.inductance = henries;
	a.supply = supply_volts;

	return *this;
}

stepper_device &stepper_device::set_drive_current(double amps)
{
	acoustics &a = acoustic();

	if (a.drive_current != amps)
	{
		if (m_stream)
			m_stream->update();

		a.drive_current = amps;
		a.prime = true;
	}

	return *this;
}

stepper_device &stepper_device::set_mode(int index, double hz, double q, double gain)
{
	acoustics &a = acoustic();

	if (unsigned(index) < ACOUSTIC_MODES)
	{
		a.mode[index].hz = hz;
		a.mode[index].q = q;
		a.mode[index].gain = gain;
		a.mode[index].pinned = true;
		a.tuned_at = -1.0;
	}

	return *this;
}

stepper_device &stepper_device::set_radiation(double fraction)
{
	acoustic().radiation = fraction;

	return *this;
}

double stepper_device::resonance_hz() const
{
	if (!m_acoustics)
		return 0.0;

	const acoustics &a = *m_acoustics;
	const double j = a.inertia + a.load_inertia + a.extra_inertia;
	const double th = a.torque * (a.drive_current / a.rated_current);

	if ((j <= 0.0) || (th <= 0.0))
		return 0.0;

	return std::sqrt(double(a.teeth) * th / j) / (2.0 * M_PI);
}

///////////////////////////////////////////////////////////////////////////

void stepper_device::set_coil_currents(double ia, double ib)
{
	if (!m_acoustics)
		return;

	acoustics &a = *m_acoustics;
	const uint32_t next = (a.head + 1) % QUEUE;

	if (next == a.tail)
	{
		a.tail = (a.tail + 1) % QUEUE;
		if (!a.dropped++)
			logerror("step queue overflowed; sound will be thin until the stream catches up\n");
	}

	a.when[a.head] = machine().time().as_double();
	a.ia[a.head] = ia;
	a.ib[a.head] = ib;
	a.head = next;
}

void stepper_device::set_holding(bool energised, double ia, double ib)
{
	if (!m_acoustics)
		return;

	acoustics &a = *m_acoustics;

	if (a.energised != energised)
	{
		if (m_stream)
			m_stream->update();

		a.energised = energised;
		a.prime = true;
		a.thunk += DETENT_SNAP * a.drive_current;
	}

	if (a.tail == a.head)
	{
		const double want_ia = energised ? ia : 0.0;
		const double want_ib = energised ? ib : 0.0;
		const double had = a.cmd_ia * a.cmd_ia + a.cmd_ib * a.cmd_ib;
		const double gets = want_ia * want_ia + want_ib * want_ib;

		if (std::abs(gets - had) > REGIME_CHANGE * std::max(had, gets))
			a.prime = true;

		a.cmd_ia = want_ia;
		a.cmd_ib = want_ib;
	}
}

void stepper_device::excite_phase(bool moved)
{
	if (!m_pattern)
	{
		set_holding(false, 0.0, 0.0);
		return;
	}

	const double current = m_acoustics->drive_current;
	const double ia = PHASE_IA[m_phase & 7] * current;
	const double ib = PHASE_IB[m_phase & 7] * current;

	if (moved)
		set_coil_currents(ia, ib);

	set_holding(true, ia, ib);
}

///////////////////////////////////////////////////////////////////////////

void stepper_device::retune(double rate)
{
	acoustics &a = *m_acoustics;

	for (int i = 0; i < ACOUSTIC_MODES; i++)
	{
		const bool rotor = (i == 0) && !a.mode[0].pinned;
		const double hz = rotor ? resonance_hz() : a.mode[i].hz;
		const double q = rotor ? a.rotor_q : a.mode[i].q;

		a.active_gain[i] = ((hz > 0.0) && (q > 0.0)) ? (rotor ? a.rotor_gain : a.mode[i].gain) : 0.0;
		if (a.active_gain[i] != 0.0)
			a.filter[i].configure(hz, q, rate);
	}

	a.radiate.configure(RADIATE_HZ, rate);
	a.tuned_at = a.drive_current;
}

void stepper_device::sound_stream_update(sound_stream &stream)
{
	if (!m_acoustics)
		return;

	acoustics &a = *m_acoustics;

	double ringing = std::abs(a.radiate.y);
	for (int k = 0; k < ACOUSTIC_MODES; k++)
		ringing += std::abs(a.filter[k].y1);

	const double settling = std::abs(a.cmd_ia - a.act_ia) + std::abs(a.cmd_ib - a.act_ib);
	if ((a.tail == a.head) && (a.thunk == 0.0) && (settling < 1e-6) && (ringing < SILENT_BELOW))
		return;

	const int samples = stream.samples();
	const double rate = stream.sample_rate();
	const double t0 = stream.start_time().as_double();

	if (a.drive_current != a.tuned_at)
		retune(rate);

	for (int s = 0; s < samples; s++)
	{
		const double opens = t0 + double(s) / rate;
		const double closes = opens + 1.0 / rate;

		double area = 0.0;
		double covered = 0.0;
		double impulse = a.carry;
		a.carry = 0.0;

		while (a.tail != a.head && a.when[a.tail] < closes)
		{
			const double at = std::clamp((a.when[a.tail] - opens) * rate, covered, 1.0);

			a.slew(a.when[a.tail] - a.last_slew);
			a.last_slew = a.when[a.tail];

			area += a.radial * (at - covered);
			covered = at;

			const double jump_ia = a.ia[a.tail] - a.cmd_ia;
			const double jump_ib = a.ib[a.tail] - a.cmd_ib;

			const double had = a.cmd_ia * a.cmd_ia + a.cmd_ib * a.cmd_ib;
			const double gets = a.ia[a.tail] * a.ia[a.tail] + a.ib[a.tail] * a.ib[a.tail];
			if (std::abs(gets - had) > REGIME_CHANGE * std::max(had, gets))
				a.prime = true;

			a.cmd_ia = a.ia[a.tail];
			a.cmd_ib = a.ib[a.tail];

			const double amp = TANGENTIAL * std::sqrt(jump_ia * jump_ia + jump_ib * jump_ib);
			impulse += amp * (1.0 - at);
			a.carry += amp * at;

			a.tail = (a.tail + 1) % QUEUE;
		}

		a.slew(closes - a.last_slew);
		a.last_slew = closes;
		area += a.radial * (1.0 - covered);

		const double excite = area * RADIAL + impulse;

		if (a.prime)
		{
			a.dc_x1 = excite;
			a.dc_y1 = 0.0;

			a.prime = (std::abs(a.cmd_ia - a.act_ia) + std::abs(a.cmd_ib - a.act_ib)) > 1e-6;
		}

		const double hp = excite - a.dc_x1 + 0.999 * a.dc_y1;
		a.dc_x1 = excite;
		a.dc_y1 = hp;

		const double drive = hp + a.thunk;
		a.thunk = 0.0;

		double out = a.radiation * a.radiate.run(drive);
		for (int k = 0; k < ACOUSTIC_MODES; k++)
			if (a.active_gain[k] != 0.0)
				out += a.active_gain[k] * a.filter[k].run(drive);

		stream.put(0, s, float(CEILING * std::tanh(out * VOICE_GAIN / CEILING)));
	}
}
