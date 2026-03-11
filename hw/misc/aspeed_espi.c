/*
 * ASPEED AST2600 eSPI Controller
 *
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Phases 1-4 implementation:
 *   Phase 1: Register skeleton + Virtual Wire channel (CH1)
 *   Phase 2: Peripheral channel (CH0) FIFO and DMA data paths
 *   Phase 3: OOB channel (CH2) FIFO and DMA data paths
 *   Phase 4: Flash channel (CH3) FIFO and DMA data paths
 *
 * Implements the AST2600 eSPI controller at register level, sufficient for
 * the OpenBMC aspeed-espi kernel driver to probe and initialize. Supports:
 *   - Full register map read/write with correct reset values
 *   - Interrupt status (write-1-to-clear) and interrupt enable
 *   - Virtual Wire system events (SYSEVT/SYSEVT1) and GPIO
 *   - Channel capability and configuration reporting
 *   - Peripheral channel (CH0) FIFO-based TX/RX for posted completions
 *   - Peripheral channel non-posted TX path
 *   - OOB channel (CH2) FIFO-based TX/RX for out-of-band messages
 *   - Flash channel (CH3) FIFO-based TX/RX for flash access
 *   - DMA transfers between device and guest DRAM
 *   - Software reset for peripheral, OOB, and Flash channel FIFOs
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
#include "hw/core/qdev-properties.h"
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

/* ---- Peripheral channel (CH0) helpers ---- */

/*
 * Build a CTRL register value from cycle type, tag, and length.
 */
static inline uint32_t espi_perif_ctrl_pack(uint8_t cyc, uint8_t tag,
                                             uint32_t len)
{
    return ((len & 0xFFF) << ESPI_PERIF_CTRL_LEN_SHIFT) |
           ((tag & 0xF) << ESPI_PERIF_CTRL_TAG_SHIFT) |
           (cyc & 0xFF);
}

/*
 * Reset a peripheral channel FIFO (called on SW reset bits).
 */
static void aspeed_espi_perif_pc_rx_reset(AspeedESPIState *s)
{
    memset(s->pc_rx_buf, 0, sizeof(s->pc_rx_buf));
    s->pc_rx_len = 0;
    s->pc_rx_pos = 0;
    s->regs[R_ESPI_PERIF_PC_RX_CTRL] = 0;
}

static void aspeed_espi_perif_pc_tx_reset(AspeedESPIState *s)
{
    memset(s->pc_tx_buf, 0, sizeof(s->pc_tx_buf));
    s->pc_tx_len = 0;
    s->regs[R_ESPI_PERIF_PC_TX_CTRL] = 0;
}

static void aspeed_espi_perif_np_tx_reset(AspeedESPIState *s)
{
    memset(s->np_tx_buf, 0, sizeof(s->np_tx_buf));
    s->np_tx_len = 0;
    s->regs[R_ESPI_PERIF_NP_TX_CTRL] = 0;
}

/* ---- OOB channel (CH2) helpers ---- */

static void aspeed_espi_oob_rx_reset(AspeedESPIState *s)
{
    memset(s->oob_rx_buf, 0, sizeof(s->oob_rx_buf));
    s->oob_rx_len = 0;
    s->oob_rx_pos = 0;
    s->regs[R_ESPI_OOB_RX_CTRL] = 0;
}

static void aspeed_espi_oob_tx_reset(AspeedESPIState *s)
{
    memset(s->oob_tx_buf, 0, sizeof(s->oob_tx_buf));
    s->oob_tx_len = 0;
    s->regs[R_ESPI_OOB_TX_CTRL] = 0;
}

/* ---- Flash channel (CH3) helpers ---- */

static void aspeed_espi_flash_rx_reset(AspeedESPIState *s)
{
    memset(s->flash_rx_buf, 0, sizeof(s->flash_rx_buf));
    s->flash_rx_len = 0;
    s->flash_rx_pos = 0;
    s->regs[R_ESPI_FLASH_RX_CTRL] = 0;
}

static void aspeed_espi_flash_tx_reset(AspeedESPIState *s)
{
    memset(s->flash_tx_buf, 0, sizeof(s->flash_tx_buf));
    s->flash_tx_len = 0;
    s->regs[R_ESPI_FLASH_TX_CTRL] = 0;
}


/*
 * Complete a PC TX operation: transfer data via DMA or FIFO,
 * clear TRIG_PEND, and raise TX completion interrupt.
 */
