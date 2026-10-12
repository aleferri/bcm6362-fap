/* SPDX-License-Identifier: GPL-2.0-or-later */

#define CP0_BADVADDR	$8
#define CP0_STATUS	$12
#define CP0_CAUSE	$13
#define CP0_EPC		$14
#define CP0_CONFIG	$16
#define CP0_TAGLO	$28
#define CP0_TAGHI	$29

#define CONFIG_K0_MASK	0x7
#define CONFIG_K0_WT	0x0

#define KSEG0		0x80000000
#define KSEG1		0xa0000000

#define INDEX_STORE_TAG_I	0x08

	.set	noreorder

/* 0xbfc00000: still uncached, continue from the kseg1 alias of _start */
	.section .vec.reset, "ax"
	.globl	_reset
_reset:
	la	$t0, _start
	li	$t1, KSEG1
	or	$t0, $t0, $t1
	jr	$t0
	 nop

/* 0x80000180 (BEV=0) through the low address translator */
	.section .vec.general, "ax"
	la	$k0, _exception
	jr	$k0
	 nop

/* 0xbfc00380 (BEV=1) */
	.section .vec.boot, "ax"
	la	$k0, _exception
	jr	$k0
	 nop

/* 0xbfc00480: EJTAG debug exception */
	.section .vec.ejtag, "ax"
	deret

	.text
_start:
	mtc0	$zero, CP0_CAUSE
	mtc0	$zero, CP0_STATUS

	/* invalidate the I-cache, geometry from Config1 */
	mfc0	$t0, CP0_CONFIG, 1
	srl	$t1, $t0, 19
	andi	$t1, $t1, 7		/* IL */
	beqz	$t1, 2f
	 li	$t2, 2
	sllv	$t2, $t2, $t1		/* line size */
	srl	$t3, $t0, 22
	andi	$t3, $t3, 7		/* IS */
	li	$t4, 64
	sllv	$t4, $t4, $t3		/* sets per way */
	srl	$t3, $t0, 16
	andi	$t3, $t3, 7		/* IA */
	addiu	$t3, $t3, 1
	mul	$t4, $t4, $t3
	mul	$t4, $t4, $t2		/* cache size */
	mtc0	$zero, CP0_TAGLO
	mtc0	$zero, CP0_TAGHI
	li	$t3, KSEG0
	addu	$t4, $t4, $t3
1:	cache	INDEX_STORE_TAG_I, 0($t3)
	addu	$t3, $t3, $t2
	bne	$t3, $t4, 1b
	 nop
2:
	mfc0	$t0, CP0_CONFIG
	li	$t1, ~CONFIG_K0_MASK
	and	$t0, $t0, $t1
	ori	$t0, $t0, CONFIG_K0_WT
	mtc0	$t0, CP0_CONFIG

	la	$sp, _stack_top
	la	$t0, 3f
	jr.hb	$t0			/* to kseg0, with the new K0 */
	 nop

3:	la	$t0, _bss_start
	la	$t1, _bss_end
4:	beq	$t0, $t1, 5f
	 nop
	sw	$zero, 0($t0)
	b	4b
	 addiu	$t0, $t0, 4

5:	jal	fap_main
	 nop
6:	b	6b
	 nop

_exception:
	mfc0	$a0, CP0_CAUSE
	mfc0	$a1, CP0_EPC
	mfc0	$a2, CP0_BADVADDR
	la	$sp, _stack_top
	jal	fap_exception
	 nop
7:	b	7b
	 nop
