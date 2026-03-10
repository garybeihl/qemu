/*
 * ASPEED AST2600 eSPI Controller
 *
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Phase 1 implementation: Register skeleton + Virtual Wire channel (CH1).
 *
 * Implements the AST2600 eSPI controller at register level, sufficient for
 * the OpenBMC aspeed-espi kernel driver to probe and initialize. Supports:
 *   - Full register map read/write with correct reset values
 *   - Interrupt status (write-1-to-clear) and interrupt enable
 *   - Virtual Wire system events (SYSEVT/SYSEVT1) and GPIO
 *   - Channel capability and configuration reporting
 *
 * Reference:
 *   - Intel eSPI Base Specification Rev 1.0
 *   - Aspeed Linux driver: drivers/soc/aspeed/ast2600-espi.{c,h}
 *   - Aspeed Linux driver: drivers/soc/aspeed/aspeed-espi-comm.h
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/bitops.h"
#include "hw/core/irq.h"
#include "hw/misc/aspeed_espi.h"
#include "migration/vmstate.h"
#include "trace.h"

/* Bits in ESPI_VW_SYSEVT that are read-only from the BMC side (host-driven) */
#define SYSEVT_HOST_DRIVEN_MASK ( \
    ESPI_VW_SYSEVT_HOST_RST_WARN | \
    ESPI_VW_SYSEVT_OOB_RST_WARN  | \
    ESPI_VW_SYSEVT_PLTRST_N      | \
    ESPI_VW_SYSEVT_SUSPEND        | \
    ESPI_VW_SYSEVT_S5_SLEEP       | \
    ESPI_VW_SYSEVT_S4_SLEEP       | \
    ESPI_VW_SYSEVT_S3_SLEEP)

/* Bits in ESPI_VW_SYSEVT that are writable by BMC firmware (slave-driven) */
#define SYSEVT_SLAVE_DRIVEN_MASK ( \
    ESPI_VW_SYSEVT_HOST_RST_ACK   | \
    ESPI_VW_SYSEVT_RST_CPU_INIT   | \
    ESPI_VW_SYSEVT_SLV_BOOT_STS   | \
    ESPI_VW_SYSEVT_NON_FATAL_ERR  | \
    ESPI_VW_SYSEVT_FATAL_ERR      | \
    ESPI_VW_SYSEVT_SLV_BOOT_DONE  | \
    ESPI_VW_SYSEVT_OOB_RST_ACK    | \
    ESPI_VW_SYSEVT_NMI_OUT        | \
    ESPI_VW_SYSEVT_SMI_OUT)

static void aspeed_espi_update_irq(AspeedESPIState *s)
{
    uint32_t active = s->regs[R_ESPI_INT_STS] & s->regs[R_ESPI_INT_EN];

    if (active) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

/*
 * Check if a VW system event change should trigger an interrupt.
 * Called when host-driven SYSEVT bits are injected (e.g., via QTest).
 */
static void aspeed_espi_vw_notify_sysevt(AspeedESPIState *s,
                                          uint32_t old_val,
                                          uint32_t new_val)
{
    uint32_t changed = old_val ^ new_val;
    uint32_t enabled = s->regs[R_ESPI_VW_SYSEVT_INT_EN];

    if (changed & enabled) {
        s->regs[R_ESPI_VW_SYSEVT_INT_STS] |= (changed & enabled);
        s->regs[R_ESPI_INT_STS] |= ESPI_INT_VW_SYSEVT;
        aspeed_espi_update_irq(s);
    }
}

static uint64_t aspeed_espi_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedESPIState *s = ASPEED_ESPI(opaque);
    uint32_t reg = offset >> 2;

    if (reg >= ASPEED_ESPI_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }

    return s->regs[reg];
}