static void aspeed_espi_perif_pc_tx_complete(AspeedESPIState *s)
{
    if (s->regs[R_ESPI_CTRL] & ESPI_CTRL_PERIF_PC_TX_DMA_EN) {
        /*
         * DMA mode: data was written to guest DRAM by firmware.
         * In a full two-sided model the master would read it;
         * for now we just acknowledge the transfer.
         */
    } else {
        /*
         * FIFO mode: data is already in pc_tx_buf from DATA writes.
         * Nothing to transfer in a single-sided model.
         */
    }

    /* Clear TRIG_PEND to indicate TX is complete */
    s->regs[R_ESPI_PERIF_PC_TX_CTRL] &= ~ESPI_PERIF_PC_TX_CTRL_TRIG_PEND;

    /* Reset TX FIFO position for next packet */
    s->pc_tx_len = 0;

    /* Raise TX completion interrupt */
    s->regs[R_ESPI_INT_STS] |= ESPI_INT_PERIF_PC_TX_CMPLT;
    aspeed_espi_update_irq(s);
}

/*
 * Complete an NP TX operation: same logic as PC TX.
 */
static void aspeed_espi_perif_np_tx_complete(AspeedESPIState *s)
{
    if (s->regs[R_ESPI_CTRL] & ESPI_CTRL_PERIF_NP_TX_DMA_EN) {
        /* DMA mode: acknowledge transfer */
    } else {
        /* FIFO mode: data already in np_tx_buf */
    }

    s->regs[R_ESPI_PERIF_NP_TX_CTRL] &= ~ESPI_PERIF_NP_TX_CTRL_TRIG_PEND;
    s->np_tx_len = 0;

    s->regs[R_ESPI_INT_STS] |= ESPI_INT_PERIF_NP_TX_CMPLT;
    aspeed_espi_update_irq(s);
}

/*
 * Complete an OOB TX operation: transfer data via DMA or FIFO,
 * clear TRIG_PEND, and raise TX completion interrupt.
 */
static void aspeed_espi_oob_tx_complete(AspeedESPIState *s)
{
    if (s->regs[R_ESPI_CTRL] & ESPI_CTRL_OOB_TX_DMA_EN) {
        /* DMA mode: data was written to guest DRAM by firmware */
    } else {
        /* FIFO mode: data is in oob_tx_buf from DATA writes */
    }

    s->regs[R_ESPI_OOB_TX_CTRL] &= ~ESPI_OOB_TX_CTRL_TRIG_PEND;
    s->oob_tx_len = 0;

    s->regs[R_ESPI_INT_STS] |= ESPI_INT_OOB_TX_CMPLT;
    aspeed_espi_update_irq(s);
}

/*
 * Complete a Flash TX operation: transfer data via DMA or FIFO,
 * clear TRIG_PEND, and raise TX completion interrupt.
 */
static void aspeed_espi_flash_tx_complete(AspeedESPIState *s)
{
    if (s->regs[R_ESPI_CTRL] & ESPI_CTRL_FLASH_TX_DMA_EN) {
        /* DMA mode: data was written to guest DRAM by firmware */
    } else {
        /* FIFO mode: data is in flash_tx_buf from DATA writes */
    }

    s->regs[R_ESPI_FLASH_TX_CTRL] &= ~ESPI_FLASH_TX_CTRL_TRIG_PEND;
    s->flash_tx_len = 0;

    s->regs[R_ESPI_INT_STS] |= ESPI_INT_FLASH_TX_CMPLT;
    aspeed_espi_update_irq(s);
}


