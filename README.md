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
| Frequency | 166.67 MHz, CP0 Count at 83.33 MHz (resolution 2), measured against the host clock. The SDK comments say 200 MHz | [HW] |
| Interrupts | CP0 timer on IP7 (IntCtl.IPTI), FAP block on IP2. Entry into a C handler 54–740 cycles, including the context save | [HW] |
| EBase | Writable: moved to 0x9fc00000 at run time, interrupts keep working. CPUNum (bits 9:0) reads 2 [HW]. The firmware now sets it at boot | [HW] |

### Access cost

Cycles per access from the 4KE, with the loop overhead taken out. Loads
are followed by an instruction using the loaded value.

| Target | Load | Store |
|---|---|---|
| DSPRAM | 2 | 1 |
| PSM, QSM | 7 | 4.6 |
| FAP control registers | 6 | 6.6 |
| DDR through a translator, same word | 30.6 | 7.6 |
| DDR through a translator, alternating words 4 KB or 32 KB apart | 59 | not measured |
| DDR at its host physical address, same word | 56 | not measured |
| ENETSW iuDMA registers | 42 | not measured |
| ENETSW iuDMA state RAM | 43 | not measured |
| Integrated WLAN core registers, single load | about 64 | not measured |
| PCIe memory window, single load | about 230 | not measured |

Source: [HW], phase 1 benchmark. Stores go through the write buffer.
The iuDMA is only read, since the enetsw driver owns it.

Through the translators a DDR row change costs about 28 cycles more. The
4KE also reaches DDR directly at its host physical address, outside the
translator windows, but even without leaving the row that path costs
about as much as a row change through a window [HW]; why is unknown.

Host accesses, in ns per access, plain and with the MMIO accessors,
which on MIPS add a sync around every access [HW]:

| Target | Load | Store | readl | writel |
|---|---|---|---|---|
| FAP registers, QSM, PSM | 186 | 72–84 | 234 | 186–198 |
| DDR uncached | 253 | 54 | 300 | 144 |
| DDR cached | 10 | 7.6 | 55 | 28 |

The 4KE reaches the DMA engines of the integrated WLAN 802.11 core at
0x10005200 through kseg1, with the core up (b43 holds it in reset while
no interface is up): 64-bit engines, TX and RX enabled, rings in RAM,
RX index moving in monitor mode [HW]. It also reaches the PCIe memory
window at physical 0xa0000000 through kuseg at 0x60000000, as the Fixed
Mapping MMU implies: a load from the BCM4352 BAR0 completed with the
Broadcom backplane "dead" pattern 0xdeaddead, byte swapped, so PCIe data
reaches the 4KE in little-endian order [HW]. That the core behind the
window answered dead means it was not accessible, not that the path
failed.

The 4KE reaches the ENETSW iuDMA at its host physical address 0x1000d800
through kseg1: it reads the controller enabled, RX channel 0 enabled and
two page aligned ring addresses inside the RAM, as the enetsw driver sets
them [HW]. WAIT keeps the CP0 Count running, so the timer wakes the core
[HW].

### Address map

| Block | Seen by the 4KE | Seen by the host via UBUS | Size |
|---|---|---|---|
| FAP registers | 0xe0001000 [HW] | 0x11001000 [HW] | 4 KB, plus the DQM counters at +0x1000 [SDK] |
| QSM | 0xe0004000 [HW] | 0x11004000 [HW]; the stock driver uses the SMISB alias 0x14004000 [blob] | 16 KB [HW] |
| PSM | 0xe0010000 [HW] | 0x11010000 [HW] | 32 KB [HW] |
| DPE | 0xe0050000… | 0x11040000 | [SDK] |
| DDR | Through two translators: mask, 4KE-side base, host-side base. They ignore bits 31 and 29, so kseg0 and kseg1 are aliases [HW]. Outside their windows, directly at the host physical address [HW] | — | power-of-two sized, naturally aligned block [HW] |

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
| 0x00 / 0x04 | irq_4ke mask and status, driving IP2 [HW]. Bit 0 timer, bit 4 MBOX_IN [SDK]. Bit 5 MBOX_OUT follows gp_status bit 3 and clears with it [HW] |
| 0x08 / 0x0c | irq_mips mask and status. Bit 4 (MBOX_IN) follows gp_status | [HW] |
| 0x10 / 0x14 | gp mask and status. Bit 2: the 4KE wrote. Bit 3: the host wrote. Both write-one-to-clear. The firmware sets mask bit 3 for its interrupt; whether that is needed was not tested | [HW] |
| 0x28 | host_mbox_in, from the 4KE to the host | [HW] |
| 0x2c | host_mbox_out, from the host to the 4KE | [HW] |
| 0x6c–0x80 | Translators 1 and 2 | [HW] |
| 0x84 | scratch | [SDK] |
| 0x8c | soft_resets, bit 0 holds the 4KE in reset | [HW] |