static void aspeed_espi_write(void *opaque, hwaddr offset, uint64_t data,
                               unsigned size)
{
    AspeedESPIState *s = ASPEED_ESPI(opaque);
    uint32_t reg = offset >> 2;
    uint32_t old_val;

    if (reg >= ASPEED_ESPI_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }

    switch (reg) {
    case R_ESPI_CTRL:
        /*
         * SW reset bits are self-clearing. Store the rest
         * (ready bits, DMA enables, SAFS mode, etc).
         */
        s->regs[R_ESPI_CTRL] = data & ~(
            ESPI_CTRL_FLASH_TX_SW_RST |
            ESPI_CTRL_FLASH_RX_SW_RST |
            ESPI_CTRL_OOB_TX_SW_RST   |
            ESPI_CTRL_OOB_RX_SW_RST   |
            ESPI_CTRL_PERIF_NP_TX_SW_RST |
            ESPI_CTRL_PERIF_NP_RX_SW_RST |
            ESPI_CTRL_PERIF_PC_TX_SW_RST |
            ESPI_CTRL_PERIF_PC_RX_SW_RST);
        break;

    case R_ESPI_INT_STS:
        /* Write-1-to-clear */
        s->regs[R_ESPI_INT_STS] &= ~(uint32_t)data;
        aspeed_espi_update_irq(s);
        break;

    case R_ESPI_INT_EN:
        s->regs[R_ESPI_INT_EN] = (uint32_t)data;
        aspeed_espi_update_irq(s);
        break;

    case R_ESPI_INT_EN_CLR:
        /* Writing bits here clears corresponding bits in INT_EN */
        s->regs[R_ESPI_INT_EN] &= ~(uint32_t)data;
        aspeed_espi_update_irq(s);
        break;

    case R_ESPI_VW_SYSEVT_INT_EN:
        old_val = s->regs[R_ESPI_VW_SYSEVT_INT_EN];
        s->regs[R_ESPI_VW_SYSEVT_INT_EN] = (uint32_t)data;
        /* Re-evaluate: newly enabled bits may match current SYSEVT state */
        aspeed_espi_vw_notify_sysevt(s, 0, s->regs[R_ESPI_VW_SYSEVT]);
        break;

    case R_ESPI_VW_SYSEVT:
        /*
         * BMC firmware can write slave-driven bits.
         * Host-driven bits are read-only from BMC perspective
         * (set via QMP/QTest in this model).
         */
        old_val = s->regs[R_ESPI_VW_SYSEVT];
        s->regs[R_ESPI_VW_SYSEVT] =
            (data & SYSEVT_SLAVE_DRIVEN_MASK) |
            (old_val & SYSEVT_HOST_DRIVEN_MASK);
        break;

    case R_ESPI_VW_SYSEVT1:
        old_val = s->regs[R_ESPI_VW_SYSEVT1];
        /* SUSPEND_ACK is slave-driven, SUSPEND_WARN is host-driven */
        s->regs[R_ESPI_VW_SYSEVT1] =
            (data & ESPI_VW_SYSEVT1_SUSPEND_ACK) |
            (old_val & ESPI_VW_SYSEVT1_SUSPEND_WARN);
        break;

    case R_ESPI_VW_GPIO_VAL:
        old_val = s->regs[R_ESPI_VW_GPIO_VAL];
        /*
         * GPIO VW value. Direction bits in VW_GPIO_DIR determine
         * which bits are output (writable by BMC). For now, allow
         * all writes; firmware sets direction first.
         */
        s->regs[R_ESPI_VW_GPIO_VAL] = (uint32_t)data;
        if (old_val != s->regs[R_ESPI_VW_GPIO_VAL]) {
            s->regs[R_ESPI_INT_STS] |= ESPI_INT_VW_GPIO;
            aspeed_espi_update_irq(s);
        }
        break;

    case R_ESPI_VW_SYSEVT_INT_STS:
        /* Write-1-to-clear */
        s->regs[R_ESPI_VW_SYSEVT_INT_STS] &= ~(uint32_t)data;
        /* If all SYSEVT sub-interrupts cleared, clear the top-level bit */
        if (!s->regs[R_ESPI_VW_SYSEVT_INT_STS]) {
            s->regs[R_ESPI_INT_STS] &= ~ESPI_INT_VW_SYSEVT;
        }
        aspeed_espi_update_irq(s);
        break;

    case R_ESPI_VW_SYSEVT1_INT_STS:
        /* Write-1-to-clear */
        s->regs[R_ESPI_VW_SYSEVT1_INT_STS] &= ~(uint32_t)data;
        if (!s->regs[R_ESPI_VW_SYSEVT1_INT_STS]) {
            s->regs[R_ESPI_INT_STS] &= ~ESPI_INT_VW_SYSEVT1;
        }
        aspeed_espi_update_irq(s);
        break;

    case R_ESPI_GEN_CAP_N_CONF:
    case R_ESPI_CH0_CAP_N_CONF:
    case R_ESPI_CH1_CAP_N_CONF:
    case R_ESPI_CH2_CAP_N_CONF:
    case R_ESPI_CH3_CAP_N_CONF:
    case R_ESPI_CH3_CAP_N_CONF2:
        /* Capabilities are read-only from the BMC side */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Write to read-only capability register 0x%"
                      HWADDR_PRIx "\n", __func__, offset);
        break;

    case R_ESPI_STS:
        /* Status register is read-only */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Write to read-only status register\n", __func__);
        break;

    default:
        s->regs[reg] = (uint32_t)data;
        break;
    }
}

