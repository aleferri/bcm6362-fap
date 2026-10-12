// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BCM6362 FAP firmware: log, local memory tests, mailbox ping/pong
 */

#include "fap_shared.h"

#define KSEG0(x)		(0x80000000 | (x))
#define KSEG1(x)		(0xa0000000 | (x))
#define FAP_4KE_REGS		0xe0001000
#define FAP_4KE_QSM		0xe0004000
#define FAP_4KE_PSM		0xe0010000
#define HEARTBEAT_POLLS		(1 << 16)

/* data scratchpad, at the physical address the SDK firmware uses */
#define DSPRAM_BASE		0x4000
#define DSPRAM_SIZE		0x2000

#define CP0_ERRCTL_SPR		(1 << 28)
#define SPRAM_TAG0_ENABLE	0x80
#define SPRAM_TAG_STRIDE	8

#define INDEX_LOAD_TAG_D	0x05
#define INDEX_STORE_TAG_D	0x09

#define read_c0(reg, sel) ({						\
	unsigned int __v;						\
	__asm__ volatile("mfc0 %0, $" #reg ", " #sel : "=r" (__v));	\
	__v;								\
})

#define write_c0(reg, sel, val) ({					\
	__asm__ volatile("mtc0 %z0, $" #reg ", " #sel "\n\tehb"		\
			 : : "Jr" (val));				\
})

#define cache_op(op, addr) ({						\
	__asm__ volatile("cache %0, 0(%1)\n\tehb"			\
			 : : "i" (op), "r" (addr) : "memory");		\
})

void fap_main(void);
void fap_exception(unsigned int cause, unsigned int epc,
		   unsigned int badvaddr);

static volatile struct fap_shared *const shared =
	(volatile struct fap_shared *)KSEG1(FAP_4KE_BOOT_BASE + FAP_SHARED_OFFSET);

static unsigned int reg_rd(unsigned int off)
{
	return *(volatile unsigned int *)(FAP_4KE_REGS + off);
}

static void reg_wr(unsigned int off, unsigned int val)
{
	*(volatile unsigned int *)(FAP_4KE_REGS + off) = val;
}

static void log_putc(char c)
{
	unsigned int head = shared->log_head;

	shared->log[head % FAP_LOG_SIZE] = c;
	__asm__ volatile("sync" ::: "memory");
	shared->log_head = head + 1;
}

static void log_puts(const char *s)
{
	while (*s)
		log_putc(*s++);
}

static void log_reg(const char *name, unsigned int val)
{
	int shift;

	log_puts(name);
	log_puts(" 0x");
	for (shift = 28; shift >= 0; shift -= 4)
		log_putc("0123456789abcdef"[(val >> shift) & 0xf]);
	log_putc('\n');
}

static void mbox_send(unsigned int type, unsigned int val)
{
	reg_wr(FAP_REG_MBOX_IN, FAP_MSG(type, val));
}

static void dspram_store_tag(unsigned int offset, unsigned int tag)
{
	unsigned int errctl = read_c0(26, 0);

	write_c0(26, 0, errctl | CP0_ERRCTL_SPR);
	write_c0(28, 0, tag);
	cache_op(INDEX_STORE_TAG_D, KSEG0(offset));
	write_c0(26, 0, errctl);
}

static unsigned int dspram_load_tag(unsigned int offset)
{
	unsigned int errctl = read_c0(26, 0);
	unsigned int tag;

	write_c0(26, 0, errctl | CP0_ERRCTL_SPR);
	cache_op(INDEX_LOAD_TAG_D, KSEG0(offset));
	tag = read_c0(28, 0);
	write_c0(26, 0, errctl);

	return tag;
}

static unsigned int mem_test(volatile unsigned int *mem, unsigned int words)
{
	unsigned int errors = 0;
	unsigned int i;

	for (i = 0; i < words; i++)
		mem[i] = FAP_MEM_PATTERN(i);

	for (i = 0; i < words; i++)
		if (mem[i] != FAP_MEM_PATTERN(i))
			errors++;

	return errors;
}

static unsigned int mem_pattern_count(volatile unsigned int *mem,
				      unsigned int words)
{
	unsigned int count = 0;
	unsigned int i;

	for (i = 0; i < words; i++)
		if (mem[i] == FAP_MEM_PATTERN(i))
			count++;

	return count;
}

static int dspram_init(void)
{
	unsigned int errors, leaked;

	dspram_store_tag(0, DSPRAM_BASE | SPRAM_TAG0_ENABLE);
	log_reg("dspram tag0", dspram_load_tag(0));
	log_reg("dspram tag1", dspram_load_tag(SPRAM_TAG_STRIDE));

	errors = mem_test((volatile unsigned int *)KSEG0(DSPRAM_BASE),
			  DSPRAM_SIZE / 4);

	/*
	 * Without the scratchpad the same physical range goes through the
	 * low translator to the memory block, which the boot translator
	 * shows at FAP_4KE_BOOT_BASE.
	 */
	leaked = mem_pattern_count((volatile unsigned int *)
				   KSEG1(FAP_4KE_BOOT_BASE + DSPRAM_BASE),
				   DSPRAM_SIZE / 4);

	log_reg("dspram errors", errors);
	log_reg("dspram words in ddr", leaked);

	return !errors && !leaked;
}

static int local_mem_test(const char *name, unsigned int addr,
			  unsigned int size)
{
	unsigned int errors;

	errors = mem_test((volatile unsigned int *)addr, size / 4);
	log_puts(name);
	log_reg(" errors", errors);

	return !errors;
}

void fap_exception(unsigned int cause, unsigned int epc,
		   unsigned int badvaddr)
{
	log_reg("exception cause", cause);
	log_reg("epc", epc);
	log_reg("badvaddr", badvaddr);
	mbox_send(FAP_MSG_EXCEPTION, (cause >> 2) & 0x1f);

	for (;;)
		;
}

void fap_main(void)
{
	unsigned int ready = 0;
	unsigned int polls = 0;
	unsigned int msg;

	reg_wr(FAP_REG_GP_STATUS, FAP_GP_MBOX_OUT);
	shared->magic = FAP_SHARED_MAGIC;

	log_puts("bcm6362 fap up\n");
	log_reg("prid", read_c0(15, 0));
	log_reg("config", read_c0(16, 0));
	log_reg("config1", read_c0(16, 1));

	if (dspram_init())
		ready |= FAP_READY_DSPRAM;
	if (local_mem_test("psm", FAP_4KE_PSM, FAP_PSM_SIZE))
		ready |= FAP_READY_PSM;
	if (local_mem_test("qsm", FAP_4KE_QSM, FAP_QSM_SIZE))
		ready |= FAP_READY_QSM;

	mbox_send(FAP_MSG_READY, ready);

	for (;;) {
		if (reg_rd(FAP_REG_GP_STATUS) & FAP_GP_MBOX_OUT) {
			reg_wr(FAP_REG_GP_STATUS, FAP_GP_MBOX_OUT);
			msg = reg_rd(FAP_REG_MBOX_OUT);
			log_reg("ping", msg);
			mbox_send(FAP_MSG_PONG, msg);
		}

		if (++polls % HEARTBEAT_POLLS == 0)
			shared->heartbeat++;
	}
}