static uint64_t aspeed_espi_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedESPIState *s = ASPEED_ESPI(opaque);
    uint32_t reg = offset >> 2;
    uint8_t byte_val;

    if (reg >= ASPEED_ESPI_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }

    switch (reg) {
    case R_ESPI_PERIF_PC_RX_DATA:
        /*
         * FIFO mode: return next byte from RX buffer.
         * DMA mode: data is in guest DRAM, register reads return 0.
         */
        if (s->regs[R_ESPI_CTRL] & ESPI_CTRL_PERIF_PC_RX_DMA_EN) {
            return 0;
        }
        if (s->pc_rx_pos < s->pc_rx_len) {
            byte_val = s->pc_rx_buf[s->pc_rx_pos++];
            return byte_val;
        }
        return 0;

    case R_ESPI_OOB_RX_DATA:
        /* OOB RX: same FIFO/DMA pattern as peripheral channel */
        if (s->regs[R_ESPI_CTRL] & ESPI_CTRL_OOB_RX_DMA_EN) {
            return 0;
        }
        if (s->oob_rx_pos < s->oob_rx_len) {
            byte_val = s->oob_rx_buf[s->oob_rx_pos++];
            return byte_val;
        }
        return 0;

    case R_ESPI_FLASH_RX_DATA:
        /* Flash RX: same FIFO/DMA pattern */
        if (s->regs[R_ESPI_CTRL] & ESPI_CTRL_FLASH_RX_DMA_EN) {
            return 0;
        }
        if (s->flash_rx_pos < s->flash_rx_len) {
            byte_val = s->flash_rx_buf[s->flash_rx_pos++];
            return byte_val;
        }
        return 0;

    default:
        return s->regs[reg];
    }
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
         * Execute peripheral channel FIFO resets before clearing bits.
         */
        if (data & ESPI_CTRL_PERIF_PC_RX_SW_RST) {
            aspeed_espi_perif_pc_rx_reset(s);
        }
        if (data & ESPI_CTRL_PERIF_PC_TX_SW_RST) {
            aspeed_espi_perif_pc_tx_reset(s);
        }
        if (data & ESPI_CTRL_PERIF_NP_TX_SW_RST) {
            aspeed_espi_perif_np_tx_reset(s);
        }
        if (data & ESPI_CTRL_OOB_RX_SW_RST) {
            aspeed_espi_oob_rx_reset(s);
        }
        if (data & ESPI_CTRL_OOB_TX_SW_RST) {
            aspeed_espi_oob_tx_reset(s);
        }
        if (data & ESPI_CTRL_FLASH_RX_SW_RST) {
            aspeed_espi_flash_rx_reset(s);
        }
        if (data & ESPI_CTRL_FLASH_TX_SW_RST) {
            aspeed_espi_flash_tx_reset(s);
        }
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

    case R_ESPI_PERIF_PC_RX_DMA:
    case R_ESPI_PERIF_PC_TX_DMA:
    case R_ESPI_PERIF_NP_TX_DMA:
        /* DMA address registers: store the guest physical address */
        s->regs[reg] = (uint32_t)data;
        break;

    case R_ESPI_PERIF_PC_RX_CTRL:
        /*
         * BMC writes SERV_PEND to acknowledge receipt of a packet.
         * This clears the pending flag and resets the RX FIFO position.
         */
        if (data & ESPI_PERIF_PC_RX_CTRL_SERV_PEND) {
            s->regs[R_ESPI_PERIF_PC_RX_CTRL] &=
                ~ESPI_PERIF_PC_RX_CTRL_SERV_PEND;
            s->pc_rx_pos = 0;
            s->pc_rx_len = 0;
        }
        break;

    case R_ESPI_PERIF_PC_TX_CTRL:
        /*
         * BMC writes CYC|TAG|LEN|TRIG_PEND to trigger a TX.
         * Store the control word, then if TRIG_PEND is set,
         * complete the transmission.
         */
        s->regs[R_ESPI_PERIF_PC_TX_CTRL] = (uint32_t)data;
        if (data & ESPI_PERIF_PC_TX_CTRL_TRIG_PEND) {
            aspeed_espi_perif_pc_tx_complete(s);
        }
        break;

    case R_ESPI_PERIF_NP_TX_CTRL:
        s->regs[R_ESPI_PERIF_NP_TX_CTRL] = (uint32_t)data;
        if (data & ESPI_PERIF_NP_TX_CTRL_TRIG_PEND) {
            aspeed_espi_perif_np_tx_complete(s);
        }
        break;

    case R_ESPI_PERIF_PC_RX_DATA:
        /* RX DATA is read-only from BMC side */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Write to read-only RX DATA register\n", __func__);
        break;

    case R_ESPI_PERIF_PC_TX_DATA:
        /*
         * FIFO mode: BMC pushes bytes into TX buffer.
         * DMA mode: writes are ignored (data comes from DRAM).
         */
        if (!(s->regs[R_ESPI_CTRL] & ESPI_CTRL_PERIF_PC_TX_DMA_EN)) {
            if (s->pc_tx_len < ASPEED_ESPI_PERIF_FIFO_SIZE) {
                s->pc_tx_buf[s->pc_tx_len++] = (uint8_t)data;
            }
        }
        break;

    case R_ESPI_PERIF_NP_TX_DATA:
        if (!(s->regs[R_ESPI_CTRL] & ESPI_CTRL_PERIF_NP_TX_DMA_EN)) {
            if (s->np_tx_len < ASPEED_ESPI_PERIF_FIFO_SIZE) {
                s->np_tx_buf[s->np_tx_len++] = (uint8_t)data;
            }
        }
        break;

    /* OOB channel (CH2) registers */
    case R_ESPI_OOB_RX_DMA:
    case R_ESPI_OOB_TX_DMA:
        /* DMA address registers: store the guest physical address */
        s->regs[reg] = (uint32_t)data;
        break;

    case R_ESPI_OOB_RX_CTRL:
        /*
         * BMC writes SERV_PEND to acknowledge receipt of an OOB packet.
         * This clears the pending flag and resets the RX FIFO position.
         */
        if (data & ESPI_OOB_RX_CTRL_SERV_PEND) {
            s->regs[R_ESPI_OOB_RX_CTRL] &= ~ESPI_OOB_RX_CTRL_SERV_PEND;
            s->oob_rx_pos = 0;
            s->oob_rx_len = 0;
        }
        break;

    case R_ESPI_OOB_TX_CTRL:
        /*
         * BMC writes CYC|TAG|LEN|TRIG_PEND to trigger an OOB TX.
         * Store the control word, then if TRIG_PEND is set,
         * complete the transmission.
         */
        s->regs[R_ESPI_OOB_TX_CTRL] = (uint32_t)data;
        if (data & ESPI_OOB_TX_CTRL_TRIG_PEND) {
            aspeed_espi_oob_tx_complete(s);
        }
        break;

    case R_ESPI_OOB_RX_DATA:
        /* OOB RX DATA is read-only from BMC side */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Write to read-only OOB RX DATA register\n",
                      __func__);
        break;

    case R_ESPI_OOB_TX_DATA:
        /*
         * FIFO mode: BMC pushes bytes into OOB TX buffer.
         * DMA mode: writes are ignored (data comes from DRAM).
         */
        if (!(s->regs[R_ESPI_CTRL] & ESPI_CTRL_OOB_TX_DMA_EN)) {
            if (s->oob_tx_len < ASPEED_ESPI_OOB_FIFO_SIZE) {
                s->oob_tx_buf[s->oob_tx_len++] = (uint8_t)data;
            }
        }
        break;

    /* Flash channel (CH3) registers */
    case R_ESPI_FLASH_RX_DMA:
    case R_ESPI_FLASH_TX_DMA:
        s->regs[reg] = (uint32_t)data;
        break;

    case R_ESPI_FLASH_RX_CTRL:
        if (data & ESPI_FLASH_RX_CTRL_SERV_PEND) {
            s->regs[R_ESPI_FLASH_RX_CTRL] &=
                ~ESPI_FLASH_RX_CTRL_SERV_PEND;
            s->flash_rx_pos = 0;
            s->flash_rx_len = 0;
        }
        break;

    case R_ESPI_FLASH_TX_CTRL:
        s->regs[R_ESPI_FLASH_TX_CTRL] = (uint32_t)data;
        if (data & ESPI_FLASH_TX_CTRL_TRIG_PEND) {
            aspeed_espi_flash_tx_complete(s);
        }
        break;

    case R_ESPI_FLASH_RX_DATA:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Write to read-only Flash RX DATA register\n",
                      __func__);
        break;

    case R_ESPI_FLASH_TX_DATA:
        if (!(s->regs[R_ESPI_CTRL] & ESPI_CTRL_FLASH_TX_DMA_EN)) {
            if (s->flash_tx_len < ASPEED_ESPI_FLASH_FIFO_SIZE) {
                s->flash_tx_buf[s->flash_tx_len++] = (uint8_t)data;
            }
        }
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

    /* Set up DMA address space for peripheral channel transfers */
    if (s->dram_mr) {
        address_space_init(&s->dma_as, s->dram_mr,
                           TYPE_ASPEED_ESPI ".dma");
    }
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

    /* Reset peripheral channel FIFO state */
    aspeed_espi_perif_pc_rx_reset(s);
    aspeed_espi_perif_pc_tx_reset(s);
    aspeed_espi_perif_np_tx_reset(s);

    /* Reset OOB channel FIFO state */
    aspeed_espi_oob_rx_reset(s);
    aspeed_espi_oob_tx_reset(s);

    /* Reset Flash channel FIFO state */
    aspeed_espi_flash_rx_reset(s);
    aspeed_espi_flash_tx_reset(s);
}

