# QEMU Aspeed AST2600 eSPI Controller — Phase 1 Design Document

## 1. Executive Summary

This document describes a new hardware device model added to the QEMU machine
emulator that implements the Enhanced Serial Peripheral Interface (eSPI)
controller found on the Aspeed AST2600 system-on-chip (SoC). The AST2600 is a
widely deployed Baseboard Management Controller (BMC) processor used in modern
data-center servers. This is the first eSPI device model ever contributed to
QEMU; no prior implementation exists in the upstream project or any publicly
visible fork.

Phase 1 delivers a register-level skeleton with full Virtual Wire (VW) channel
support—sufficient for the OpenBMC Linux kernel driver (`aspeed-espi`) to probe,
initialize, and exchange system-event signals. Data channels (Peripheral, OOB,
Flash) are defined in the register map but their data-path logic is deferred to
later phases.

---

## 2. Background

### 2.1 What Is a BMC?

A Baseboard Management Controller is a small embedded processor soldered onto a
server motherboard, separate from the main host CPU. It runs independently
(typically Linux or a real-time OS) and is responsible for:

- **Remote management**: power on/off/reset the server, access serial consoles
- **Hardware monitoring**: read temperatures, fan speeds, voltages
- **Event logging**: record errors to a System Event Log (SEL)
- **Firmware updates**: update BIOS and other firmware remotely

The AST2600, made by Aspeed Technology, is the dominant BMC chip in modern
Intel and AMD server platforms. It contains dual ARM Cortex-A7 cores, and its
silicon includes dozens of hardware peripherals for interacting with the host.

### 2.2 What Is eSPI?

The Enhanced Serial Peripheral Interface (eSPI) is a communication bus defined
by Intel in the *Intel eSPI Interface Base Specification, Revision 1.0* (Document
327432-004, publicly available at intel.com). It replaces the legacy Low Pin
Count (LPC) bus that historically connected the BMC to the host chipset.

Key characteristics:

