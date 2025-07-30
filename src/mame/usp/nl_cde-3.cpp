// license:BSD-3-Clause
// copyright-holders:Felipe Sanches

/***************************************************************************

  Netlist for the CDE-3 board of the Patinho Feio computer.
  Included from patinhofeio.cpp

***************************************************************************/

#include "netlist/devices/net_lib.h"

// This PCB is called CDE-3:
// - C stands for "Control" Unit.
// - DE is for the Instruction "Decoder".
// - The number 3 denotes it is the third board in
//   the set of 8 PCBs that implement the control unit.

NETLIST_START(cde3)
{
// Note: I am unsure about how to properly setup
//       the values in this entire preamble:
// =============================================

	// cribbed parameters from Tank
	SOLVER(Solver, 48000)
	PARAM(Solver.ACCURACY, 1e-5)
	PARAM(Solver.DYNAMIC_TS, 1)
	PARAM(Solver.DYNAMIC_LTE, 1e-2)
	PARAM(NETLIST.USE_DEACTIVATE, 1)

	ANALOG_INPUT(V5, 5)
	ALIAS(VCC, V5)

	TTL_INPUT(high, 1)
	TTL_INPUT(low, 0)

	NET_C(VCC, high.VCC, low.VCC)
	NET_C(GND, high.GND, low.GND)

	ALIAS(GROUND, low)
// =============================================


	TTL_7404_INVERT(2A_P10A, RD_0)
	ALIAS(_RD_0, 2A_P10A.Q)

	TTL_7404_INVERT(2A_P10F, RD_1)
	ALIAS(_RD_1, 2A_P10F.Q)

	TTL_7404_INVERT(2B_P10C, RD_2)
	ALIAS(_RD_2, 2B_P10C.Q)

	TTL_7404_INVERT(2B_P10B, RD_3)
	ALIAS(_RD_3, 2B_P10B.Q)

	TTL_7404_INVERT(2B_P10A, RD_4)
	ALIAS(_RD_4, 2B_P10A.Q)

	TTL_7404_INVERT(2B_P10D, RD_5)
	ALIAS(_RD_5, 2B_P10D.Q)

	TTL_7404_INVERT(2B_P10E, RD_6)
	ALIAS(_RD_6, 2B_P10E.Q)

	TTL_7404_INVERT(2B_P10F, RD_7)
	ALIAS(_RD_7, 2B_P10F.Q)


	TTL_7404_INVERT(1A_P10D, RI_0)
	TTL_7404_INVERT(1A_P10E, 1A_P10D.Q)
	ALIAS(_RI_0, 1A_P10D.Q)
	ALIAS(pRI_0, 1A_P10E.Q)

	TTL_7404_INVERT(1A_P10C, RI_1)
	TTL_7404_INVERT(1A_P10B, 1A_P10C.Q)
	ALIAS(_RI_1, 1A_P10C.Q)
	ALIAS(pRI_1, 1A_P10B.Q)

	TTL_7404_INVERT(1A_P10A, RI_2)
	TTL_7404_INVERT(1A_P10F, 1A_P10A.Q)
	ALIAS(_RI_2, 1A_P10A.Q)
	ALIAS(pRI_2, 1A_P10F.Q)

	TTL_7404_INVERT(1B_P10D, RI_3)
	TTL_7404_INVERT(1B_P10E, 1B_P10E.Q)
	ALIAS(_RI_3, 1B_P10D.Q)
	ALIAS(pRI_3, 1B_P10E.Q)

	TTL_7404_INVERT(1B_P10C, RI_4)
	TTL_7404_INVERT(1B_P10B, 1B_P10C.Q)
	ALIAS(_RI_4, 1B_P10C.Q)
	ALIAS(pRI_4, 1B_P10B.Q)

	TTL_7404_INVERT(1B_P10A, RI_5)
	TTL_7404_INVERT(1B_P10F, 1B_P10A.Q)
	ALIAS(_RI_5, 1B_P10A.Q)
	ALIAS(pRI_5, 1B_P10F.Q)

	TTL_7404_INVERT(2A_P10D, RI_6)
	TTL_7404_INVERT(2A_P10E, 2A_P10D.Q)
	ALIAS(_RI_6, 2A_P10D.Q)
	ALIAS(pRI_6, 2A_P10E.Q)

	TTL_7404_INVERT(2A_P10C, RI_7)
	TTL_7404_INVERT(2A_P10B, 2A_P10C.Q)
	ALIAS(_RI_7, 2A_P10C.Q)
	ALIAS(pRI_7, 2A_P10B.Q)


	TTL_7400_NAND(4D_P10A, _RI_0, pRI_3)
	TTL_7400_NAND(4D_P10B, _RI_1, _RI_0)
	TTL_7400_NAND(4D_P10D, pRI_1, _RI_0)

	// Instrução Indexada:
	// RI & 0x90 == 0x10
	TTL_7404_INVERT(3A_P10A, 4D_P10A.Q)	
	ALIAS(INDICE, 3A_P10A.Q)

	// Pula Incondicional: 
	// RI & 0xE0 == 0x00
	TTL_7402_NOR(4C_P10A, pRI_2, 4D_P10B.Q)
	ALIAS(PLI, 4C_P10A.Q)

	// Armazena: 
	// RI & 0xE0 == 0x20
	TTL_7402_NOR(4C_P10B, _RI_2, 4D_P10B.Q)
	ALIAS(ARA, 4C_P10B.Q)

	// Carrega: 
	// RI & 0xE0 == 0x40
	TTL_7402_NOR(4C_P10D, pRI_2, 4D_P10D.Q)
	ALIAS(CAC, 4C_P10D.Q)

	// Soma: 
	// RI & 0xE0 == 0x60
	TTL_7402_NOR(4C_P10C, 4D_P10D.Q, _RI_2)
	ALIAS(SOM, 4C_P10C.Q)

	// CAC_ou_SOM
	TTL_7404_INVERT(3A_P10B, 4D_P10D.Q)	
	ALIAS(CAR_ou_SOM, 3A_P10B.Q)

	// PLI_ou_ARA
	TTL_7404_INVERT(3A_P10C, 4D_P10B.Q)	
	ALIAS(PLI_ou_ARA, 3A_P10C.Q)


	TTL_7410_NAND(1C_P10A, pRI_0, pRI_1, pRI_2)
	TTL_7410_NAND(1C_P10B, pRI_0, _RI_1, pRI_2)

	// Subtrai um ou Salta: 
	// RI & 0xF0 == 0xE0
	TTL_7402_NOR(5C_P10A, pRI_3, 1C_P10A.Q)
	ALIAS(SUS, 5C_P10A.Q)

	// Pula e Guarda: 
	// RI & 0xF0 == 0xF0
	TTL_7402_NOR(5C_P10B, _RI_3, 1C_P10A.Q)
	ALIAS(PUG, 5C_P10B.Q)

	// Pula se Negativo: 
	// RI & 0xF0 == 0xA0
	TTL_7402_NOR(5C_P10D, pRI_3, 1C_P10B.Q)
	ALIAS(PAN, 5C_P10D.Q)

	// Pula se Zero: 
	// RI & 0xF0 == 0xB0
	TTL_7402_NOR(5C_P10C, _RI_3, 1C_P10B.Q)
	ALIAS(PAZ, 5C_P10C.Q)

	// SUS_ou_PUG
	TTL_7404_INVERT(3A_P10F, 1C_P10B.Q)	
	ALIAS(SUS_ou_PUG, 3A_P10F.Q)

	// PAN_ou_PAZ
	TTL_7404_INVERT(3A_P10E, 1C_P10A.Q)	
	ALIAS(PAN_ou_PAZ, 3A_P10E.Q)

	// RD is zero:
	TTL_7430_NAND(2C_P10, _RD_0, _RD_1, _RD_2, _RD_3, _RD_4, _RD_5, _RD_6, _RD_7)
	ALIAS(_RDZ, 2C_P10.Q)


	TTL_7413_NAND(2E_P10A, pRI_0, pRI_1, _RI_2, pRI_3)

	// Soma imediata: 
	// IME && (RI & 0x08 == 0x08)
	TTL_7402_NOR(5B_P10A, _RI_4, 2E_P10A.Q)
	ALIAS(IMEADD, 5B_P10A.Q)

	// NAND imediato: 
	// IME && (RI & 0x04 == 0x04)
	TTL_7402_NOR(5B_P10B, _RI_5, 2E_P10A.Q)
	ALIAS(IMENAND, 5B_P10B.Q)

	// Exclusive-OR imediato: 
	// IME && (RI & 0x02 == 0x02)
	TTL_7402_NOR(5B_P10D, _RI_6, 2E_P10A.Q)
	ALIAS(IMEXOR, 5B_P10D.Q)

	// Instruções de deslocamento: 
	// IME && (RI & 0x01 == 0x01)
	TTL_7402_NOR(5B_P10C, _RI_7, 2E_P10A.Q)
	ALIAS(SHIFT, 5B_P10C.Q)

	// Instruções imediatas: 
	// RI & 0xF0 == 0xD0
	TTL_7404_INVERT(3A_P10D, 2E_P10A.Q)	
	ALIAS(IME, 3A_P10D.Q)


	TTL_7413_NAND(2E_P10B, pRI_0, pRI_1, _RI_2, _RI_3)

	// Saída de Dados:
	// SAE && (RD & 0x80)
	TTL_7402_NOR(4B_P10A, _RD_0, 2E_P10B.Q)
	ALIAS(SAEDS, 4B_P10A.Q)

	// Entrada de Dados:
	// SAE && (RD & 0x40)
	TTL_7402_NOR(4B_P10B, _RD_1, 2E_P10B.Q)
	ALIAS(SAEDE, 4B_P10B.Q)

	// Salto:
	// SAE && (RD & 0x20)
	TTL_7402_NOR(4B_P10D, _RD_2, 2E_P10B.Q)
	ALIAS(SAESA, 4B_P10D.Q)

	// Função:
	// SAE && (RD & 0x10)
	TTL_7402_NOR(4B_P10C, _RD_3, 2E_P10B.Q)
	ALIAS(SAEFU, 4B_P10C.Q)

	// Instruções de I/O:
	// SAE = ((RI & 0xF0) == 0xC0)
	TTL_7404_INVERT(3B_P10A, 2E_P10B.Q)	
	ALIAS(SAE, 3B_P10A.Q)


	// Observação:
	// Os códigos de 3 bits para as instruções
	// abaixo divergem dos valores listados na
	// tabela 5-6 de FREGNI/LANGDON

	TTL_7410_NAND(2D_P10C, pRI_0, _RI_1, _RI_2)

	TTL_7410_NAND(1D_P10A, MIC, pRI_3, pRI_4)
	ALIAS(_MICG2B, 1D_P10A.Q)

	TTL_7410_NAND(1D_P10B, MIC, pRI_3, _RI_4)
	ALIAS(_MICG2A, 1D_P10B.Q)

	// Instruções Curtas:
	// RI & 0xE0 == 0x80
	TTL_7404_INVERT(3B_P10B, 2D_P10C.Q)	
	ALIAS(MIC, 3B_P10B.Q)

	// Instruções do Micro-Grupo 1:
	// RI & 0xF0 == 0x80
	TTL_7402_NOR(4A_P10A, 2D_P10C.Q, pRI_3)
	ALIAS(MICG1, 4A_P10A.Q)

	// Pula para a posição 0x002
	// e limpa interrupção:
	// MICG2B && (RI & 0x07 == 0x00)
	TTL_7410_NAND(3D_P10A, _RI_5, _RI_6, _RI_7)
	TTL_7402_NOR(4A_P10B, 3D_P10A.Q, _MICG2B)
	ALIAS(PUL, 4A_P10B.Q)

	// Troca acumulador com extensão:
	// MICG2B && (RI & 0x07 == 0x01)
	TTL_7410_NAND(3D_P10B, _RI_5, _RI_6, pRI_7)
	TTL_7402_NOR(4A_P10D, 3D_P10B.Q, _MICG2B)
	ALIAS(TRO, 4A_P10D.Q)

	// Inibe interrupção:
	// MICG2B && (RI & 0x07 == 0x02)
	TTL_7410_NAND(3D_P10C, _RI_5, pRI_6, _RI_7)
	TTL_7402_NOR(4A_P10C, 3D_P10C.Q, _MICG2B)
	ALIAS(INI, 4A_P10C.Q)

	// Permite interrupção:
	// MICG2B && (RI & 0x07 == 0x03)
	TTL_7410_NAND(1E_P10A, _RI_5, pRI_6, pRI_7)
	TTL_7402_NOR(5A_P10A, 1E_P10A.Q, _MICG2B)
	ALIAS(PER, 5A_P10A.Q)

	// Espere:
	// MICG2B && (RI & 0x07 == 0x04)
	TTL_7410_NAND(2D_P10A, pRI_5, _RI_6, _RI_7)
	TTL_7402_NOR(5A_P10B, 2D_P10A.Q, _MICG2B)
	ALIAS(ESP_D, 5A_P10B.Q)

	// Pare:
	// MICG2B && (RI & 0x07 == 0x05)
	TTL_7410_NAND(2D_P10B, pRI_5, _RI_6, pRI_7)
	TTL_7402_NOR(5A_P10D, 2D_P10B.Q, _MICG2B)
	ALIAS(PAR_D, 5A_P10D.Q)

	// Troca o acumulador com
	// o registrador de índice:
	// MICG2B && (RI & 0x07 == 0x06)
	TTL_7410_NAND(1D_P10C, pRI_5, pRI_6, _RI_7)
	TTL_7402_NOR(5A_P10C, 1D_P10C.Q, _MICG2B)
	ALIAS(TRI, 5A_P10C.Q)

	// Código reservado para uso futuro:
	// MICG2B && (RI & 0x07 == 0x07)
	//
	// Foi utilizado para a instrução
	// IND para endereçamento
	// indireto da memória.
	TTL_7410_NAND(1C_P10C, pRI_5, pRI_6, pRI_7)
	TTL_7402_NOR(3C_P10A, 1C_P10C.Q, _MICG2B)
	ALIAS(RESERVADO, 3C_P10A.Q)

	// Instruções do Micro-Grupo 2B:
	// RI & 0xF1 == 0x98
	TTL_7404_INVERT(3B_P10C, _MICG2B)	
	ALIAS(MICG2B, 3B_P10C.Q)

	// Instruções do Micro-Grupo 2A:
	// RI & 0xF1 == 0x90
	TTL_7404_INVERT(3B_P10F, _MICG2A)	
	ALIAS(MICG2A, 3B_P10F.Q)
}