static const MemoryRegionOps aspeed_espi_ops = {
    .read  = aspeed_espi_read,
    .write = aspeed_espi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void aspeed_espi_realize(DeviceState *dev, Error **errp)
{
    AspeedESPIState *s = ASPEED_ESPI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &aspeed_espi_ops, s,
                          TYPE_ASPEED_ESPI, 0x1000);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

static void aspeed_espi_reset(DeviceState *dev)
{
    AspeedESPIState *s = ASPEED_ESPI(dev);

    memset(s->regs, 0, sizeof(s->regs));

    /*
     * Set reset values for capability registers.
     * These tell firmware what the HW supports.
     */
    s->regs[R_ESPI_GEN_CAP_N_CONF]  = ESPI_GEN_CAP_N_CONF_RESET;
    s->regs[R_ESPI_CH0_CAP_N_CONF]  = ESPI_CH0_CAP_N_CONF_RESET;
    s->regs[R_ESPI_CH1_CAP_N_CONF]  = ESPI_CH1_CAP_N_CONF_RESET;
    s->regs[R_ESPI_CH2_CAP_N_CONF]  = ESPI_CH2_CAP_N_CONF_RESET;
    s->regs[R_ESPI_CH3_CAP_N_CONF]  = ESPI_CH3_CAP_N_CONF_RESET;
    s->regs[R_ESPI_CH3_CAP_N_CONF2] = ESPI_CH3_CAP_N_CONF2_RESET;

    /*
     * Set PLTRST# deasserted and no sleep states active by default.
     * This simulates a powered-on host that has completed platform reset.
     */
    s->regs[R_ESPI_VW_SYSEVT] = ESPI_VW_SYSEVT_PLTRST_N;

    /*
     * Assert RST_DEASSERT in interrupt status to indicate the eSPI
     * link is up. The driver checks this during probe.
     */
    s->regs[R_ESPI_INT_STS] = ESPI_INT_RST_DEASSERT;
}

static const VMStateDescription vmstate_aspeed_espi = {
    .name = TYPE_ASPEED_ESPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedESPIState, ASPEED_ESPI_NR_REGS),
        VMSTATE_END_OF_LIST(),
    },
};

static void aspeed_espi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_espi_realize;
    device_class_set_legacy_reset(dc, aspeed_espi_reset);
    dc->vmsd    = &vmstate_aspeed_espi;
    dc->desc    = "Aspeed AST2600 eSPI Controller";
}

static const TypeInfo aspeed_espi_types[] = {
    {
        .name          = TYPE_ASPEED_ESPI,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(AspeedESPIState),
        .class_init    = aspeed_espi_class_init,
        .abstract      = false,
    },
};

DEFINE_TYPES(aspeed_espi_types)