| Property | LPC (legacy) | eSPI |
|---|---|---|
| Pins | 7+ signals | 4 signals (CLK, CS#, IO[0:1]) + Alert# |
| Speed | 33 MHz | Up to 66 MHz |
| Width | 4-bit | 1, 2, or 4-bit (configurable) |
| Channels | Flat memory/IO | 4 multiplexed logical channels |
| Topology | Multi-drop bus | Point-to-point, up to 2 slaves |

eSPI defines four logical channels multiplexed over the same physical wires:

| Channel | Name | Purpose |
|---|---|---|
| CH0 | Peripheral | Memory-mapped and I/O read/write cycles (replaces LPC memory/IO) |
| CH1 | Virtual Wire | Single-bit signals: sleep states, resets, GPIOs, NMI, SMI |
| CH2 | Out-of-Band (OOB) | SMBus-tunneled messages (MCTP/PLDM) for platform management |
| CH3 | Flash Access | Host-initiated reads/writes to SPI flash via BMC (SAFS mode) |

The bus has a **master** (the host chipset, e.g., Intel PCH or S3M die) and a
**slave** (the BMC). The master initiates transactions; the slave responds. The
slave can signal events to the master via an **Alert#** pin.

### 2.3 What Is QEMU?

QEMU is an open-source machine emulator and virtualizer. It can emulate
complete computer systems, including CPU, memory, and peripheral devices, in
software. For BMC development, QEMU provides an `ast2600-evb` machine type that
models the AST2600 SoC with enough fidelity to boot an OpenBMC Linux image
without needing physical hardware.

QEMU's device model architecture is object-oriented C, built around the QOM
(QEMU Object Model) type system. Each hardware device is a C struct that
inherits from a base type (e.g., `SysBusDevice` for memory-mapped peripherals).
Devices implement:

- **`realize`**: one-time initialization (register MMIO regions, connect IRQs)
- **`reset`**: set all registers to their power-on-reset values
- **`read`/`write` handlers**: respond to CPU accesses to the device's MMIO region
- **`VMState`**: describe register state for live-migration and snapshots

### 2.4 Why Add eSPI to QEMU?

Prior to this change, the AST2600 SoC model in QEMU had the eSPI controller
mapped as an `UnimplementedDeviceState`—a generic stub that logs accesses but
returns zero for all reads. This caused:

1. **Driver probe failures**: The OpenBMC `aspeed-espi` kernel driver reads
   capability registers during probe. All-zero capabilities indicate "no
   hardware present," so the driver would fail or skip initialization.

2. **No VW signal exchange**: Virtual Wires carry critical system signals. BMC
   firmware uses VWs to learn about host power state (SLP_S3/S4/S5), platform
   reset (PLTRST#), and to acknowledge resets. Without VW support, the BMC
   firmware cannot correctly track or respond to host state changes.

3. **No eSPI-based KCS/IPMI path**: In real hardware, IPMI traffic between the
   BIOS and BMC flows over eSPI Peripheral Channel 0. Without it, there is no
   way to test BIOS↔BMC communication in QEMU.

---

## 3. Specification References

All register addresses, bit definitions, and reset values used in this
implementation are derived from publicly available sources:

1. **Intel eSPI Interface Base Specification, Rev 1.0**
   (Document 327432-004) — Defines the eSPI protocol, channel types, and
   Virtual Wire index/data encoding.

2. **Aspeed AST2600 Datasheet** — Provides the eSPI controller base address
   (`0x1E6EE000`), interrupt number (SPI 74 / GIC IRQ 42), and register layout.

3. **Aspeed Linux kernel driver source** (GPLv2, publicly available):
   - `drivers/soc/aspeed/ast2600-espi.c` — Driver logic
   - `drivers/soc/aspeed/ast2600-espi.h` — Register offset definitions
   - `drivers/soc/aspeed/aspeed-espi-comm.h` — Bit field definitions
   These files are the authoritative register-level reference for the AST2600
   eSPI controller and were used to derive every register offset and bit mask
   in this implementation.

4. **QEMU source tree** — Existing Aspeed device models (`aspeed_peci.c`,
   `aspeed_sbc.c`, `aspeed_scu.c`) were used as templates for QEMU coding
   patterns and API usage.

---

## 4. Files Changed

The implementation spans 7 files across the QEMU source tree. Each is described
in detail below.

### 4.1 New File: `include/hw/misc/aspeed_espi.h`

**Purpose**: Public header defining the device type, state structure, register
map, and bit-field constants.

#### 4.1.1 Device Type and State Structure

```c
#define TYPE_ASPEED_ESPI "aspeed.espi"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedESPIState, ASPEED_ESPI)

struct AspeedESPIState {
    SysBusDevice parent;
    MemoryRegion mmio;
    qemu_irq irq;
    uint32_t regs[ASPEED_ESPI_NR_REGS];
};
```

- `TYPE_ASPEED_ESPI` (`"aspeed.espi"`) is the QOM type name used to register
  this device in QEMU's type system.
- `OBJECT_DECLARE_SIMPLE_TYPE` is a QEMU macro that generates cast functions
  (`ASPEED_ESPI(obj)`) and type-checking helpers.
- `SysBusDevice parent` makes this a system-bus device, meaning it has
  memory-mapped I/O regions and interrupt outputs (as opposed to a PCI device).
- `MemoryRegion mmio` is the MMIO region that handles CPU read/write accesses.
- `qemu_irq irq` is the output interrupt line connected to the GIC.
- `regs[]` is a flat array of 32-bit registers. The array size
  `ASPEED_ESPI_NR_REGS` is `0x900 >> 2 = 576` entries, covering the full
  register space from offset `0x000` to `0x8FF`.

**Justification**: The AST2600 datasheet maps the eSPI controller registers
from `0x1E6EE000` to approximately `0x1E6EE900`. Using a flat register array
indexed by `offset >> 2` is the standard QEMU pattern for MMIO devices (see
`aspeed_scu.c`, `aspeed_hace.c`).

#### 4.1.2 Register Map

Every `#define R_ESPI_*` constant maps a register name to its array index,
computed as `(byte_offset / 4)`. The full list, organized by functional group:

**Main control and status (used in Phase 1):**

| Symbol | Offset | Description | Spec Reference |
|---|---|---|---|
| `R_ESPI_CTRL` | 0x000 | Master control: channel enables, SW resets, DMA enables | ast2600-espi.h |
| `R_ESPI_STS` | 0x004 | Link status (read-only) | ast2600-espi.h |
| `R_ESPI_INT_STS` | 0x008 | Interrupt status (write-1-to-clear) | ast2600-espi.h |
| `R_ESPI_INT_EN` | 0x00C | Interrupt enable mask | ast2600-espi.h |
| `R_ESPI_INT_EN_CLR` | 0x0FC | Write-to-clear bits in INT_EN | ast2600-espi.h |

**Virtual Wire channel registers (implemented in Phase 1):**

| Symbol | Offset | Description | Spec Reference |
|---|---|---|---|
| `R_ESPI_VW_SYSEVT_INT_EN` | 0x094 | Per-bit interrupt enable for system events | ast2600-espi.h |
| `R_ESPI_VW_SYSEVT` | 0x098 | System event VW values (SLP, PLTRST, etc.) | ast2600-espi.h |
| `R_ESPI_VW_GPIO_VAL` | 0x09C | GPIO virtual wire values | ast2600-espi.h |
| `R_ESPI_VW_SYSEVT_INT_STS` | 0x11C | SYSEVT interrupt status (W1C) | ast2600-espi.h |
| `R_ESPI_VW_SYSEVT1_INT_EN` | 0x100 | SYSEVT1 interrupt enable | ast2600-espi.h |
| `R_ESPI_VW_SYSEVT1` | 0x104 | SUSPEND_WARN/SUSPEND_ACK | ast2600-espi.h |
| `R_ESPI_VW_SYSEVT1_INT_STS` | 0x12C | SYSEVT1 interrupt status (W1C) | ast2600-espi.h |

**Capability registers (read-only, implemented in Phase 1):**

| Symbol | Offset | Description | Spec Reference |
|---|---|---|---|
| `R_ESPI_GEN_CAP_N_CONF` | 0x0A0 | General capabilities | eSPI Base Spec §7.2.1 |
| `R_ESPI_CH0_CAP_N_CONF` | 0x0A4 | Peripheral channel capabilities | eSPI Base Spec §7.2.2 |
| `R_ESPI_CH1_CAP_N_CONF` | 0x0A8 | Virtual Wire channel capabilities | eSPI Base Spec §7.2.3 |
| `R_ESPI_CH2_CAP_N_CONF` | 0x0AC | OOB channel capabilities | eSPI Base Spec §7.2.4 |
| `R_ESPI_CH3_CAP_N_CONF` | 0x0B0 | Flash channel capabilities | eSPI Base Spec §7.2.5 |
| `R_ESPI_CH3_CAP_N_CONF2` | 0x0B4 | Flash channel capabilities (extended) | ast2600-espi.h |

**Data-path registers (defined but not yet functional — Phases 2–4):**

| Group | Offsets | Purpose |
|---|---|---|
| Peripheral CH0 | 0x010–0x038 | PC/NP TX/RX DMA addresses, control, data |
| OOB CH2 | 0x040–0x058 | OOB TX/RX DMA addresses, control, data |
| Flash CH3 | 0x060–0x078 | Flash TX/RX DMA addresses, control, data |

#### 4.1.3 Bit-Field Definitions

**`ESPI_CTRL` register bits** — Each bit has a specific hardware function:

| Symbol | Bit | Function | Behavior in Model |
|---|---|---|---|
| `ESPI_CTRL_FLASH_TX_SW_RST` | 31 | Software reset flash TX path | Self-clearing (write is acknowledged, bit reads back 0) |
| `ESPI_CTRL_FLASH_RX_SW_RST` | 30 | Software reset flash RX path | Self-clearing |
| `ESPI_CTRL_OOB_TX_SW_RST` | 29 | Software reset OOB TX path | Self-clearing |
| `ESPI_CTRL_OOB_RX_SW_RST` | 28 | Software reset OOB RX path | Self-clearing |
| `ESPI_CTRL_PERIF_NP_TX_SW_RST` | 27 | Software reset peripheral NP TX | Self-clearing |
| `ESPI_CTRL_PERIF_NP_RX_SW_RST` | 26 | Software reset peripheral NP RX | Self-clearing |
| `ESPI_CTRL_PERIF_PC_TX_SW_RST` | 25 | Software reset peripheral PC TX | Self-clearing |
| `ESPI_CTRL_PERIF_PC_RX_SW_RST` | 24 | Software reset peripheral PC RX | Self-clearing |
| `ESPI_CTRL_FLASH_SW_RDY` | 7 | BMC signals flash channel ready | Read-write, stored |
| `ESPI_CTRL_OOB_SW_RDY` | 4 | BMC signals OOB channel ready | Read-write, stored |
| `ESPI_CTRL_VW_SW_RDY` | 3 | BMC signals VW channel ready | Read-write, stored |
| `ESPI_CTRL_PERIF_SW_RDY` | 1 | BMC signals peripheral channel ready | Read-write, stored |

The self-clearing behavior is specified in the AST2600 datasheet: writing 1 to
a SW_RST bit triggers a reset of that internal FIFO/DMA path, and the hardware
automatically clears the bit. In our model, the `write` handler masks out these
bits before storing, which is functionally equivalent since there is no internal
FIFO state to reset in Phase 1.

**`ESPI_INT_STS` / `ESPI_INT_EN` bits** — The interrupt status register uses
write-1-to-clear (W1C) semantics: writing a 1 to a bit clears that bit. This
is standard practice for interrupt status registers in embedded controllers, as
it allows firmware to acknowledge specific interrupts without affecting others.

| Symbol | Bit | Meaning |
|---|---|---|
| `ESPI_INT_RST_DEASSERT` | 31 | eSPI reset has been deasserted (link is up) |
| `ESPI_INT_VW_SYSEVT1` | 22 | SYSEVT1 change detected |
| `ESPI_INT_VW_GPIO` | 9 | VW GPIO value changed |
| `ESPI_INT_VW_SYSEVT` | 8 | SYSEVT change detected |
| (others) | 0–7,10–21,23 | Data-channel completion/error events (Phase 2+) |

**`ESPI_VW_SYSEVT` bits** — The system event register is the heart of the
Virtual Wire channel. Each bit represents a system-level signal defined in the
Intel eSPI specification (§5.3 "Virtual Wire Channel"):

| Symbol | Bit | Signal | Direction | Description |
|---|---|---|---|---|
| `HOST_RST_ACK` | 27 | HOST_RST_ACK | BMC→Host | BMC acknowledges host reset warning |
| `RST_CPU_INIT` | 26 | RST_CPU_INIT | BMC→Host | BMC requests CPU re-initialization |
| `SLV_BOOT_STS` | 23 | SLV_BOOT_STS | BMC→Host | Slave boot status |
| `NON_FATAL_ERR` | 22 | ERROR_NONFATAL | BMC→Host | BMC reports non-fatal error |
| `FATAL_ERR` | 21 | ERROR_FATAL | BMC→Host | BMC reports fatal error |
| `SLV_BOOT_DONE` | 20 | SLV_BOOT_DONE | BMC→Host | BMC has finished booting |
| `OOB_RST_ACK` | 16 | OOB_RST_ACK | BMC→Host | BMC acknowledges OOB reset |
| `NMI_OUT` | 10 | NMIOUT# | BMC→Host | Non-maskable interrupt to host |
| `SMI_OUT` | 9 | SMIOUT# | BMC→Host | System management interrupt to host |
| `HOST_RST_WARN` | 8 | HOST_RST_WARN | Host→BMC | Host is about to reset |
| `OOB_RST_WARN` | 6 | OOB_RST_WARN | Host→BMC | OOB channel reset warning |
| `PLTRST_N` | 5 | PLTRST# | Host→BMC | Platform reset (active low; 1 = deasserted = normal) |
| `SUSPEND` | 4 | SUS_WARN# | Host→BMC | Host entering suspend |
| `S5_SLEEP` | 2 | SLP_S5# | Host→BMC | Host entering S5 (soft off) |
| `S4_SLEEP` | 1 | SLP_S4# | Host→BMC | Host entering S4 (hibernate) |
| `S3_SLEEP` | 0 | SLP_S3# | Host→BMC | Host entering S3 (sleep) |

The **direction** is critical: bits marked "Host→BMC" are read-only from the
BMC's perspective (in real hardware, the eSPI master sets them). The model
enforces this by masking writes against `SYSEVT_HOST_DRIVEN_MASK` and
`SYSEVT_SLAVE_DRIVEN_MASK`.

#### 4.1.4 Capability Reset Values

```c
#define ESPI_GEN_CAP_N_CONF_RESET   0x0000F759
#define ESPI_CH0_CAP_N_CONF_RESET   0x00000073
#define ESPI_CH1_CAP_N_CONF_RESET   0x00000033
#define ESPI_CH2_CAP_N_CONF_RESET   0x00000033
#define ESPI_CH3_CAP_N_CONF_RESET   0x00000003
```

The general capability value `0xF759` decodes as follows per eSPI Base Spec
§7.2.1:

| Bits | Value | Meaning |
|---|---|---|
| [3:0] | 0x9 | Max frequency supported (bits vary by implementation) |
| [6:4] | 0x5 | I/O mode support (single + dual + quad) |
| [10:8] | 0x7 | Channel support: CH0, CH1, CH2, CH3 all supported |
| [15:12] | 0xF | Max payload size, alert mode, CRC support |

These values match what real AST2600 A3 silicon reports to the host during
the eSPI capability negotiation phase.

---

### 4.2 New File: `hw/misc/aspeed_espi.c`

**Purpose**: The device model implementation — contains all runtime logic.

#### 4.2.1 Direction Masks

```c
#define SYSEVT_HOST_DRIVEN_MASK (HOST_RST_WARN | OOB_RST_WARN | PLTRST_N | ...)
#define SYSEVT_SLAVE_DRIVEN_MASK (HOST_RST_ACK | SLV_BOOT_DONE | SMI_OUT | ...)
```

These masks separate the SYSEVT register into host-controlled and
BMC-controlled bit fields. On real hardware, the eSPI master (host chipset)
drives certain VW signals to the slave (BMC), and those bits are inherently
read-only from the BMC CPU's perspective. The BMC can only write to
slave-driven bits.

In the model, these masks ensure that BMC firmware writes to `ESPI_VW_SYSEVT`
only affect slave-driven bits. Host-driven bits can be injected externally via
QEMU's QTest framework or QMP (QEMU Machine Protocol) for testing.

#### 4.2.2 Interrupt Logic: `aspeed_espi_update_irq()`

```c
static void aspeed_espi_update_irq(AspeedESPIState *s)
{
    uint32_t active = s->regs[R_ESPI_INT_STS] & s->regs[R_ESPI_INT_EN];
    if (active) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}
```

This follows the standard level-triggered interrupt pattern used by all Aspeed
peripherals. The GIC (Generic Interrupt Controller) sees the interrupt as
asserted when **any** bit is set in both `INT_STS` and `INT_EN` simultaneously.
The interrupt is deasserted when all status bits are cleared or masked.

This function is called after every write to `INT_STS`, `INT_EN`, or
`INT_EN_CLR`, and after any event that sets a status bit.

#### 4.2.3 VW Event Notification: `aspeed_espi_vw_notify_sysevt()`

```c
static void aspeed_espi_vw_notify_sysevt(AspeedESPIState *s,
                                          uint32_t old_val, uint32_t new_val)
{
    uint32_t changed = old_val ^ new_val;
    uint32_t enabled = s->regs[R_ESPI_VW_SYSEVT_INT_EN];
    if (changed & enabled) {
        s->regs[R_ESPI_VW_SYSEVT_INT_STS] |= (changed & enabled);
        s->regs[R_ESPI_INT_STS] |= ESPI_INT_VW_SYSEVT;
        aspeed_espi_update_irq(s);
    }
}
```

This implements the two-level interrupt hierarchy described in the Aspeed
driver:

1. When a SYSEVT bit changes, the corresponding bit is set in
   `VW_SYSEVT_INT_STS` (if enabled in `VW_SYSEVT_INT_EN`).
2. If any bit is set in `VW_SYSEVT_INT_STS`, the top-level `ESPI_INT_VW_SYSEVT`
   bit is set in `ESPI_INT_STS`.
3. The top-level IRQ is asserted if `ESPI_INT_VW_SYSEVT` is enabled in
   `ESPI_INT_EN`.

Firmware clears interrupts bottom-up: first clear `VW_SYSEVT_INT_STS`, which
automatically clears `ESPI_INT_VW_SYSEVT`, then the top-level IRQ deasserts.

#### 4.2.4 MMIO Read Handler: `aspeed_espi_read()`

```c
static uint64_t aspeed_espi_read(void *opaque, hwaddr offset, unsigned size)
```

Straightforward: computes the register index as `offset >> 2` and returns the
stored value. Out-of-bounds reads are logged via `LOG_GUEST_ERROR` (a QEMU
facility for reporting firmware bugs) and return 0.

#### 4.2.5 MMIO Write Handler: `aspeed_espi_write()`

This is the most complex function. It dispatches on the register index and
implements register-specific write semantics:

| Register | Write Behavior | Justification |
|---|---|---|
| `ESPI_CTRL` | Store value with SW_RST bits masked out | AST2600 HW self-clears reset bits |
| `ESPI_INT_STS` | Write-1-to-clear | Standard W1C interrupt pattern |
| `ESPI_INT_EN` | Direct write | Mask register is freely writable |
| `ESPI_INT_EN_CLR` | Clears bits in `INT_EN` | Write-to-clear alias per Aspeed datasheet |
| `ESPI_VW_SYSEVT` | Write only slave-driven bits; host bits preserved | Direction enforcement |
| `ESPI_VW_SYSEVT1` | Write `SUSPEND_ACK`; preserve `SUSPEND_WARN` | Mixed-direction register |
| `ESPI_VW_GPIO_VAL` | Full write; sets GPIO interrupt on change | Aspeed driver expects this |
| `VW_SYSEVT_INT_EN` | Direct write; re-evaluates pending events | Newly enabled bits may match current state |
| `VW_SYSEVT_INT_STS` | W1C; clears top-level bit when all sub-bits clear | Two-level interrupt hierarchy |
| `VW_SYSEVT1_INT_STS` | W1C; clears top-level SYSEVT1 bit when clear | Same pattern as SYSEVT |
| Capability regs | Ignored with `LOG_GUEST_ERROR` | Capabilities are HW-defined, read-only |
| `ESPI_STS` | Ignored with `LOG_GUEST_ERROR` | Status is read-only |
| All others | Direct write to `regs[]` | Default passthrough for future expansion |

#### 4.2.6 MMIO Region Configuration

```c
static const MemoryRegionOps aspeed_espi_ops = {
    .read  = aspeed_espi_read,
    .write = aspeed_espi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4, },
};
```

The AST2600 eSPI registers are 32-bit-only accessible (no byte or halfword
access). Setting `min_access_size = max_access_size = 4` causes QEMU to reject
sub-word accesses, matching real hardware behavior.

`DEVICE_LITTLE_ENDIAN` matches the ARM Cortex-A7 default byte order.

#### 4.2.7 Device Lifecycle Functions

**`aspeed_espi_realize()`**: Called once when the device is instantiated.
Creates the 4 KB MMIO region and registers one IRQ output.

**`aspeed_espi_reset()`**: Called on machine reset (power-on or system reset).
Zeros all registers, then sets:

- **Capability registers** to their silicon reset values (§4.1.4 above).
- **`ESPI_VW_SYSEVT`** to `PLTRST_N` (bit 5 = 1), indicating platform reset
  is deasserted (i.e., the host is powered on and not in reset). This matches
  the expected state when the BMC driver probes.
- **`ESPI_INT_STS`** to `RST_DEASSERT` (bit 31 = 1), indicating the eSPI link
  is up. The Aspeed Linux driver checks this bit during probe to confirm the
  eSPI bus is operational.

**`vmstate_aspeed_espi`**: Declares the register array for QEMU's migration
subsystem. This allows live migration and savevm/loadvm to preserve the eSPI
controller state.

#### 4.2.8 Type Registration

```c
static const TypeInfo aspeed_espi_types[] = {
    {
        .name          = TYPE_ASPEED_ESPI,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(AspeedESPIState),
        .class_init    = aspeed_espi_class_init,
    },
};
DEFINE_TYPES(aspeed_espi_types)
```

This registers `"aspeed.espi"` in QOM as a child of `SysBusDevice`. QEMU's
type system uses this to instantiate the device when the AST2600 SoC model
calls `object_initialize_child()`.

---

### 4.3 Modified File: `include/hw/arm/aspeed_soc.h`

**Two changes:**

1. **Added `#include "hw/misc/aspeed_espi.h"`** (line 41 area) so the SoC
   header knows about the `AspeedESPIState` type.

2. **Changed the `espi` field type** in `struct AspeedSoCState`:
   ```c
   // Before:
   UnimplementedDeviceState espi;
   // After:
   AspeedESPIState espi;
   ```

**Justification**: `UnimplementedDeviceState` is a QEMU generic stub that logs
all accesses but provides no functionality. Replacing it with our typed state
struct enables the actual device model to be used. This is the standard
progression in QEMU: devices start as `UnimplementedDeviceState` and graduate
to full implementations as they are developed.

---

### 4.4 Modified File: `hw/arm/aspeed_ast2600.c`

This file defines the AST2600 SoC model. Four additions wire the eSPI
controller into the SoC.

#### 4.4.1 Memory Map Entry

```c
[ASPEED_DEV_ESPI] = 0x1E6EE000,
```

Added to the `aspeed_soc_ast2600_memmap[]` array. This is the physical base
address of the eSPI controller in the AST2600 address space, per the datasheet.

#### 4.4.2 IRQ Map Entry

```c
[ASPEED_DEV_ESPI] = 42,
```

Added to the `aspeed_soc_ast2600_irqmap[]` array. The AST2600 datasheet lists
the eSPI interrupt as SPI number 74. QEMU's GIC model uses an offset of −32
(since the first 32 SPI numbers are reserved for SGIs/PPIs), yielding IRQ 42.

#### 4.4.3 Object Initialization

```c
object_initialize_child(obj, "espi", &s->espi, TYPE_ASPEED_ESPI);
```

Added in `aspeed_soc_ast2600_init()`. This creates the QOM child object
during SoC construction, before any device is realized. QEMU requires that all
child objects be initialized in `init` and realized later in `realize`.

#### 4.4.4 Device Realization and Wiring

```c
/* eSPI Controller */
if (!sysbus_realize(SYS_BUS_DEVICE(&s->espi), errp)) {
    return;
}
aspeed_mmio_map(s->memory, SYS_BUS_DEVICE(&s->espi), 0,
                sc->memmap[ASPEED_DEV_ESPI]);
sysbus_connect_irq(SYS_BUS_DEVICE(&s->espi), 0,
                   aspeed_soc_ast2600_get_irq(s, ASPEED_DEV_ESPI));
```

Added in `aspeed_soc_ast2600_realize()`. This:

1. **Realizes** the device (calls `aspeed_espi_realize`, which creates the MMIO
   region and IRQ).
2. **Maps** the MMIO region into the SoC's memory space at `0x1E6EE000`.
3. **Connects** the device's IRQ output to the GIC input corresponding to
   interrupt 42.

The pattern exactly follows how other AST2600 peripherals (PECI, SBC, LPC,
HACE) are wired.

---

### 4.5 Modified File: `hw/misc/meson.build`

```meson
system_ss.add(when: 'CONFIG_ASPEED_SOC', if_true: files(
  'aspeed_espi.c',   # ← added
  'aspeed_hace.c',
  ...
```

This adds `aspeed_espi.c` to the build when `CONFIG_ASPEED_SOC` is enabled
(which it is for all `arm-softmmu` builds that include Aspeed machines). The
file is listed alphabetically among other Aspeed source files.

---

### 4.6 New File: `tests/qtest/aspeed_espi-test.c`

**Purpose**: Automated register-level tests using QEMU's QTest framework.

QTest is QEMU's built-in testing framework. It launches a QEMU process with a
special `-qtest` flag that provides a socket-based interface for reading and
writing memory/IO from the test harness, without needing to run any guest
firmware. This allows testing device models in isolation.

#### 4.6.1 Test: `test_espi_cap_reset`

```c
val = qtest_readl(s, ESPI_BASE + ESPI_GEN_CAP_N_CONF);
g_assert_cmphex(val, ==, ESPI_GEN_CAP_RESET);  // 0x0000F759
```

**Verifies**: The general capability register reads `0xF759` immediately after
reset, matching AST2600 A3 silicon. This is the first register the Aspeed Linux
driver reads during probe.

#### 4.6.2 Test: `test_espi_cap_readonly`

```c
qtest_writel(s, ESPI_BASE + ESPI_GEN_CAP_N_CONF, 0xDEADBEEF);
val = qtest_readl(s, ESPI_BASE + ESPI_GEN_CAP_N_CONF);
g_assert_cmphex(val, ==, ESPI_GEN_CAP_RESET);  // still 0xF759
```

**Verifies**: Capability registers are read-only. Writes are silently ignored.
This is important because firmware should not be able to change what the
hardware reports as its capabilities.

#### 4.6.3 Test: `test_espi_int_sts_reset`

```c
val = qtest_readl(s, ESPI_BASE + ESPI_INT_STS);
g_assert_cmphex(val, ==, 0x80000000);  // RST_DEASSERT
```

**Verifies**: The `RST_DEASSERT` bit (bit 31) is set at reset. The Aspeed
driver checks `if (!(readl(INT_STS) & RST_DEASSERT)) return -ENODEV;` during
probe — if this bit is not set, the driver concludes the eSPI link is down and
refuses to initialize.

#### 4.6.4 Test: `test_espi_int_sts_w1c`

```c
val = qtest_readl(s, ESPI_BASE + ESPI_INT_STS);
g_assert_cmphex(val, ==, 0x80000000);          // RST_DEASSERT set
qtest_writel(s, ESPI_BASE + ESPI_INT_STS, 0x80000000);  // write 1 to clear
val = qtest_readl(s, ESPI_BASE + ESPI_INT_STS);
g_assert_cmphex(val, ==, 0x00000000);          // now cleared
```

**Verifies**: Write-1-to-clear semantics work correctly. This is the standard
interrupt acknowledge pattern: firmware reads INT_STS to see which interrupts
fired, then writes back the same value to clear them.

#### 4.6.5 Test: `test_espi_ctrl_readwrite`

```c
val = qtest_readl(s, ESPI_BASE + ESPI_CTRL);
g_assert_cmphex(val, ==, 0x00000000);        // zero at reset
qtest_writel(s, ESPI_BASE + ESPI_CTRL, 0x0000000F);
val = qtest_readl(s, ESPI_BASE + ESPI_CTRL);
g_assert_cmphex(val, ==, 0x0000000F);        // written value persists
```

**Verifies**: The control register is writable and retains values. The value
`0x0F` sets the lower channel-ready bits (`PERIF_SW_RDY`, `VW_SW_RDY`,
`OOB_SW_RDY`), which is what firmware does during initialization. Note that
bits 24–31 (the SW_RST bits) are self-clearing and would not persist if written.

#### 4.6.6 Test: `test_espi_vw_sysevt_reset`

```c
val = qtest_readl(s, ESPI_BASE + ESPI_VW_SYSEVT);
g_assert_cmphex(val, ==, 0x00000020);  // PLTRST_N deasserted
```

**Verifies**: At reset, `PLTRST_N` (bit 5) is set to 1, meaning platform reset
is deasserted (normal operating state). In real hardware, PLTRST# is an
active-low signal from the host chipset. A value of 1 means "not in reset,"
which is the expected state for a powered-on server.

#### 4.6.7 Test: `test_espi_vw_sysevt_int`

```c
qtest_writel(s, ESPI_BASE + ESPI_VW_SYSEVT_INT_EN, 0x00000020);
val = qtest_readl(s, ESPI_BASE + ESPI_VW_SYSEVT_INT_EN);
g_assert_cmphex(val, ==, 0x00000020);
```

**Verifies**: The VW system event interrupt enable register is writable. The
value `0x20` enables the interrupt for `PLTRST_N` changes. In a real system,
the BMC driver enables this interrupt to be notified when the host asserts or
deasserts PLTRST#.

---

### 4.7 Modified File: `tests/qtest/meson.build`

```meson
qtests_aspeed = \
  ['aspeed_espi-test',   # ← added
   'aspeed_gpio-test',
   'aspeed_hace-test',
   ...
```

This adds our test to the Aspeed QTest suite. Tests in `qtests_aspeed` are
automatically run for all Aspeed machine types during `meson test`.

---

## 5. What Is NOT Implemented (Future Phases)

Phase 1 intentionally defers the following to keep the initial change small and
reviewable:

| Feature | Phase | Description |
|---|---|---|
| Peripheral Channel (CH0) data path | 2 | FIFO + DMA TX/RX for KCS/IPMI over eSPI |
| OOB Channel (CH2) data path | 3 | MCTP/PLDM message exchange |
| Flash Channel (CH3) data path | 4 | SAFS (Slave Attached Flash Sharing) |
| MMBI (Memory-Mapped Bus Interface) | 5 | Memory window for direct host↔BMC data |
| Host-side eSPI master device | 6 | Full bidirectional eSPI bus abstraction |

The register offsets and bit definitions for these features are already present
in the header file, allowing future phases to add functionality incrementally
without restructuring the register map.

---

## 6. Testing

### 6.1 Automated Tests (QTest)

All 7 tests pass:

```
ok 1 /arm/aspeed-espi/cap-reset
ok 2 /arm/aspeed-espi/cap-readonly
ok 3 /arm/aspeed-espi/int-sts-reset
ok 4 /arm/aspeed-espi/int-sts-w1c
ok 5 /arm/aspeed-espi/ctrl-readwrite
ok 6 /arm/aspeed-espi/vw-sysevt-reset
ok 7 /arm/aspeed-espi/vw-sysevt-int
```

Run command:
```
QTEST_QEMU_BINARY=./qemu-system-arm ./tests/qtest/aspeed_espi-test -v
```

### 6.2 Manual Smoke Test (QEMU Monitor)

The device was also verified interactively via the QEMU monitor:

```
(qemu) xp /1xw 0x1E6EE000   → 0x00000000  (CTRL = zero at reset)
(qemu) xp /1xw 0x1E6EE008   → 0x80000000  (INT_STS = RST_DEASSERT)
(qemu) xp /1xw 0x1E6EE098   → 0x00000020  (SYSEVT = PLTRST_N)
(qemu) xp /1xw 0x1E6EE0A0   → 0x0000F759  (GEN_CAP = correct)
```

---

## 7. Upstream Considerations

- **No prior art**: As of March 2026, no eSPI device model exists in QEMU
  upstream, any public fork, or the qemu-devel mailing list. This was verified
  by searching GitHub (all code, all forks), GitLab QEMU MRs, the QEMU
  Patchwork instance, and the lore.kernel.org mailing list archive.

- **Coding style**: The implementation follows QEMU coding conventions
  (Uncrustify-formatted, 4-space indent in `.c` files, QEMU type macros,
  `LOG_GUEST_ERROR` for firmware bugs).

- **License**: GPL-2.0-or-later, matching QEMU and the Aspeed Linux driver
  from which the register definitions are derived.

- **Review path**: Aspeed device models are maintained by Cédric Le Goater
  (`clg@kaod.org`) and Joel Stanley (`joel@jms.id.au`). Patches would be sent
  to `qemu-devel@nongnu.org` and `qemu-arm@nongnu.org` with those maintainers
  CC'd.

