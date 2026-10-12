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

### Access cost

Cycles per access from the 4KE, with the loop overhead taken out. Loads
are followed by an instruction using the loaded value. Every access hits
the same word, so the DDR figure is a best case with the row already open.

| Target | Load | Store |
|---|---|---|
| DSPRAM | 2 | 1 |
| PSM, QSM | 7 | 4.6 |
| FAP control registers | 6 | 6.6 |
| DDR | 30.6 | 7.6 |
| ENETSW iuDMA registers | 42 | not measured |
| ENETSW iuDMA state RAM | 43 | not measured |

Source: [HW], phase 1 benchmark. Stores go through the write buffer.
The iuDMA is only read, since the enetsw driver owns it.

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

- what sits on COP2;
- DQM semantics beyond the header structs;
- the cost of stores to the iuDMA, and whether the iuDMA can use descriptors in QSM, as the stock firmware does for ENET RX;
- the cost of host accesses to QSM and PSM over UBUS;
- the DDR latency with row misses, and the cost of the FAP internal DMA, which could bring packet headers into PSM;

## Operating model

When the FAP is active it is the only owner of the ENETSW iuDMA. Since a
stuck FAP would then take the whole network down, the rest of the system
never depends on it:

1. the FAP boots with the driver and stays idle: it runs, posts its
   heartbeat and answers the mailbox, and touches nothing outside its own
   block;
2. the system behaves as if it did not exist: the enetsw driver keeps the
   iuDMA until the FAP is activated by hand or by a script;
3. the heartbeat only advances if the firmware main loop does, so a core
   spinning in interrupts or stuck in a loop stops it;
4. the host checks at low frequency the heartbeat and, while the FAP is
   active, its packet progress: if there is work pending (filled RX
   descriptors, host packets queued for TX) and the FAP counters did not
   move, or they diverge (packets taken in but neither transmitted nor
   handed to the host), the host deactivates the FAP. An idle link moves
   nothing and is fine.

Deactivation, by hand or by the watchdog, gives the iuDMA back to the
host driver, which keeps working without offload. A FAP that stalls or
faults is also held in reset, with its log kept for inspection.

## Offload plan

| Phase | Content | Exit criterion | Status |
|---|---|---|---|
| 0 | Loader, boot, mailbox, log, memory tests | The 4KE runs and talks to the host | done |
| 1 | Characterization: frequency from the CP0 Count register, latencies of registers, PSM, QSM and DDR, stack in DSPRAM, interrupts on the 4KE (mailbox and timer). Heartbeat from the main loop, host watchdog holding a stalled 4KE in reset | The cycle budget per packet, and hence whether 700–800 Mbit/s is realistic | done |
| 2 | Host–FAP transport: DQM queues in QSM with a "not empty" interrupt. Alternatively, our own rings in PSM | A ping that goes through the queues instead of the mailbox | |
| 3 | Activation: on an activation command the enetsw driver stops its iuDMA channels and the FAP takes RX and TX, with the RX interrupts routed to the FAP; the host receives and transmits through the queues. On deactivation, or when the watchdog trips on packet progress, the host takes the channels back and the FAP returns idle. Only the test bench for phase 4, never a default | Activation and deactivation under traffic with the network still working afterwards; a firmware forced to stall or fault gives the network back to the host within one watchdog period | |
| 4 | Flow offload, the FAP in control: nf_flowtable with ndo_setup_tc(TC_SETUP_FT) on the enetsw conduit, which DSA already forwards. Flow table in PSM. The FAP does the 5-tuple lookup, NAT rewrite, TTL, incremental checksum update, port tag, and transmits directly; the rest goes to the host as in phase 3. Statistics and expirations go back to the host | Routing throughput with NAT and host CPU load, with and without offload | |

Today the enetsw driver reclaims transmitted descriptors in its NAPI
poll, which on the 6362 only the RX interrupt schedules, since the ENETSW
has no TX interrupt lines. With the FAP owning TX, completions can reach
the host through the queues.

## Phase 1: characterization

The firmware now:

- maps the DSPRAM in the startup code and runs with the stack at its top,
  as the stock firmware does; the memory test covers the rest of it;
- reads the CP0 Count resolution with `rdhwr $3`;
- times 8192 loads and 8192 stores on one word of DSPRAM, PSM, QSM, the
  scratch control register and DDR, plus the same loop with adds only;
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
  RAM. It only reads: the enetsw driver owns the iuDMA.

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
```

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
    an add in place of each load.

### From the measurements to the budget

At a core clock f the cycles available per packet are
f / (T / (8 * (L + 20))) for a throughput T in bit/s and frames of L
bytes (20 bytes of preamble and inter-frame gap). At the measured
166.67 MHz and 800 Mbit/s that is about 2500 cycles per 1500 byte frame
and 140 per 64 byte frame.

The budget is shared by both directions: a TCP download at 800 Mbit/s is
about 66 kpps of data plus 33 kpps of ACKs, about 1700 cycles per packet
on average. A forwarded packet needing some ten header loads from DDR
(about 300 cycles), a PSM table lookup (about 150) plus descriptors and
stores stays well under 1000 cycles, so 700–800 Mbit/s with full size
frames looks reachable, while small frames at line rate do not. The
iuDMA itself is the expensive part, over 40 cycles per load, more than
DDR: the per packet path should read descriptor ownership from memory
rather than the iuDMA registers, and only write to the iuDMA (channel
kick, interrupt acknowledge).

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