static const VMStateDescription vmstate_aspeed_espi = {
    .name = TYPE_ASPEED_ESPI,
    .version_id = 4,
    .minimum_version_id = 4,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedESPIState, ASPEED_ESPI_NR_REGS),
        VMSTATE_UINT8_ARRAY(pc_rx_buf, AspeedESPIState,
                            ASPEED_ESPI_PERIF_FIFO_SIZE),
        VMSTATE_UINT32(pc_rx_len, AspeedESPIState),
        VMSTATE_UINT32(pc_rx_pos, AspeedESPIState),
        VMSTATE_UINT8_ARRAY(pc_tx_buf, AspeedESPIState,
                            ASPEED_ESPI_PERIF_FIFO_SIZE),
        VMSTATE_UINT32(pc_tx_len, AspeedESPIState),
        VMSTATE_UINT8_ARRAY(np_tx_buf, AspeedESPIState,
                            ASPEED_ESPI_PERIF_FIFO_SIZE),
        VMSTATE_UINT32(np_tx_len, AspeedESPIState),
        VMSTATE_UINT8_ARRAY(oob_rx_buf, AspeedESPIState,
                            ASPEED_ESPI_OOB_FIFO_SIZE),
        VMSTATE_UINT32(oob_rx_len, AspeedESPIState),
        VMSTATE_UINT32(oob_rx_pos, AspeedESPIState),
        VMSTATE_UINT8_ARRAY(oob_tx_buf, AspeedESPIState,
                            ASPEED_ESPI_OOB_FIFO_SIZE),
        VMSTATE_UINT32(oob_tx_len, AspeedESPIState),
        VMSTATE_UINT8_ARRAY(flash_rx_buf, AspeedESPIState,
                            ASPEED_ESPI_FLASH_FIFO_SIZE),
        VMSTATE_UINT32(flash_rx_len, AspeedESPIState),
        VMSTATE_UINT32(flash_rx_pos, AspeedESPIState),
        VMSTATE_UINT8_ARRAY(flash_tx_buf, AspeedESPIState,
                            ASPEED_ESPI_FLASH_FIFO_SIZE),
        VMSTATE_UINT32(flash_tx_len, AspeedESPIState),
        VMSTATE_END_OF_LIST(),
    },
};

