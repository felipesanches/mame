// license:GPL-2.0+
// copyright-holders:Felipe Sanches
#include "emu.h"

#include "cpu/anotherworld/anotherworld.h"
CPU_DISASSEMBLE( another_world )
{
    //switch (oprom[0])
    //{
    //    case 0x00: sprintf (buffer, "NOP"); return 1;
    //}

    sprintf (buffer, "illegal instruction");
    return 1;
}
