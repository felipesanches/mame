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
	//     name,   CLK,   ENP, ENT,     CLRQ, LOADQ,      A,      B,      C,      D
	TTL_9316(L2, CLOCK,     P,   P, HRESET_Q,     P, GROUND, GROUND, GROUND, GROUND)
	TTL_9316(K2, CLOCK, L2.RC,   P, HRESET_Q,     P, GROUND, GROUND, GROUND, GROUND)
	//        name,   CLK, J, K, CLRQ
	TTL_74107(K1_A, K2.RC, P, P,    P)
	TTL_74107(K1_B,  256H, P, P,    P)
	TTL_7404_INVERT(unlabeled_not, 16H)
	ALIAS(  1H,   L2.QA)
	ALIAS(  2H,   L2.QB)
	ALIAS(  4H,   L2.QC)
	ALIAS(  8H,   L2.QD)
	ALIAS( 16H,   K2.QA)
	ALIAS( 16H_Q, unlabeled_not.Q)
	ALIAS( 32H,   K2.QB)
	ALIAS( 64H,   K2.QC)
	ALIAS(128H,   K2.QD)
	ALIAS(256H,   K1_A.Q)
	ALIAS(512H,   K1_B.Q)
	ALIAS(512H_Q, K1_B.QQ)

	// Horizontal Reset
	TTL_7408_AND(E2_A, 2H, 64H)
	ALIAS(2H_AND_64H, E2_A.Q)
	TTL_7410_NAND(J1_C, 2H_AND_64H, 128H, 256H) // 450H
	//       name,   CLK,      D, CLRQ, PREQ
	TTL_7474(H1_A, CLOCK, J1_C.Q,    P,    P)
	ALIAS(HRESET_Q, H1_A.Q)
	ALIAS(HRESET,   H1_A.QQ)

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
	TTL_7408_AND(E2_B, 512V, 8V)
	ALIAS(8V_AND_512V, E2_B.Q) //520V
	TTL_7474(H1_D, HRESET_Q, 8V_AND_512V, P, P)
	ALIAS(VRESET, H1_D.Q)
	ALIAS(VRESET_Q, H1_D.QQ)

	// Horizontal Blank Flip-Flop:
	TTL_7402_NOR(F1_C, 32H, HBLANK_Q)
	TTL_7402_NOR(F1_B, HRESET, HBLANK)
	ALIAS(HBLANK, F1_C.Q)
	ALIAS(HBLANK_Q, F1_B.Q)

	// Vertical Sync Flip-Flop:
	TTL_7402_NOR(F1_D, 8V, VSYNC_Q)
	TTL_7402_NOR(F1_A, VRESET, VSYNC)
	ALIAS(VSYNC,   F1_D.Q)
	ALIAS(VSYNC_Q, F1_A.Q)

	// Horizontal Sync = HBLANK & /HRESET & /512H
	TTL_7410_NAND(J1_A, HBLANK, HRESET_Q, 512H_Q)
	TTL_7404_INVERT(E1_E, HSYNC_Q)
	ALIAS(HSYNC,   E1_E.Q)
	ALIAS(HSYNC_Q, J1_A.Q)

	// Composite Sync = /HSYNC XOR /VSYNC
	TTL_7486_XOR(D2_A, HSYNC_Q, VSYNC)
	TTL_7404_INVERT(C2_C, COMP_SYNC)
	ALIAS(COMP_SYNC,   D2_A.Q)
	ALIAS(COMP_SYNC_Q, C2_C.Q)

	// VLd1 signal (vertical load one)
	// Used to address the ROM for the vertical positioning of the car image
	//       name,      CLK,      D, CLRQ,   PREQ
	TTL_7474(B1_B, VRESET_Q, GROUND,    P, B1_A.Q)
	TTL_7474(B1_A,    HSYNC, B1_B.Q,    P,      P)
	ALIAS(VLd1_Q, B1_A.Q)
	ALIAS(VLd1,   B1_A.QQ)

	// Ld1B_Q signal (load one B):
	//       name,   CLK, D,    CLRQ,    PREQ
	TTL_7474(D1_B, 16H_Q, P, HSYNC_Q, VSYNC_Q)
	ALIAS(Ld1B_Q, D1_B.Q)

	// LD1B signal:
	TTL_7404_INVERT(E1_D, Ld1B_Q)
	ALIAS(Ld1B, E1_D.Q)

	// Actual ROM chip is labeled 74186:
	//       name,  GQ, EPQ,  A0,  A1,  A2,  A3,  A4,  A5,  A6,  A7,  A8,  A9, A10
	EPROM_2716(J5, low, low, low, low, low, low, low, low, low, low, low, low, low)
	PARAM(J5.ROM, "racetrack")

	// Vertical positioning of the car:
	//     name,     EQ, MRQ,    S0Q,    S1Q,    S2Q,    S3Q,    D0,    D1,    D2,    D3
	TTL_9314(H7, VLd1_Q,   P, GROUND, GROUND, GROUND, GROUND, J5.D4, J5.D5, J5.D6, J5.D7)

        //      name,   CLK, CLKINH,  SH_LDQ,   SER,     A,     B,     C,     D,     E,     F,     G,     H
	TTL_74165(H6, CLOCK,      P, HSYNC_Q,     P, J5.D0, J5.D1, J5.D2, J5.D3, J5.D4, J5.D5, J5.D6, J5.D7)
	TTL_74165(F6, CLOCK,      P,  Ld1B_Q, H6.QH, J5.D0, J5.D1, J5.D2, J5.D3, J5.D4, J5.D5, J5.D6, J5.D7)
	//TTL_74165(H6, CLOCK, P, HSYNC_Q,     P,     P, G, G, P,   G, G, G, P) // '.--.---.----...-'
	//TTL_74165(F6, CLOCK, P,  Ld1B_Q, H6.QH,     G, G, G, G,   P, P, P, G)
	ALIAS(CAR1VIDEO, F6.QHQ)
	//ALIAS(G, GROUND)

	// --------------- Hack ----------------------
	// This is a signal only used for debugging.
	// It generats a pattern of squares on the screen.
	// CAR1VIDEO = not(16H & 16V | VBLANK | HBLANK)
	#define SIGNAL_RT 1
        #if SIGNAL_RT
	TTL_7408_AND(16H_and_16V, 16H, 16V)