static const Property aspeed_espi_properties[] = {
    DEFINE_PROP_LINK("dram", AspeedESPIState, dram_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static void aspeed_espi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_espi_realize;
    device_class_set_legacy_reset(dc, aspeed_espi_reset);
    dc->vmsd    = &vmstate_aspeed_espi;
    dc->desc    = "Aspeed AST2600 eSPI Controller";
    device_class_set_props(dc, aspeed_espi_properties);
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

/*
 * Inject a posted completion RX packet into the peripheral channel.
 * This simulates a host-to-BMC transaction arriving over the eSPI bus.
 *
 * In FIFO mode, data is placed in the internal RX buffer.
 * In DMA mode, data is written directly to guest DRAM at the
 * address specified in PERIF_PC_RX_DMA.
 */
void aspeed_espi_perif_pc_rx_inject(AspeedESPIState *s, uint8_t cyc,
                                     uint8_t tag, const uint8_t *data,
                                     uint32_t len)
{
    uint32_t i;

    if (len > ASPEED_ESPI_PERIF_FIFO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Packet too large (%u > %u)\n",
                      __func__, len, ASPEED_ESPI_PERIF_FIFO_SIZE);
        len = ASPEED_ESPI_PERIF_FIFO_SIZE;
    }

    if (s->regs[R_ESPI_CTRL] & ESPI_CTRL_PERIF_PC_RX_DMA_EN) {
        /* DMA mode: write data to guest DRAM */
        if (s->dram_mr) {
            uint32_t dma_addr = s->regs[R_ESPI_PERIF_PC_RX_DMA];
            for (i = 0; i < len; i++) {
                address_space_stb(&s->dma_as,
                                  dma_addr + i,
                                  data[i],
                                  MEMTXATTRS_UNSPECIFIED,
                                  NULL);
            }
        }
    } else {
        /* FIFO mode: copy data into RX buffer */
        memcpy(s->pc_rx_buf, data, len);
        s->pc_rx_len = len;
        s->pc_rx_pos = 0;
    }

    /* Set CTRL with packet header and SERV_PEND flag */
    s->regs[R_ESPI_PERIF_PC_RX_CTRL] =
        ESPI_PERIF_PC_RX_CTRL_SERV_PEND |
        espi_perif_ctrl_pack(cyc, tag, len);

    /* Raise RX completion interrupt */
    s->regs[R_ESPI_INT_STS] |= ESPI_INT_PERIF_PC_RX_CMPLT;
    aspeed_espi_update_irq(s);
}

/*
 * Inject an OOB RX packet into the OOB channel.
 * This simulates a host-to-BMC OOB message arriving over the eSPI bus.
 *
 * In FIFO mode, data is placed in the internal OOB RX buffer.
 * In DMA mode, data is written directly to guest DRAM at the
 * address specified in OOB_RX_DMA.
 */
void aspeed_espi_oob_rx_inject(AspeedESPIState *s, uint8_t cyc,
                                uint8_t tag, const uint8_t *data,
                                uint32_t len)
{
    uint32_t i;

    if (len > ASPEED_ESPI_OOB_FIFO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: OOB packet too large (%u > %u)\n",
                      __func__, len, ASPEED_ESPI_OOB_FIFO_SIZE);
        len = ASPEED_ESPI_OOB_FIFO_SIZE;
    }

    if (s->regs[R_ESPI_CTRL] & ESPI_CTRL_OOB_RX_DMA_EN) {
        /* DMA mode: write data to guest DRAM */
        if (s->dram_mr) {
            uint32_t dma_addr = s->regs[R_ESPI_OOB_RX_DMA];
            for (i = 0; i < len; i++) {
                address_space_stb(&s->dma_as,
                                  dma_addr + i,
                                  data[i],
                                  MEMTXATTRS_UNSPECIFIED,
                                  NULL);
            }
        }
    } else {
        /* FIFO mode: copy data into OOB RX buffer */
        memcpy(s->oob_rx_buf, data, len);
        s->oob_rx_len = len;
        s->oob_rx_pos = 0;
    }

    /* Set CTRL with packet header and SERV_PEND flag */
    s->regs[R_ESPI_OOB_RX_CTRL] =
        ESPI_OOB_RX_CTRL_SERV_PEND |
        espi_perif_ctrl_pack(cyc, tag, len);

    /* Raise OOB RX completion interrupt */
    s->regs[R_ESPI_INT_STS] |= ESPI_INT_OOB_RX_CMPLT;
    aspeed_espi_update_irq(s);
}