### Clock, reset, power and interrupts

- Clock, reset, IDDQ: the FAP clock bit is 18, the reset bit 13, the IDDQ bit 11. The kernel providers already handle them. [HW]
- Interrupt to the host: peripheral line 29. [HW]
- Interrupts to the FAP: peripherals can be routed to the FAP through miscFapIrqMask, Extra and Extra2. The stock firmware routed the ENETSW RX DMAs and the SAR DMAs there. [SDK]
- ENETSW DMA interrupts: RX 0–3 reach the host interrupt controller; TX 0–3 only exist in extra2ChipIrqStatus, which can be routed to the FAP through extra2FapIrqMask, so only the FAP can get TX completion interrupts. [SDK]
- iuDMA splitting: the SDK kernel configuration has "PktDma Iudma Rx Splitting" and "Tx Splitting" options, which need the FAP, and allows 1 to 4 ENET RX and TX DMA channels. The stock design already shared the iuDMA channels between host and FAP in both directions. [SDK]
- ENET channels: logical channel n is iuDMA channel 2n for RX and 2n + 1 for TX. On the 6362 the stock driver gives logical channel 0 to the FAP and channel 1 to the host when splitting, and keeps the descriptors of the FAP channels in PSM (`ENET_RX_BDS_IN_PSM`, `ENET_TX_BDS_IN_PSM`), so the iuDMA reaches the PSM. [SDK]
- The D-Link DSL-3580_EU build profile has no RX or TX splitting and one ENET RX and one TX channel, both owned by the FAP, with FAP GSO enabled: the stock firmware routes all host traffic through the FAP, receive and transmit. Logical channel 1 (iuDMA 2 and 3) is unused there. [SDK]
- Switch to iuDMA channels: register 0xac of switch page 0 (`REG_IUDMA_QUEUE_CTRL`, 32 bits, at 0x10e000ac). Bits 31:16 hold 2 bits per CPU port egress queue (8 queues), the logical RX channel the queue goes to; bits 11:0 hold 3 bits per logical TX channel, the switch queue its frames take. The stock RX splitting sends queue 1 to channel 1. [SDK]
- The per port `pri_id_map` at switch offset 0x3050, which an SDK header comment ties to the RX iuDMA channel, is not what the stock driver uses for it; all 9 read 0 [HW].
- The stock FAP host object for this board (`bcmfapDSL-3580_EU.o_save` in the GPL tarball) is not stripped and carries debug information; the 4KE image is in `gSections4ke`. It is the reference for what the open sources leave out, such as the routing of the ENETSW interrupts to the FAP (`fapIrq_init`).

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

- what sits on COP2;
- DQM semantics beyond the header structs;
- the cost of stores to the iuDMA, and whether the iuDMA can use descriptors in QSM, as the stock firmware does for ENET RX;
- why direct DDR accesses from the 4KE cost twice those through a
  translator window;
- the DDR latency with row misses, and the cost of the FAP internal DMA, which could bring packet headers into PSM;

## Operating model

The iuDMA channels are independent: each has its own registers, state
RAM and descriptor ring. When active, the FAP owns the RX channel and a
TX channel of its own, while the enetsw driver keeps its TX channel:

- RX: every frame lands on the FAP. Frames of offloaded flows are
  rewritten and sent on the FAP TX channel, all the others go to the host
  through a queue.
- TX: the host transmits on its own channel as today, the FAP on its
  own, the switch takes both on the CPU port. This is the "TX splitting"
  of the stock firmware [SDK].

Splitting the work multiplies the processing capacity, not the link
capacity: everything still crosses the CPU port of the switch and, on the
DSL-3580L, the RGMII link to the BCM53125, at most 1 Gbit/s per
direction.

Since a stuck FAP would take the RX path down, the rest of the system
never depends on it:

1. the FAP boots with the driver and stays idle: it runs, posts its
   heartbeat and answers the mailbox, and touches nothing outside its own
   block;
