// license:BSD-3-Clause
// copyright-holders:Ryan Holtz

/***************************************************************************

  Netlist (gtrak10) included from atarittl.cpp

***************************************************************************/

#include "netlist/devices/net_lib.h"

NETLIST_START(gtrak10)

	SOLVER(Solver, 48000)
	PARAM(Solver.PARALLEL, 0) // Don't do parallel solvers
	PARAM(Solver.ACCURACY, 1e-4) // works and is sufficient
	PARAM(NETLIST.USE_DEACTIVATE, 1)

	ANALOG_INPUT(V5, 5)

	TTL_INPUT(high, 1)
	TTL_INPUT(low, 0)

	MAINCLOCK(CLOCK, 14318181)

	ALIAS(P, high)
	ALIAS(GROUND, low)

	// Horizontal Counters
	TTL_9316(L2, CLOCK,     P, P, H1_1.Q, P, GROUND, GROUND, GROUND, GROUND)
	TTL_9316(K2, CLOCK, L2.RC, P, H1_1.Q, P, GROUND, GROUND, GROUND, GROUND)
	ALIAS(  1H, L2.QA)
	ALIAS(  2H, L2.QB)
	ALIAS(  4H, L2.QC)
	ALIAS(  8H, L2.QD)
	ALIAS( 16H, K2.QA)
	ALIAS( 32H, K2.QB)
	ALIAS( 64H, K2.QC)
	ALIAS(128H, K2.QD)
	TTL_74107(K1_1, K2.RC, P, P, P)
	TTL_74107(K1_2,  256H, P, P, P)
	ALIAS(256H,  K1_1.Q)
	ALIAS(512H,  K1_2.Q)
	ALIAS(512H_Q, K1_2.QQ)

	// Horizontal Reset
	TTL_7408_AND(E2_1, 2H, 64H)
	ALIAS(2H_AND_64H, E2_1.Q)
	TTL_7410_NAND(J1_3, 2H_AND_64H, 128H, 256H) //450H
	TTL_7474(H1_1, CLOCK, J1_3.Q, P, P)
	ALIAS(HRESET_Q, H1_1.Q)
	ALIAS(HRESET, H1_1.QQ)

	// Vertical Counters
	TTL_7493(F3, HBLANK, 1V, VRESET, VRESET)
	TTL_7493(H2, 8V, 16V, VRESET, VRESET)
	TTL_7493(I2, 128V, 256V, VRESET, VRESET)
	ALIAS(  1V, F3.QA)
	ALIAS(  2V, F3.QB)
	ALIAS(  4V, F3.QC)
	ALIAS(  8V, F3.QD)
	ALIAS( 16V, H2.QA)
	ALIAS( 32V, H2.QB)
	ALIAS( 64V, H2.QC)
	ALIAS(128V, H2.QD)
	ALIAS(256V, I2.QA)
	ALIAS(512V, I2.QB)

	// Vertical Reset
	TTL_7408_AND(E2_2, 512V, 8V)
	ALIAS(8V_AND_512V, E2_2.Q) //520V
	TTL_7474(H1_4, HRESET_Q, 8V_AND_512V, P, P)
	ALIAS(VRESET, H1_4.Q)
	ALIAS(VRESET_Q, H1_4.QQ)

	// Horizontal Blank Flip-Flop:
	TTL_7402_NOR(F1_3, 32H, F1_2.Q)
	TTL_7402_NOR(F1_2, HRESET, F1_3.Q)
	ALIAS(HBLANK, F1_3.Q)
	ALIAS(HBLANK_Q, F1_2.Q)

	// Vertical Sync Flip-Flop:
	TTL_7402_NOR(F1_4, 8V, F1_1.Q)
	TTL_7402_NOR(F1_1, VRESET, F1_4.Q)
	ALIAS(VSYNC, F1_4.Q)
	ALIAS(VSYNC_Q, F1_1.Q)

	// Horizontal Sync:
	TTL_7410_NAND(J1_1, HBLANK, HRESET_Q, 512H_Q)
	ALIAS(HSYNC_Q, J1_1.Q)

	// Composite Sync:
	TTL_7486_XOR(D2_1, HSYNC_Q, VSYNC)
	ALIAS(COMP_SYNC, D2_1.Q)
	TTL_7404_INVERT(C2_3, COMP_SYNC)
	ALIAS(COMP_SYNC_Q, C2_3.Q)

        // ------------- Video Mixer -----------------
        //TTL_7486_XOR(CHECKERS, 16H, 16V)
        TTL_7408_AND(CHECKERS, 16H, 16V)
	ALIAS(CAR1VIDEO, CHECKERS.Q)

	RES(R63, RES_K(1))
	RES(R73, RES_K(0.330))
	RES(R64, RES_K(0.330))
	//RES(R74, RES_K(0.330))
	//RES(R66, RES_K(0.330))
	//RES(R65, RES_K(4.7))

	NET_C(V5, R63.1)
	NET_C(COMP_SYNC, R73.1)
	NET_C(CAR1VIDEO, R64.1)
	//NET_C(FINISH_LINE, R74.1)
	//NET_C(RT, R66.1)
	//NET_C(SLICK_Q, R65.1)

	CAP(C44, CAP_U(10))
	NET_C(C44.1, R63.2)
	NET_C(C44.1, R73.2)
	NET_C(C44.1, R64.2)
	//NET_C(C44.1, R74.2)
	//NET_C(C44.1, R66.2)
	//NET_C(C44.1, R65.2)
	ALIAS(VIDEO_OUT, C44.2)

NETLIST_END()
