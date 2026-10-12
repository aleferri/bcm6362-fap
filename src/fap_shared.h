/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Definitions shared by the BCM6362 FAP host driver and the 4KE firmware
 */

#ifndef __BCM6362_FAP_SHARED_H
#define __BCM6362_FAP_SHARED_H

/* memory block holding the firmware, naturally aligned to its size */
#define FAP_MEM_SIZE		0x10000
#define FAP_SHARED_OFFSET	0xf000
#define FAP_LOG_SIZE		0x800

/* 4KE physical address the block is mapped at to cover the reset vector */
#define FAP_4KE_BOOT_BASE	0x1fc00000

/* queue and packet shared memories, local to the FAP */
#define FAP_QSM_SIZE		0x4000
#define FAP_PSM_SIZE		0x8000

/* fill pattern of the memory tests, word index i */
#define FAP_MEM_PATTERN(i)	(((i) << 16) | (~(i) & 0xffff))

/* control registers used by both sides */
#define FAP_REG_GP_STATUS	0x14
#define  FAP_GP_MBOX_IN		(1 << 2)	/* 4KE wrote FAP_REG_MBOX_IN */
#define  FAP_GP_MBOX_OUT	(1 << 3)	/* host wrote FAP_REG_MBOX_OUT */
#define FAP_REG_MBOX_IN		0x28		/* 4KE to host */
#define FAP_REG_MBOX_OUT	0x2c		/* host to 4KE */

/* 4KE to host messages: type in the upper half, value in the lower one */
#define FAP_MSG(type, val)	(((type) << 16) | ((val) & 0xffff))
#define FAP_MSG_TYPE(msg)	((msg) >> 16)
#define FAP_MSG_VALUE(msg)	((msg) & 0xffff)

#define FAP_MSG_READY		1
#define  FAP_READY_DSPRAM	(1 << 0)	/* memory test passed */
#define  FAP_READY_PSM		(1 << 1)	/* memory test passed */
#define  FAP_READY_QSM		(1 << 2)	/* memory test passed */
#define FAP_MSG_PONG		2
#define FAP_MSG_EXCEPTION	3

#define FAP_SHARED_MAGIC	0x46415034

#ifndef __ASSEMBLER__
struct fap_shared {
	unsigned int magic;
	unsigned int heartbeat;
	unsigned int log_head;
	unsigned int reserved;
	char log[FAP_LOG_SIZE];
};
#endif

#endif /* __BCM6362_FAP_SHARED_H */