2. the system behaves as if it did not exist: the enetsw driver keeps
   RX until the FAP is activated by hand or by a script;
3. the heartbeat only advances if the firmware main loop does, so a core
   spinning in interrupts or stuck in a loop stops it;
4. the host checks at low frequency the heartbeat and, while the FAP is
   active, its packet progress: if there is work pending (filled RX
   descriptors) and the FAP counters did not move, or they diverge
   (packets taken in but neither transmitted nor handed to the host), the
   host deactivates the FAP. An idle link moves nothing and is fine.

Deactivation, by hand or by the watchdog, moves RX back to the host,
which keeps working without offload; its TX channel was never touched.
A FAP that stalls or faults is also held in reset, with its log kept for
inspection.

Moving RX can be done in two ways; the second is preferred, and the
switch register mapping its CPU port queues to RX channels [SDK] makes
it possible:

- the FAP takes over the host RX channel, which the enetsw driver stops
  on activation and restarts on deactivation;
- the FAP gets an RX channel of its own and the switch maps its CPU port
  queues to it on activation and back on deactivation, if it can map
  queues to channels: then the host never stops anything.

Shared state needs coordination either way: the global controller
enable and the channel resets belong to the whole iuDMA, so the enetsw
driver must know about the FAP, and the interrupt of the FAP RX channel
has to be routed to the FAP with miscFapIrqMask [SDK].

## Offload plan

| Phase | Content | Exit criterion | Status |
|---|---|---|---|
| 0 | Loader, boot, mailbox, log, memory tests | The 4KE runs and talks to the host | done |
| 1 | Characterization: frequency from the CP0 Count register, latencies of registers, PSM, QSM, DDR and iuDMA, stack in DSPRAM, interrupts on the 4KE (mailbox and timer), WAIT. Heartbeat from the main loop, host watchdog holding a stalled 4KE in reset | The cycle budget per packet, and hence whether 700–800 Mbit/s is realistic | done |
| 2 | Host–FAP transport for received frames: a FAP to host queue of filled buffers with an interrupt, a host to FAP queue of free buffers, our own rings placed after the phase 1 costs | Frames generated by the FAP received by the host, checked and recycled; frame rate and host CPU load | implemented, to be run on hardware |
| 3 | RX delegation: on activation RX moves to the FAP, which hands every frame to the host through the phase 2 queues; on deactivation, or when the watchdog trips on packet progress, RX moves back. Only the test bench for phase 4, never a default | Activation and deactivation under traffic with the network still working afterwards; a firmware forced to stall or fault gives RX back to the host within one watchdog period | |
| 4 | Flow offload with TX splitting: the FAP gets its own TX channel. nf_flowtable with ndo_setup_tc(TC_SETUP_FT) on the enetsw conduit, which DSA already forwards; the host writes the flow table directly into PSM. The FAP does the 5-tuple lookup, NAT rewrite, TTL, incremental checksum update, port tag, and transmits on its channel; the rest goes to the host as in phase 3. Statistics and expirations go back to the host | Routing throughput with NAT and host CPU load, with and without offload | |

Today the enetsw driver reclaims transmitted descriptors in its NAPI
poll, which on the 6362 only the RX interrupt schedules, since the TX
DMA interrupts do not reach the host. With RX on the FAP, the interrupt
the FAP raises for the phase 2 queue schedules that poll instead.

With the host on logical channel 0, as the OpenWrt device tree sets it
(iuDMA 0 and 1), the FAP takes logical channel 1 (iuDMA 2 and 3). RX
delegation then means writing 1 to the 8 queue fields of
`REG_IUDMA_QUEUE_CTRL`, and deactivation writing 0 back: one register,
all traffic at once, whatever the ingress port.

## Phase 1: characterization

The firmware now:

- maps the DSPRAM in the startup code and runs with the stack at its top,
  as the stock firmware does; the memory test covers the rest of it;
- reads the CP0 Count resolution with `rdhwr $3`;
- times 8192 loads and 8192 stores on one word of DSPRAM, PSM, QSM, the
  scratch control register and DDR, plus the same loop with adds only,
  and DDR loads alternating between two words 4 KB and 32 KB apart, to
  see the cost of leaving the open row;
- runs on interrupts: the CP0 timer on IP7 every `FAP_TIMER_PERIOD`
  Count ticks, and the mailbox on IP2 through `irq_4ke_mask` bit 5 with
  `FAP_GP_MBOX_OUT` added to `gp_mask`;