//	TTL_7486_XOR(16H_xor_16V, 16H, 16V)
        TTL_7432_OR(BLANK, HBLANK, VRESET)
        TTL_7402_NOR(nor, 16H_and_16V, BLANK)
	ALIAS(RT, nor.Q)
	#endif

        // ------------- Video Mixer -----------------
	RES(R63, RES_K(1))
	RES(R73, RES_K(0.330))
	RES(R64, RES_K(0.330))
	//RES(R74, RES_K(0.330))
#if SIGNAL_RT
	RES(R66, RES_K(0.330))
#endif
	//RES(R65, RES_K(4.7))

	NET_C(V5, R63.1)
	NET_C(COMP_SYNC, R73.1)
	NET_C(CAR1VIDEO, R64.1)
	//NET_C(FINISH_LINE, R74.1)
#if SIGNAL_RT
	NET_C(RT, R66.1)
#endif
	//NET_C(SLICK_Q, R65.1)

	CAP(C44, CAP_U(10))
	NET_C(C44.1, R63.2)
	NET_C(C44.1, R73.2)
	NET_C(C44.1, R64.2)
	//NET_C(C44.1, R74.2)
#if SIGNAL_RT
	NET_C(C44.1, R66.2)
#endif
	//NET_C(C44.1, R65.2)
	ALIAS(VIDEO_OUT, C44.2)

NETLIST_END()
