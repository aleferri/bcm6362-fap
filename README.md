# BCM6362 FAP

Next to every fact in this summary is its source:

- **[HW]** verified by us on the hardware;
- **[blob]** derived from the disassembled stock code;
- **[SDK]** from the headers only;
- **[ded.]** deduced.

## What we know about the FAP

### The core

| | | Source |
|---|---|---|
| How many | Only one on the 6362. The 63168 has two | [SDK] |
| CPU | MIPS 4KEp Release 2 (PRId 0x0001916c), big-endian, Fixed Mapping without TLB, iterative multiplier | [HW] |
| Cache | 16 KB 4-way I-cache, 16-byte lines. No D-cache | [HW] |
| DSPRAM | Present, 8 KB, enabled at 0x4000 (TagLo 0x4080, read back as 0x40a0) and tested. The size comes from the stock firmware, not from the hardware | [HW] |
| Other | MIPS16e, EJTAG, COP2 interface present (what is attached to it is unknown), no FPU and no performance counters | [HW] |
| Frequency | Unknown. The polling loop runs 13.9 million iterations per second, so at least about 100 MHz | [HW] |

### Address map

| Block | Seen by the 4KE | Seen by the host via UBUS | Size |
|---|---|---|---|
| FAP registers | 0xe0001000 [HW] | 0x11001000 [HW] | 4 KB, plus the DQM counters at +0x1000 [SDK] |
| QSM | 0xe0004000 [HW] | 0x11004000 [SDK]; the stock driver uses the SMISB alias 0x14004000 [blob] | 16 KB [HW, 4KE side] |
| PSM | 0xe0010000 [HW] | 0x11010000 [blob] | 32 KB [HW, 4KE side] |
| DPE | 0xe0050000… | 0x11040000 | [SDK] |
| DDR | Through two translators: mask, 4KE-side base, host-side base. They ignore bits 31 and 29, so kseg0 and kseg1 are aliases | — | power-of-two sized, naturally aligned block [HW] |

The sub-blocks inside the register space, as offsets from the base [SDK]:

- control 0x000;
- outgoing message FIFO 0x100 and incoming 0x200;
- internal DMA 0x300;
- token interface 0x400;
- performance measurement 0x600;
- message ID 0x700;
- DQM control 0x800, queue control 0xa00, queue data 0xc00, counters 0x1000.

### Control registers in use

| Offset | Register | Source |
|---|---|---|
| 0x00 / 0x04 | irq_4ke mask and status | [SDK] |
| 0x08 / 0x0c | irq_mips mask and status. Bit 4 (MBOX_IN) follows gp_status | [HW] |
| 0x10 / 0x14 | gp mask and status. Bit 2: the 4KE wrote. Bit 3: the host wrote. Both write-one-to-clear | [HW] |
| 0x28 | host_mbox_in, from the 4KE to the host | [HW] |
| 0x2c | host_mbox_out, from the host to the 4KE | [HW] |
| 0x6c–0x80 | Translators 1 and 2 | [HW] |
| 0x8c | soft_resets, bit 0 holds the 4KE in reset | [HW] |

### Clock, reset, power and interrupts

- Clock, reset, IDDQ: the FAP clock bit is 18, the reset bit 13, the IDDQ bit 11. The kernel providers already handle them. [HW]
- Interrupt to the host: peripheral line 29. [HW]
- Interrupts to the FAP: peripherals can be routed to the FAP through miscFapIrqMask, Extra and Extra2. The stock firmware routed the ENETSW RX DMAs and the SAR DMAs there. [SDK]
- ENETSW on the 6362: it only has RX DMA lines 0–3, no TX. [SDK]

### Boot [HW]

1. power, clock and reset of the block (about 20 ms);
2. soft_resets set to 1 to hold the 4KE;
3. image copied into the DDR block;
4. low translator at 0, so the exceptions at 0x80000180 land in the block;
5. boot translator at 0x1fc00000, i.e. the reset vector;
6. 4KE released.

### Stock firmware

- Size: 512 KB block, about 110 KB of code and 150 KB of bss. [blob]
- Boot: on reset it writes 0x00C0FFEE into the mailbox and jumps to the startup code. The startup code sets K0 write-through, invalidates the I-cache, configures the DSPRAM and puts the stack there. [blob]
- QSM: according to a header comment, the stock firmware places the ENET RX descriptors in the upper part of the QSM. So the iuDMA can read descriptors from there. [SDK]
- Registers and memories: the stock 4KE code uses the QSM up to 0xe0007f34 and the PSM up to 0xe0017b80, consistent with their sizes. [blob]

### Still unknown

- 4KE frequency and access latencies of registers, PSM, QSM and DDR;
- what sits on COP2;
- DQM semantics beyond the header structs;
- how the 4KE reaches the ENETSW iuDMA registers and SRAM (to be derived from the stock 4KE code);
- the QSM over UBUS from the host side, verifiable with the next flash.

## Offload plan

| Phase | Content | Exit criterion | Status |
|---|---|---|---|
| 0 | Loader, boot, mailbox, log, memory tests | The 4KE runs and talks to the host | done |
| 1 | Characterization: frequency from the CP0 Count register, latencies of registers, PSM, QSM and DDR, stack in DSPRAM, interrupts on the 4KE (mailbox and timer) | The cycle budget per packet, and hence whether 700–800 Mbit/s is realistic | next |
| 2 | Host–FAP transport: DQM queues in QSM with a "not empty" interrupt. Alternatively, our own rings in PSM | A ping that goes through the queues instead of the mailbox | |
| 3 | Passthrough: the FAP takes the ENETSW iuDMA RX and TX channels, RX routed to the FAP, the host receives and transmits through the queues | The router works entirely through the FAP. TX completions with interrupts, so the timer is no longer needed | |
| 4 | Flow offload: nf_flowtable with ndo_setup_tc(TC_SETUP_FT) on the enetsw conduit, which DSA already forwards. Flow table in PSM. The FAP does the 5-tuple lookup, NAT rewrite, TTL, incremental checksum update, port tag, and transmits directly. Statistics and expirations go back to the host | Routing throughput with NAT and host CPU load, with and without offload | |
