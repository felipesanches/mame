// license:BSD-3-Clause
// copyright-holders:James Wallace
///////////////////////////////////////////////////////////////////////////
//                                                                       //
// steppers.cpp steppermotor emulation                                   //
//                                                                       //
// Emulates : stepper motors driven with full step or half step          //
//            also emulates the index optic                              //
//            optionally models the noise the motor makes                //
//                                                                       //
// TODO:  add further types of stepper motors if needed (Konami/IGT?)    //
///////////////////////////////////////////////////////////////////////////


#ifndef MAME_MACHINE_STEPPERS_H
#define MAME_MACHINE_STEPPERS_H

#pragma once

#include <memory>

#define BASIC_STEPPER           0
#define STARPOINT_48STEP_REEL   1           /* STARPOINT RMXXX reel unit */
#define STARPOINT_144STEP_DICE  2           /* STARPOINT 1DCU DICE mechanism */
#define STARPOINT_200STEP_REEL  3

#define BARCREST_48STEP_REEL    4           /* Barcrest bespoke reel unit */
#define MPU3_48STEP_REEL        5

#define ECOIN_200STEP_REEL      6           /* Probably not bespoke, but can't find a part number */

#define GAMESMAN_48STEP_REEL    7
#define GAMESMAN_100STEP_REEL   8
#define GAMESMAN_200STEP_REEL   9

#define PROJECT_48STEP_REEL     10

#define SRU_200STEP_REEL        11

#define SYS5_100STEP_REEL       12


class stepper_device : public device_t, public device_sound_interface
{
public:
	enum motor_type : int
	{
		MOTOR_NEMA17 = 0,
		MOTOR_REEL_48STEP,
		MOTOR_REEL_200STEP,
		MOTOR_TYPES
	};

	static constexpr int ACOUSTIC_MODES = 4;

	stepper_device(const machine_config &mconfig, const char *tag, device_t *owner, uint8_t init_phase)
		: stepper_device(mconfig, tag, owner, (uint32_t)0)
	{
		set_init_phase(init_phase);
	}

	stepper_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
	virtual ~stepper_device();

	auto optic_handler() { return m_optic_cb.bind(); }

	/* total size of reel (in half steps) */
	void set_max_steps(int16_t steps) { m_max_steps = steps; }

	/* start position of index (in half steps) */
	void set_start_index(int16_t index) { m_index_start = index; }

	/* end position of index (in half steps) */
	void set_end_index(int16_t index) { m_index_end = index; }

	/* end position of index (in half steps) */
	void set_index_pattern(int16_t index) { m_index_patt = index; }

	/* Phase at 0, for opto linkage */
	void set_init_phase(uint8_t phase) { m_initphase = phase; m_phase = phase; m_old_phase = phase; }

	/* update a motor */
	int update(uint8_t pattern);

	/* get current position in half steps */
	int get_position()          { return m_step_pos; }
	/* get current absolute position in half steps */
	int get_absolute_position() { return m_abs_step_pos; }
	/* set absolute position in half steps */
	void set_absolute_position(int pos) { m_abs_step_pos = pos; }
	/* get maximum position in half steps */
	int get_max()               { return m_max_steps; }

	/* acoustics: the rotor */
	stepper_device &set_motor_type(motor_type type);
	stepper_device &set_rotor(double inertia_kgm2, double holding_torque_nm, int rotor_teeth = 50);
	stepper_device &set_rotor_damping(double q, double gain);

	/* acoustics: what the shaft drives */
	stepper_device &set_linear_load(double moving_kg, double mm_per_rev, int motors = 1);
	stepper_device &set_belt_load(double moving_kg, int pulley_teeth, double belt_pitch_mm, int motors = 1);
	stepper_device &set_screw_load(double moving_kg, double lead_mm, int motors = 1);
	stepper_device &set_inertial_load(double extra_kgm2);

	/* acoustics: electrical */
	stepper_device &set_winding(double henries, double supply_volts);
	stepper_device &set_drive_current(double amps);

	/* acoustics: measured override */
	stepper_device &set_mode(int index, double hz, double q, double gain);
	stepper_device &set_radiation(double fraction);

	double resonance_hz() const;

	/* acoustics: excitation */
	void set_coil_currents(double ia, double ib);
	void set_holding(bool energised, double ia, double ib);

protected:
	stepper_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock = 0);

	// device-level overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_post_load() override;
	virtual void sound_stream_update(sound_stream &stream) override;

	uint8_t m_pattern;      /* coil pattern */
	uint8_t m_old_pattern;  /* old coil pattern */
	uint8_t m_initphase;
	uint8_t m_phase;        /* motor phase */
	uint8_t m_old_phase;    /* old phase */
	int16_t m_step_pos;     /* step position 0 - max_steps */
	int16_t m_max_steps;    /* maximum step position */
	int32_t m_abs_step_pos; /* absolute step position */
	int16_t m_index_start;  /* start position of index (in half steps) */
	int16_t m_index_end;    /* end position of index (in half steps) */
	int16_t m_index_patt;   /* pattern needed on coils (0=don't care) */
	uint8_t m_optic;

	void update_optic();
	virtual void advance_phase();
	devcb_write_line m_optic_cb;

private:
	struct acoustics;

	acoustics &acoustic();
	void excite_phase(bool moved);
	void retune(double rate);

	std::unique_ptr<acoustics> m_acoustics;
	sound_stream *m_stream;
};

class reel_device : public stepper_device
{
public:
	reel_device(const machine_config &mconfig, const char *tag, device_t *owner, uint8_t type, int16_t start_index, int16_t end_index
		, int16_t index_pattern, uint8_t init_phase, int16_t max_steps = 48*2);

	reel_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void advance_phase() override;

	void set_reel_type(uint8_t type)
	{
		m_type = type;
		switch ( type )
		{
		default:
		case STARPOINT_48STEP_REEL:  /* STARPOINT RMxxx */
		case BARCREST_48STEP_REEL :  /* Barcrest Reel unit */
		case MPU3_48STEP_REEL :
		case GAMESMAN_48STEP_REEL :  /* Gamesman GMxxxx */
		case PROJECT_48STEP_REEL :
			m_max_steps = (48*2);
			break;
		case GAMESMAN_100STEP_REEL :
			m_max_steps = (100*2);
			break;
		case STARPOINT_144STEP_DICE :/* STARPOINT 1DCU DICE mechanism */
			//Dice reels are 48 step motors, but complete three full cycles between opto updates
			m_max_steps = ((48*3)*2);
			break;
		case STARPOINT_200STEP_REEL :
		case GAMESMAN_200STEP_REEL :
		case ECOIN_200STEP_REEL :
		case SRU_200STEP_REEL :
			m_max_steps = (200*2);
			break;
		}
	}

	uint8_t m_type;         /* reel type */
};

DECLARE_DEVICE_TYPE(STEPPER, stepper_device)
DECLARE_DEVICE_TYPE(REEL, reel_device)

#endif // MAME_MACHINE_STEPPERS_H