- waits for the host to take each message before writing the next one;
- counts its main loop iterations in DSPRAM; the timer posts a heartbeat
  only if they moved since the previous tick;
- sleeps in WAIT between interrupts. If the Count stopped in WAIT the
  timer would never wake the core, the heartbeat would stall and the
  watchdog would report the core as crashed;
- on a host command, reads the ENETSW iuDMA at its host physical address
  through kseg1 (controller configuration, configuration and ring start
  of channels 0 and 1) and times loads of a register and of the state
  RAM. It only reads: the enetsw driver owns the iuDMA;
- on another host command, reads its own shared area at the host physical
  address of the block, outside the translator windows, and times it;
- sets EBase onto the block through the boot window at boot, so that the
  low translator at 0 is no longer needed for the vectors;
- on a third one, loads once from any 4KE virtual address the host
  gives, to see what the 4KE reaches without a new firmware.

The host side boots the firmware at probe and leaves it idle, holds the
4KE in reset when the heartbeat stalls for a second or the 4KE reports an
exception, answers pings synchronously and adds the `state`, `clock` and
`stats` debugfs files. The log and the statistics stay readable after the
watchdog has stopped the core.

### Running it on OpenWrt

Build the package (`make package/bcm6362-fap/compile`), install
`kmod-bcm6362-fap` on a board whose device tree has the FAP node, then:

```
insmod bcm6362-fap
cd /sys/kernel/debug/11001000.coprocessor
cat state          # idle
cat log
cat clock          # takes one second
echo 0x1234 > mbox
cat stats
cat iudma
devmem 0x1000d800; devmem 0x1000da00; devmem 0x1000dc00
cat hostbench
cat ddr
```

`hostbench` times host loads and stores on the FAP scratch register, QSM,
PSM and DDR, uncached and cached, in ns per access: it decides where the
phase 2 queues go. It times plain accesses and the MMIO accessors
separately: on MIPS every accessor, the relaxed ones included, issues a
sync before the access and the non relaxed reads another one after, so a
queue touched word by word with them pays a sync per word.

`iudma` should report the same `controller_cfg`, channel 0 `cfg` and
channel 0 `ring` as the three `devmem` reads on the host. If the 4KE
cannot reach the iuDMA there, the read either faults, and the log shows
the exception, or never completes, and the watchdog stops the core. A
transaction stuck on the bus could also hang the host, so run it last,
with a serial console at hand.

`state` reads `crashed` after the watchdog stopped the core; the reason
is in the kernel log.

What to read:

- `log`: boot registers; `intctl` bits 31:29 are the CPU line of the timer,
  `count res` the cycles per Count tick.
- `clock`: Count rate and core frequency, measured against the host clock
  across two pings one second apart.
- `stats`:
  - `heartbeat` should grow by Count rate / `FAP_TIMER_PERIOD` per second
    while the main loop runs;
  - `timer latency` is the time from the Compare match to the handler, in
    Count ticks: the interrupt entry cost;
  - `pings` counts the pings answered, `lost` should stay at 0;
  - the access table gives cycles per access; `alu` is the loop cost with
    an add in place of each load; the `ddr ... apart` lines are the loads
    alternating between two distant words.
- `ddr`: the magic read back means the 4KE reaches any DDR address
  directly. Like `iudma`, a wrong guess shows up as an exception or a
  watchdog stop, so run it last.
- `peek`: write a 4KE virtual address, read back the word and the
  CP0 Count ticks the load took. Only one load is done, since registers
  may have read side effects, but an address nothing answers on can
  still hang the bus. With the Fixed Mapping MMU, kseg0/kseg1 reach
  physical addresses below 512 MB and kuseg maps virtual v to physical
  v + 0x40000000, uncached here (Config.KU = 2): the PCIe window at
  physical 0xa0000000 is at virtual 0x60000000 [ded.].

### From the measurements to the budget

At a core clock f the cycles available per packet are
f / (T / (8 * (L + 20))) for a throughput T in bit/s and frames of L
bytes (20 bytes of preamble and inter-frame gap). At the measured
166.67 MHz and 800 Mbit/s that is about 2500 cycles per 1500 byte frame
and 140 per 64 byte frame.

