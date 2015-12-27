// license:GPL-2.0+
// copyright-holders:Felipe Sanches
#include "emu.h"
#include "stdio.h"
#include "stdlib.h"
#include "cpu/anotherworld/anotherworld.h"
#include "cpu/anotherworld/anotherworld_dasm.h"

static void getVariableName(char* s, uint8_t id)
{
	switch(id)
	{
		case VM_VARIABLE_RANDOM_SEED:           sprintf(s, "RANDOM_SEED"); break;
		case VM_VARIABLE_LAST_KEYCHAR:          sprintf(s, "LAST_KEYCHAR"); break;
		case VM_VARIABLE_HERO_POS_UP_DOWN:      sprintf(s, "HERO_POS_UP_DOWN"); break;
		case VM_VARIABLE_MUS_MARK:              sprintf(s, "MUS_MARK"); break;
		case VM_VARIABLE_SCROLL_Y:              sprintf(s, "SCROLL_Y"); break;
		case VM_VARIABLE_HERO_ACTION:           sprintf(s, "HERO_ACTION"); break;
		case VM_VARIABLE_HERO_POS_JUMP_DOWN:    sprintf(s, "HERO_POS_JUMP_DOWN"); break;
		case VM_VARIABLE_HERO_POS_LEFT_RIGHT:   sprintf(s, "HERO_POS_LEFT_RIGHT"); break;
		case VM_VARIABLE_HERO_POS_MASK:         sprintf(s, "HERO_POS_MASK"); break;
		case VM_VARIABLE_HERO_ACTION_POS_MASK:  sprintf(s, "HERO_ACTION_POS_MASK"); break;
		case VM_VARIABLE_PAUSE_SLICES:          sprintf(s, "PAUSE_SLICES"); break;
		case VM_VARIABLE_HACK_VAR_54:           sprintf(s, "HACK_VAR_54"); break;
		case VM_VARIABLE_HACK_VAR_67:           sprintf(s, "HACK_VAR_67"); break;
		case VM_VARIABLE_HACK_VAR_DC:           sprintf(s, "HACK_VAR_DC"); break;
		case VM_VARIABLE_HACK_VAR_F7:           sprintf(s, "HACK_VAR_F7"); break;
		default:
			sprintf(s, "0x%02X", id); break;
	}
}


u32 anotherworld_disassembler::opcode_alignment() const
{
	return 1;
}