/*
 * Inject a Flash RX packet into the Flash channel.
 * This simulates a host-to-BMC flash read response arriving over eSPI.
 *
 * In FIFO mode, data is placed in the internal Flash RX buffer.
 * In DMA mode, data is written directly to guest DRAM.
 */
void aspeed_espi_flash_rx_inject(AspeedESPIState *s, uint8_t cyc,
                                  uint8_t tag, const uint8_t *data,
                                  uint32_t len)
{
    uint32_t i;

    if (len > ASPEED_ESPI_FLASH_FIFO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Flash packet too large (%u > %u)\n",
                      __func__, len, ASPEED_ESPI_FLASH_FIFO_SIZE);
        len = ASPEED_ESPI_FLASH_FIFO_SIZE;
    }

    if (s->regs[R_ESPI_CTRL] & ESPI_CTRL_FLASH_RX_DMA_EN) {
        if (s->dram_mr) {
            uint32_t dma_addr = s->regs[R_ESPI_FLASH_RX_DMA];
            for (i = 0; i < len; i++) {
                address_space_stb(&s->dma_as,
                                  dma_addr + i,
                                  data[i],
                                  MEMTXATTRS_UNSPECIFIED,
                                  NULL);
            }
        }
    } else {
        memcpy(s->flash_rx_buf, data, len);
        s->flash_rx_len = len;
        s->flash_rx_pos = 0;
    }

    s->regs[R_ESPI_FLASH_RX_CTRL] =
        ESPI_FLASH_RX_CTRL_SERV_PEND |
        espi_perif_ctrl_pack(cyc, tag, len);

    s->regs[R_ESPI_INT_STS] |= ESPI_INT_FLASH_RX_CMPLT;
    aspeed_espi_update_irq(s);
}

DEFINE_TYPES(aspeed_espi_types)
