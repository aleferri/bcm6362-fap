/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "fap_shared.h"

/*
 * The memory block is seen by the 4KE at FAP_4KE_BOOT_BASE and the image
 * runs from its kseg0 alias. The core has no D-cache, so data in kseg0 is
 * not cached either.
 */
#define KSEG0(x)	(0x80000000 + (x))

#define FAP_STACK_SIZE	0x800

OUTPUT_ARCH(mips)
ENTRY(_reset)

SECTIONS
{
	.vectors KSEG0(FAP_4KE_BOOT_BASE) : {
		KEEP(*(.vec.reset))
		. = 0x180;
		KEEP(*(.vec.general))
		. = 0x380;
		KEEP(*(.vec.boot))
		. = 0x480;
		KEEP(*(.vec.ejtag))
	}

	.text : {
		*(.text .text.*)
	}

	.data : {
		*(.rodata .rodata.*)
		*(.data .data.*)
	}

	.bss (NOLOAD) : {
		. = ALIGN(4);
		_bss_start = .;
		*(.bss .bss.* COMMON)
		. = ALIGN(4);
		_bss_end = .;
	}

	.stack (NOLOAD) : {
		. = ALIGN(8);
		. += FAP_STACK_SIZE;
		_stack_top = .;
	}

	ASSERT(_stack_top <= KSEG0(FAP_4KE_BOOT_BASE + FAP_SHARED_OFFSET),
	       "firmware overlaps the shared area")

	/DISCARD/ : {
		*(.reginfo .MIPS.abiflags .MIPS.options .pdr .comment .note.*)
		*(.gnu.attributes .mdebug.*)
	}
}