offs_t anotherworld_disassembler::disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params)
{
	offs_t pos = pc;
	char dstVarNameString[21], srcVarNameString[21];
	uint8_t opcode = opcodes.r8(pos++);

	/* first disasm the video opcodes */

	if (opcode & 0x80)
	{
		uint16_t off = ((opcode << 8) | opcodes.r8(pos++)) * 2;
		int8_t x = opcodes.r8(pos++);
		int8_t y = opcodes.r8(pos++);
		int8_t h = y - 199;

		if (h > 0)
		{
			y = 199;
			x += h;
		}


		util::stream_format(stream, "video: off=0x%X x=%d y=%d", off, x, y);
		return 4;
	}

	if (opcode & 0x40)
	{
		uint16_t x, y;
		uint16_t off = opcodes.r8(pos++);
		off = ((off << 8) | opcodes.r8(pos++)) * 2;
		char x_str[20];
		char y_str[20];
		char zoom_str[20];

		x = opcodes.r8(pos++);
		if (!(opcode & 0x20))
		{
			if (!(opcode & 0x10))
			{
				x = (x << 8) | opcodes.r8(pos++);
				sprintf(x_str, "%d", x);
			} else {
				sprintf(x_str, "[0x%02x]", x);
			}
		}
		else
		{
			if (opcode & 0x10)
				x += 0x100;

			sprintf(x_str, "%d", x);
		}

		if (!(opcode & 8))
		{
			if (!(opcode & 4))
			{
				y = opcodes.r8(pos++);
				y = (y << 8) | opcodes.r8(pos++);
				sprintf(y_str, "%d", (int) y);
			}
			else
			{
				y = opcodes.r8(pos++);
				sprintf(y_str, "[0x%02x]", y);
			}
		}
		else
		{
			y = opcodes.r8(pos++);
			sprintf(y_str, "%d", y);
		}

		uint8_t zoom;
		if (!(opcode & 2))
		{
			if (!(opcode & 1))
			{
				sprintf(zoom_str, "0x40");
			}
			else
			{
				zoom = opcodes.r8(pos++);
				sprintf(zoom_str, "[0x%02x]", zoom);
			}
		}
		else
		{
			if (opcode & 1) {
				zoom = 0x40;
				sprintf(zoom_str, "0x40");
			} else {
				zoom = opcodes.r8(pos++);
				sprintf(zoom_str, "[0x%02x]", zoom);
			}
		}
		util::stream_format(stream, "video: off=0x%X x=%s y=%s zoom:%s", off, x_str, y_str, zoom_str);
		return pos - pc;
	}

	/* And then disasm the other opcodes: */
	switch (opcode)
	{
		case 0x00: /* movConst */
		{
			getVariableName(dstVarNameString, opcodes.r8(pos++));
			uint16_t immediate = opcodes.r8(pos++);
			immediate = (immediate << 8) + opcodes.r8(pos++);
			util::stream_format(stream, "mov [%s], 0x%04X", dstVarNameString, immediate);
			return 4;
		}
		case 0x01: /* mov */
		{
			getVariableName(dstVarNameString, opcodes.r8(pos++));
			getVariableName(srcVarNameString, opcodes.r8(pos++));
			util::stream_format(stream, "mov [%s], [%s]", dstVarNameString, srcVarNameString);
			return 3;
		}
		case 0x02: /* add */
		{
			getVariableName(dstVarNameString, opcodes.r8(pos++));
			getVariableName(srcVarNameString, opcodes.r8(pos++));
			util::stream_format(stream, "add [%s], [%s]", dstVarNameString, srcVarNameString);
			return 3;
		}
		case 0x03: /* addConst */
		{
			getVariableName(dstVarNameString, opcodes.r8(pos++));
			uint16_t immediate = opcodes.r8(pos++);
			immediate = (immediate << 8) | opcodes.r8(pos++);
			util::stream_format(stream, "add [%s], 0x%04X", dstVarNameString, immediate);
			return 4;
		}
		case 0x04: /* call */
		{
			uint16_t address = opcodes.r8(pos++);
			address = (address << 8) | opcodes.r8(pos++);
			util::stream_format(stream, "call 0x%04X", address);
			return 3;
		}
		case 0x05: /* ret */
		{
			util::stream_format(stream, "ret");
			return 1;
		}
		case 0x06:
		{
			// From Eric Chahi notes found at
			// http://www.anotherworld.fr/anotherworld_uk/page_realisation.htm:
			//
			//  Break (a.k.a. "pauseThread")
			//  Temporarily stops the executing channel and goes to the next.
			util::stream_format(stream, "break");
			return 1;
		}
		case 0x07: /* jmp */
		{
			uint16_t address = opcodes.r8(pos++);
			address = (address << 8) | opcodes.r8(pos++);
			util::stream_format(stream, "jmp 0x%04X", address);
			return 3;
		}
		case 0x08:
		{
			// From Eric Chahi notes found at
			// http://www.anotherworld.fr/anotherworld_uk/page_realisation.htm:
			//
			//  Setvec "channel number", address
			//  Initialises a channel with a code address to execute

			uint8_t threadId;
			uint16_t pcOffsetRequested;
			threadId = opcodes.r8(pos++);
			pcOffsetRequested = opcodes.r8(pos++);
			pcOffsetRequested = (pcOffsetRequested << 8) | opcodes.r8(pos++);
			util::stream_format(stream, "setvec channel:0x%02X, address:0x%04X", threadId, pcOffsetRequested);
			return 4;
		}
		case 0x09:
		{
			/* djnz instruction ('D'ecrement variable value and 'J'ump if 'N'ot 'Z'ero) */
			uint8_t i = opcodes.r8(pos++);
			uint16_t offset = opcodes.r8(pos++);
			offset = (offset << 8) | opcodes.r8(pos++);
			char varStr[21];
			getVariableName(varStr, i);
			util::stream_format(stream, "djnz [%s], 0x%04X", varStr, offset);
			return 4;
		}
		case 0x0A:
		{
			/* Conditional Jump instructions */

			int retval;
			uint8_t subopcode = opcodes.r8(pos++);
			uint8_t b = opcodes.r8(pos++);
			uint8_t c = opcodes.r8(pos++);
			uint16_t offset;
			char var1Str[21], var2Str[21], prefix[27], midterm[23];
			getVariableName(var1Str, b);

			if (subopcode & 0x80)
			{
				getVariableName(var2Str, c);
				sprintf(midterm, "[%s]", var2Str);
				offset = opcodes.r8(pos++);
				offset = (offset << 8) | opcodes.r8(pos++);
				retval = 6;
			}
			else if (subopcode & 0x40)
			{
				sprintf(midterm, "0x%04X", (c << 8) | opcodes.r8(pos++));
				offset = opcodes.r8(pos++);
				offset = (offset << 8) | opcodes.r8(pos++);
				retval = 7;
			}
			else
			{
				sprintf(midterm, "0x%02X", c);
				offset = opcodes.r8(pos++);
				offset = (offset << 8) | opcodes.r8(pos++);
				retval = 6;
			}

			switch (subopcode & 7)
			{
				case 0: // jz
					sprintf(prefix, "je [%s]", var1Str);
					break;
				case 1: // jnz
					sprintf(prefix, "jne [%s]", var1Str);
					break;
				case 2: // jg
					sprintf(prefix, "jg [%s]", var1Str);
					break;
				case 3: // jge
					sprintf(prefix, "jge [%s]", var1Str);
					break;
				case 4: // jl
					sprintf(prefix, "jl [%s]", var1Str);
					break;
				case 5: // jle
					sprintf(prefix, "jle [%s]", var1Str);
					break;
				default:
					util::stream_format(stream, "< conditional jmp with invalid condition: %d >", (subopcode & 7));
					return retval;
			}

			util::stream_format(stream, "%s, %s, 0x%04X", prefix, midterm, offset);
			return retval;
		}
		case 0x0B: /* setPalette */
		{
			uint16_t paletteId = opcodes.r8(pos++);
			paletteId = (paletteId << 8) | opcodes.r8(pos++);
			util::stream_format(stream, "setPalette 0x%04X", paletteId);
			return 3;
		}
		case 0x0C:
		{
			// From Eric Chahi notes found at
			// http://www.anotherworld.fr/anotherworld_uk/page_realisation.htm:
			//
			//  Vec début, fin, type
			//  Deletes, freezes or unfreezes a series of channels.

			uint8_t first = opcodes.r8(pos++);
			uint8_t last = opcodes.r8(pos++);
			uint8_t type = opcodes.r8(pos++);
			const char* operation_names[3] = {
				"freezeChannels",
				"unfreezeChannels",
				"deleteChannels"
			};

			if (type > 2)
			{
				util::stream_format(stream, "< invalid operation type for resetThread opcode >");
				return 4;
			}

			util::stream_format(stream, "%s first:0x%02X, last:0x%02X", operation_names[type], first, last);
			return 4;
		}
		case 0x0D: /* selectVideoPage */
		{
			uint8_t frameBufferId = opcodes.r8(pos++);
			util::stream_format(stream, "selectVideoPage 0x%02X", frameBufferId);
			return 2;
		}
		case 0x0E: /* fillVideoPage */
		{
			uint8_t pageId = opcodes.r8(pos++);
			uint8_t color = opcodes.r8(pos++);
			util::stream_format(stream, "fillVideoPage 0x%02X, color:0x%02X", pageId, color);
			return 3;
		}
		case 0x0F: /* copyVideoPage */
		{
			uint8_t srcPageId = opcodes.r8(pos++);
			uint8_t dstPageId = opcodes.r8(pos++);
			util::stream_format(stream, "copyVideoPage src:0x%02X, dst:0x%02X", srcPageId, dstPageId);
			return 3;
		}
		case 0x10: /* blitFramebuffer */
		{
			uint8_t pageId = opcodes.r8(pos++);
			util::stream_format(stream, "blitFramebuffer 0x%02X", pageId);
			return 2;
		}
		case 0x11: /* killChannel (a.k.a. "killThread") */
		{
			util::stream_format(stream, "killChannel");
			return 1;
		}
		case 0x12:
		{
			// From Eric Chahi notes found at
			// http://www.anotherworld.fr/anotherworld_uk/page_realisation.htm:
			//
			//  Text "text number", x, y, color
			//  Displays in the work screen the specified text for the coordinates x,y.

			uint16_t stringId = opcodes.r8(pos++);
			stringId = (stringId << 8) | opcodes.r8(pos++);
			uint8_t x = opcodes.r8(pos++);
			uint8_t y = opcodes.r8(pos++);
			uint8_t color = opcodes.r8(pos++);

/*
			TODO: load game strings from a loaded ROM resource

			const StrEntry *se = Video::_stringsTableEng;

			//Search for the location where the string is located.
			while (se->id != END_OF_STRING_DICTIONARY && se->id != stringId)
				++se;
*/

			util::stream_format(stream, "text id:0x%04X, x:%d, y:%d, color:0x%02X", stringId, x, y, color);
			return 6;
		}
		case 0x13: /* SUB instruction */
		{
			char var1Str[21], var2Str[21];
			getVariableName(var1Str, opcodes.r8(pos++));
			getVariableName(var2Str, opcodes.r8(pos++));
			util::stream_format(stream, "sub [%s], [%s]", var1Str, var2Str);
			return 3;
		}
		case 0x14: /* AND instruction */
		{
			getVariableName(dstVarNameString, opcodes.r8(pos++));
			uint16_t immediate = opcodes.r8(pos++);
			immediate = (immediate << 8) | opcodes.r8(pos++);
			util::stream_format(stream, "and [%s], 0x%04X", dstVarNameString, immediate);
			return 4;
		}
		case 0x15: /* OR instruction */
		{
			getVariableName(dstVarNameString, opcodes.r8(pos++));
			uint16_t immediate = opcodes.r8(pos++);
			immediate = (immediate << 8) | opcodes.r8(pos++);
			util::stream_format(stream, "or [%s], 0x%04X", dstVarNameString, immediate);
			return 4;
		}
		case 0x16: /* Shift Left instruction */
		{
			uint8_t variableId = opcodes.r8(pos++);
			uint16_t leftShiftValue = opcodes.r8(pos++);
			leftShiftValue = (leftShiftValue << 8) | opcodes.r8(pos++);
			char varStr[21];
			getVariableName(varStr, variableId);
			util::stream_format(stream, "shl [%s], 0x%04X", varStr, leftShiftValue);
			return 4;
		}
		case 0x17: /* Shift Right instruction */
		{
			uint8_t variableId = opcodes.r8(pos++);
			uint16_t rightShiftValue = opcodes.r8(pos++);
			rightShiftValue = (rightShiftValue << 8) | opcodes.r8(pos++);
			char varStr[21];
			getVariableName(varStr, variableId);
			util::stream_format(stream, "shr [%s], 0x%04X", varStr, rightShiftValue);
			return 4;
		}
		case 0x18: /* play (a.k.a. "playSound") */
		{
			uint8_t freq, vol, channel;
			uint16_t resourceId;
			resourceId = opcodes.r8(pos++);
			resourceId = (resourceId << 8) | opcodes.r8(pos++);
			freq = opcodes.r8(pos++);
			vol = opcodes.r8(pos++);
			channel = opcodes.r8(pos++);
			util::stream_format(stream, "play id:0x%04X, freq:0x%02X, vol:0x%02X, channel:0x%02X", resourceId, freq, vol, channel);
			return 6;
		}
		case 0x19: /* load (a.k.a. "updateMemList") */
		{
			// From Eric Chahi notes found at
			// http://www.anotherworld.fr/anotherworld_uk/page_realisation.htm:
			//
			//  Load "file number"
			//  Loads a file in memory, such as sound, level or image.

			uint16_t immediate = opcodes.r8(pos++);
			immediate = (immediate << 8) | opcodes.r8(pos++);
			util::stream_format(stream, "load id:0x%04X", immediate);
			return 3;
		}
		case 0x1A: /* song (a.k.a. "playMusic") */
		{
			// From Eric Chahi notes found at
			// http://www.anotherworld.fr/anotherworld_uk/page_realisation.htm:
			//
			//  Song "file number" Tempo, Position
			//  Initialises a song.

			uint16_t resNum = opcodes.r8(pos++);
			resNum = (resNum << 8) | opcodes.r8(pos++);
			uint16_t delay = opcodes.r8(pos++);
			delay = (delay << 8) | opcodes.r8(pos++);
			uint8_t position = opcodes.r8(pos++);
			util::stream_format(stream, "song id:0x%04X, delay:0x%04X, pos:0x%02X", resNum, delay, position);
			return 6;
		}
		default:
			util::stream_format(stream, "< illegal instruction >");
			return 1;
	}
}