The budget is shared by both directions: a TCP download at 800 Mbit/s is
about 66 kpps of data plus 33 kpps of ACKs, about 1700 cycles per packet
on average. A forwarded packet needs some ten header loads from DDR:
about 330 cycles through a translator window (one row change, then the
same row), about 560 at the host physical address. With a PSM table
lookup (about 150) plus descriptors and stores it stays under 1000
cycles through a window, so 700–800 Mbit/s with full size frames looks
reachable, while small frames at line rate do not. The low translator,
freed by moving EBase, is worth mapping over the packet buffers. The
iuDMA itself is the expensive part, over 40 cycles per load, more than
DDR: the per packet path should read descriptor ownership from memory
rather than the iuDMA registers, and only write to the iuDMA (channel
kick, interrupt acknowledge).

For the phase 2 queues each side should write across the bus and read
locally. Host loads from the FAP memories cost 186 ns, and from
uncached DDR 253 ns, against 10 ns from its cache; host stores to the
FAP memories cost 72–84 ns, so they are worth batching. Hence the FAP to
host queue lives in DDR, written by the 4KE and read by the host through
its cache, and the host to FAP queue of free buffers lives in QSM or PSM,
read by the 4KE at 7 cycles. MMIO accessors are only for doorbells.

## Phase 2: receive queues

Each side writes across the bus and reads locally:

- free buffers, host to FAP: a ring of buffer addresses in QSM, with its
  producer index, written by the host in batches and read by the 4KE at
  7 cycles per word. The host posts its whole pool at start and gives
  every buffer back as soon as it is done with it;
- filled buffers, FAP to host: a ring of one word entries in DDR, written
  by the 4KE and read by the host through its cache. An entry holds the
  buffer address, the frame length and a phase bit that flips at every
  pass, so the host needs no producer index. The host publishes its
  consumer index in QSM;
- notification: the host arms a flag in QSM when it has drained the ring;
  the 4KE clears it and sends a mailbox message when it posts entries,
  then the host drains again in a work item, arms, and looks once more.

Buffers are 2 KB, aligned, below 256 MB. The 4KE reaches them, and the
ring, through the low translator, which the test maps over the whole RAM
right above it in the 4KE space (host address a at 4KE address
RAM size + a). The exception base set at boot is what frees that
translator; the test refuses to start if EBase did not move.

On the cache side, the BMIPS4350 does not prefetch speculatively, so the
kernel makes the sync for the CPU a no-op: the host drops the ring line
from its cache after every read, otherwise it would keep reading the same
stale entry.

The test stands in for phase 3: the 4KE writes a numbered 64 byte header
in each buffer, where the iuDMA will write whole frames, and reports a
1514 byte frame. The host checks each header and the numbering.

```
cd /sys/kernel/debug/11001000.coprocessor
echo start > rxtest; sleep 10; cat rxtest; echo stop > rxtest
cat rxtest
cat stats
```

`rxtest` shows on the host side the frames received, header errors,
numbering gaps and bad entries, the frame rate and the share of one CPU
spent in the work item; on the 4KE side the frames posted, the
notifications and how often it found no free buffer or a full ring.
After a run the `qsm` file no longer matches the test pattern, since the
rings live there.

## Upstream

- The host driver boots firmware on a coprocessor, which is what
  remoteproc is for: start/stop on `soft_resets`, the DDR block as a
  carveout mapped by the two translators, ELF loading with `da_to_va`
  translating 4KE addresses, the log as a trace buffer from a resource
  table in the firmware. The mailbox becomes a mailbox controller for
  whatever sits on top (the offload driver). The characterization in
  debugfs stays out of tree. remoteproc boots the core at probe with
  `auto_boot`, and the watchdog reports through `rproc_report_crash()`;
  activation and deactivation belong to the offload driver on top.
- A `brcm,bcm6362-fap` YAML binding, and the node added to
  `arch/mips/boot/dts/brcm/bcm6362.dtsi`. The clock, reset and power
  domain constants are already upstream.
- The firmware binary goes to linux-firmware, with the source in this
  repository referenced from WHENCE.
- Phases 3 and 4 hook into the ENETSW driver, which mainline does not
  have for bmips (OpenWrt carries `bcm6368-enetsw`): that driver has to be
  upstream before the offload can be.

## Device tree examples

`dts/` holds the device tree the loader was developed on: the BCM6362 SoC
include with the `brcm,bcm6362-fap` node and the D-Link DSL-3580L board.
They build in the OpenWrt bmips target; besides the FAP node, the SoC include
carries a `wlan` bus node and different PCIe ranges compared to the OpenWrt
one, and the board relies on OpenWrt specific bindings (bcma fallback SPROM).
