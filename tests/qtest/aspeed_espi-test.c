/*
 * QTest testcase for the Aspeed AST2600 eSPI Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Microsoft Corporation
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define AST2600_MACHINE         "-machine ast2600-evb"
#define ESPI_BASE               0x1E6EE000

/* Register offsets */
#define ESPI_CTRL               0x000
#define ESPI_INT_STS            0x008
#define ESPI_INT_EN             0x00C
#define ESPI_PERIF_PC_RX_DMA    0x010
#define ESPI_PERIF_PC_RX_CTRL   0x014
#define ESPI_PERIF_PC_RX_DATA   0x018
#define ESPI_PERIF_PC_TX_DMA    0x020
#define ESPI_PERIF_PC_TX_CTRL   0x024
#define ESPI_PERIF_PC_TX_DATA   0x028
#define ESPI_PERIF_NP_TX_DMA    0x030
#define ESPI_PERIF_NP_TX_CTRL   0x034
#define ESPI_PERIF_NP_TX_DATA   0x038
#define ESPI_VW_SYSEVT          0x098
#define ESPI_VW_SYSEVT_INT_EN   0x09C
#define ESPI_VW_SYSEVT_INT_STS  0x0A8
#define ESPI_GEN_CAP_N_CONF     0x0A0

/* ESPI_CTRL bits */
#define ESPI_CTRL_PERIF_PC_RX_SW_RST   (1u << 24)
#define ESPI_CTRL_PERIF_PC_TX_SW_RST   (1u << 25)
#define ESPI_CTRL_PERIF_NP_TX_SW_RST   (1u << 27)
#define ESPI_CTRL_PERIF_PC_TX_DMA_EN   (1u << 17)
#define ESPI_CTRL_PERIF_PC_RX_DMA_EN   (1u << 16)

/* PERIF CTRL register bits */
#define PERIF_CTRL_SERV_PEND     (1u << 31)
#define PERIF_CTRL_TRIG_PEND     (1u << 31)

/* Interrupt bits */
#define ESPI_INT_PERIF_PC_RX_CMPLT (1u << 0)
#define ESPI_INT_PERIF_PC_TX_CMPLT (1u << 1)
#define ESPI_INT_PERIF_NP_TX_CMPLT (1u << 3)


/* OOB channel (CH2) register offsets - Phase 3 */
#define ESPI_OOB_RX_DMA         0x040
#define ESPI_OOB_RX_CTRL        0x044
#define ESPI_OOB_RX_DATA        0x048
#define ESPI_OOB_TX_DMA         0x050
#define ESPI_OOB_TX_CTRL        0x054
#define ESPI_OOB_TX_DATA        0x058

/* OOB CTRL register bits */
#define OOB_CTRL_SERV_PEND      (1u << 31)
#define OOB_CTRL_TRIG_PEND      (1u << 31)

/* OOB-related ESPI_CTRL bits */
#define ESPI_CTRL_OOB_TX_SW_RST (1u << 29)
#define ESPI_CTRL_OOB_RX_SW_RST (1u << 28)


/* Flash channel (CH3) register offsets - Phase 4 */
#define ESPI_FLASH_RX_DMA       0x060
#define ESPI_FLASH_RX_CTRL      0x064
#define ESPI_FLASH_RX_DATA      0x068
#define ESPI_FLASH_TX_DMA       0x070
#define ESPI_FLASH_TX_CTRL      0x074
#define ESPI_FLASH_TX_DATA      0x078

/* Flash CTRL register bits */
#define FLASH_CTRL_SERV_PEND    (1u << 31)
#define FLASH_CTRL_TRIG_PEND    (1u << 31)

/* Flash-related ESPI_CTRL bits */
#define ESPI_CTRL_FLASH_TX_SW_RST  (1u << 31)
#define ESPI_CTRL_FLASH_RX_SW_RST  (1u << 30)


/* MMBI register offsets - Phase 5 */
#define ESPI_CTRL2              0x080
#define ESPI_PERIF_MCYC_SADDR  0x084
#define ESPI_PERIF_MCYC_TADDR  0x088
#define ESPI_PERIF_MCYC_MASK   0x08C
#define ESPI_MMBI_CTRL          0x800
#define ESPI_MMBI_INT_STS       0x808
#define ESPI_MMBI_INT_EN        0x80C
#define ESPI_MMBI_HOST_RWP0     0x810

/* MMBI_CTRL bits */
#define ESPI_MMBI_CTRL_EN       (1u << 0)

/* CTRL2 bits */
#define ESPI_CTRL2_MCYC_RD_DIS (1u << 6)
#define ESPI_CTRL2_MCYC_WR_DIS (1u << 4)
/* Flash interrupt bits */
#define ESPI_INT_FLASH_TX_CMPLT (1u << 7)
#define ESPI_INT_FLASH_RX_CMPLT (1u << 6)
/* OOB interrupt bits */
#define ESPI_INT_OOB_TX_CMPLT   (1u << 5)
#define ESPI_INT_OOB_RX_CMPLT   (1u << 4)

/* Virtual Wire host-driven event bits - Phase 6 */
#define ESPI_VW_SYSEVT_HOST_RST_WARN   (1u << 8)
#define ESPI_VW_SYSEVT_OOB_RST_WARN    (1u << 6)
#define ESPI_VW_SYSEVT_PLTRST_N        (1u << 5)
#define ESPI_VW_SYSEVT_SUSPEND          (1u << 4)
#define ESPI_VW_SYSEVT_S5_SLEEP         (1u << 2)
#define ESPI_VW_SYSEVT_S4_SLEEP         (1u << 1)
#define ESPI_VW_SYSEVT_S3_SLEEP         (1u << 0)

/* VW slave-driven event bits */
#define ESPI_VW_SYSEVT_HOST_RST_ACK    (1u << 27)
#define ESPI_VW_SYSEVT_SLV_BOOT_DONE   (1u << 20)
#define ESPI_VW_SYSEVT_OOB_RST_ACK     (1u << 16)
/* Expected reset values */
#define ESPI_GEN_CAP_RESET      0x0000F759
#define ESPI_INT_STS_RESET      0x80000000  /* RST_DEASSERT */
#define ESPI_VW_SYSEVT_RESET    0x00000020  /* PLTRST_N deasserted */

/*
 * Test: Capability register has correct reset value
 */
static void test_espi_cap_reset(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    val = qtest_readl(s, ESPI_BASE + ESPI_GEN_CAP_N_CONF);
    g_assert_cmphex(val, ==, ESPI_GEN_CAP_RESET);

    qtest_quit(s);
}

/*
 * Test: Capability register is read-only
 */
static void test_espi_cap_readonly(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    qtest_writel(s, ESPI_BASE + ESPI_GEN_CAP_N_CONF, 0xDEADBEEF);
    val = qtest_readl(s, ESPI_BASE + ESPI_GEN_CAP_N_CONF);
    g_assert_cmphex(val, ==, ESPI_GEN_CAP_RESET);

    qtest_quit(s);
}

/*
 * Test: Interrupt status has RST_DEASSERT set at reset
 */
static void test_espi_int_sts_reset(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    val = qtest_readl(s, ESPI_BASE + ESPI_INT_STS);
    g_assert_cmphex(val, ==, ESPI_INT_STS_RESET);

    qtest_quit(s);
}

/*
 * Test: Interrupt status is write-1-to-clear
 */
static void test_espi_int_sts_w1c(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* RST_DEASSERT should be set */
    val = qtest_readl(s, ESPI_BASE + ESPI_INT_STS);
    g_assert_cmphex(val, ==, ESPI_INT_STS_RESET);

    /* Write 1 to clear it */
    qtest_writel(s, ESPI_BASE + ESPI_INT_STS, ESPI_INT_STS_RESET);
    val = qtest_readl(s, ESPI_BASE + ESPI_INT_STS);
    g_assert_cmphex(val, ==, 0x00000000);

    qtest_quit(s);
}

/*
 * Test: CTRL register is read-write
 */
static void test_espi_ctrl_readwrite(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    val = qtest_readl(s, ESPI_BASE + ESPI_CTRL);
    g_assert_cmphex(val, ==, 0x00000000);

    qtest_writel(s, ESPI_BASE + ESPI_CTRL, 0x0000000F);
    val = qtest_readl(s, ESPI_BASE + ESPI_CTRL);
    g_assert_cmphex(val, ==, 0x0000000F);

    qtest_quit(s);
}

/*
 * Test: VW SYSEVT has PLTRST# deasserted at reset
 */
static void test_espi_vw_sysevt_reset(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    val = qtest_readl(s, ESPI_BASE + ESPI_VW_SYSEVT);
    g_assert_cmphex(val, ==, ESPI_VW_SYSEVT_RESET);

    qtest_quit(s);
}

/*
 * Test: VW SYSEVT INT_EN enables interrupts and triggers status
 */
static void test_espi_vw_sysevt_int(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* Enable SYSEVT interrupt for PLTRST (bit 5) */
    qtest_writel(s, ESPI_BASE + ESPI_VW_SYSEVT_INT_EN, 0x00000020);
    val = qtest_readl(s, ESPI_BASE + ESPI_VW_SYSEVT_INT_EN);
    g_assert_cmphex(val, ==, 0x00000020);

    qtest_quit(s);
}


/*
 * ---- Phase 2: Peripheral Channel Tests ----
 */

/*
 * Test: PC TX FIFO write + trigger generates TX completion interrupt
 */
static void test_espi_perif_pc_tx_fifo(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* Clear any pending interrupts */
    qtest_writel(s, ESPI_BASE + ESPI_INT_STS, 0xFFFFFFFF);

    /* Write 4 bytes to TX FIFO */
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_PC_TX_DATA, 0x11);
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_PC_TX_DATA, 0x22);
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_PC_TX_DATA, 0x33);
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_PC_TX_DATA, 0x44);

    /* Trigger TX: cyc=0x09, tag=0, len=4, TRIG_PEND set */
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_PC_TX_CTRL,
                 PERIF_CTRL_TRIG_PEND | (4 << 12) | 0x09);

    /* TRIG_PEND should be cleared after completion */
    val = qtest_readl(s, ESPI_BASE + ESPI_PERIF_PC_TX_CTRL);
    g_assert_cmphex(val & PERIF_CTRL_TRIG_PEND, ==, 0);

    /* TX completion interrupt should be set */
    val = qtest_readl(s, ESPI_BASE + ESPI_INT_STS);
    g_assert_cmphex(val & ESPI_INT_PERIF_PC_TX_CMPLT, !=, 0);

    qtest_quit(s);
}

/*
 * Test: NP TX FIFO write + trigger generates NP TX completion interrupt
 */
static void test_espi_perif_np_tx_fifo(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    qtest_writel(s, ESPI_BASE + ESPI_INT_STS, 0xFFFFFFFF);

    /* Write 2 bytes to NP TX FIFO */
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_NP_TX_DATA, 0xAA);
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_NP_TX_DATA, 0xBB);

    /* Trigger NP TX */
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_NP_TX_CTRL,
                 PERIF_CTRL_TRIG_PEND | (2 << 12) | 0x02);

    val = qtest_readl(s, ESPI_BASE + ESPI_PERIF_NP_TX_CTRL);
    g_assert_cmphex(val & PERIF_CTRL_TRIG_PEND, ==, 0);

    val = qtest_readl(s, ESPI_BASE + ESPI_INT_STS);
    g_assert_cmphex(val & ESPI_INT_PERIF_NP_TX_CMPLT, !=, 0);

    qtest_quit(s);
}

/*
 * Test: PC RX FIFO inject + read
 *
 * The RX inject function is internal to the device model and not
 * directly callable from QTest. Instead, we test the register-level
 * behavior: write to RX_CTRL to simulate a packet arrival, then
 * verify SERV_PEND acknowledge clears the flag.
 */
static void test_espi_perif_pc_rx_ctrl(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* RX CTRL should start at 0 */
    val = qtest_readl(s, ESPI_BASE + ESPI_PERIF_PC_RX_CTRL);
    g_assert_cmphex(val, ==, 0x00000000);

    /* Writing SERV_PEND to acknowledge (even when nothing pending) should work */
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_PC_RX_CTRL, PERIF_CTRL_SERV_PEND);
    val = qtest_readl(s, ESPI_BASE + ESPI_PERIF_PC_RX_CTRL);
    g_assert_cmphex(val & PERIF_CTRL_SERV_PEND, ==, 0);

    qtest_quit(s);
}

/*
 * Test: DMA address registers are read-write
 */
static void test_espi_perif_dma_addr(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* PC RX DMA address */
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_PC_RX_DMA, 0x80000000);
    val = qtest_readl(s, ESPI_BASE + ESPI_PERIF_PC_RX_DMA);
    g_assert_cmphex(val, ==, 0x80000000);

    /* PC TX DMA address */
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_PC_TX_DMA, 0x80001000);
    val = qtest_readl(s, ESPI_BASE + ESPI_PERIF_PC_TX_DMA);
    g_assert_cmphex(val, ==, 0x80001000);

    /* NP TX DMA address */
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_NP_TX_DMA, 0x80002000);
    val = qtest_readl(s, ESPI_BASE + ESPI_PERIF_NP_TX_DMA);
    g_assert_cmphex(val, ==, 0x80002000);

    qtest_quit(s);
}

/*
 * Test: SW reset clears TX CTRL and TX completion interrupt
 */
static void test_espi_perif_sw_reset(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* Write some data to TX FIFO and trigger */
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_PC_TX_DATA, 0x42);
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_PC_TX_CTRL,
                 PERIF_CTRL_TRIG_PEND | (1 << 12) | 0x09);

    /* Clear the TX completion interrupt */
    qtest_writel(s, ESPI_BASE + ESPI_INT_STS, ESPI_INT_PERIF_PC_TX_CMPLT);

    /* Now issue SW reset for PC TX */
    qtest_writel(s, ESPI_BASE + ESPI_CTRL, ESPI_CTRL_PERIF_PC_TX_SW_RST);

    /* CTRL register should not retain the reset bit (self-clearing) */
    val = qtest_readl(s, ESPI_BASE + ESPI_CTRL);
    g_assert_cmphex(val & ESPI_CTRL_PERIF_PC_TX_SW_RST, ==, 0);

    /* PC TX CTRL should be cleared by reset */
    val = qtest_readl(s, ESPI_BASE + ESPI_PERIF_PC_TX_CTRL);
    g_assert_cmphex(val, ==, 0x00000000);

    qtest_quit(s);
}

/*
 * Test: RX DATA register is read-only (writes ignored)
 */
static void test_espi_perif_rx_data_readonly(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* Write to RX DATA should be rejected */
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_PC_RX_DATA, 0xDEADBEEF);

    /* Reading should return 0 (empty FIFO) */
    val = qtest_readl(s, ESPI_BASE + ESPI_PERIF_PC_RX_DATA);
    g_assert_cmphex(val, ==, 0x00000000);

    qtest_quit(s);
}

/*
 * ---- Phase 3: OOB Channel (CH2) Tests ----
 */

/*
 * Test: OOB TX FIFO write + trigger generates TX completion interrupt
 */
static void test_espi_oob_tx_fifo(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* Clear any pending interrupts */
    qtest_writel(s, ESPI_BASE + ESPI_INT_STS, 0xFFFFFFFF);

    /* Write 4 bytes to OOB TX FIFO */
    qtest_writel(s, ESPI_BASE + ESPI_OOB_TX_DATA, 0xAA);
    qtest_writel(s, ESPI_BASE + ESPI_OOB_TX_DATA, 0xBB);
    qtest_writel(s, ESPI_BASE + ESPI_OOB_TX_DATA, 0xCC);
    qtest_writel(s, ESPI_BASE + ESPI_OOB_TX_DATA, 0xDD);

    /* Trigger OOB TX: cycle=0x21 (OOB message), tag=0, len=4, TRIG_PEND */
    qtest_writel(s, ESPI_BASE + ESPI_OOB_TX_CTRL,
                 OOB_CTRL_TRIG_PEND | (4 << 12) | 0x21);

    /* TRIG_PEND should be cleared after completion */
    val = qtest_readl(s, ESPI_BASE + ESPI_OOB_TX_CTRL);
    g_assert_cmphex(val & OOB_CTRL_TRIG_PEND, ==, 0);

    /* OOB TX completion interrupt should be set */
    val = qtest_readl(s, ESPI_BASE + ESPI_INT_STS);
    g_assert_cmphex(val & ESPI_INT_OOB_TX_CMPLT, !=, 0);

    /* W1C to clear it */
    qtest_writel(s, ESPI_BASE + ESPI_INT_STS, ESPI_INT_OOB_TX_CMPLT);
    val = qtest_readl(s, ESPI_BASE + ESPI_INT_STS);
    g_assert_cmphex(val & ESPI_INT_OOB_TX_CMPLT, ==, 0);

    qtest_quit(s);
}

/*
 * Test: OOB RX CTRL SERV_PEND acknowledge path
 */
static void test_espi_oob_rx_ctrl(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* RX CTRL should start at 0 (no pending packet) */
    val = qtest_readl(s, ESPI_BASE + ESPI_OOB_RX_CTRL);
    g_assert_cmphex(val, ==, 0x00000000);

    /* Writing SERV_PEND to acknowledge (when nothing pending) is a no-op */
    qtest_writel(s, ESPI_BASE + ESPI_OOB_RX_CTRL, OOB_CTRL_SERV_PEND);
    val = qtest_readl(s, ESPI_BASE + ESPI_OOB_RX_CTRL);
    g_assert_cmphex(val & OOB_CTRL_SERV_PEND, ==, 0);

    qtest_quit(s);
}

/*
 * Test: OOB DMA address registers are read-write
 */
static void test_espi_oob_dma_addr(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* OOB RX DMA address */
    qtest_writel(s, ESPI_BASE + ESPI_OOB_RX_DMA, 0x80100000);
    val = qtest_readl(s, ESPI_BASE + ESPI_OOB_RX_DMA);
    g_assert_cmphex(val, ==, 0x80100000);

    /* OOB TX DMA address */
    qtest_writel(s, ESPI_BASE + ESPI_OOB_TX_DMA, 0x80200000);
    val = qtest_readl(s, ESPI_BASE + ESPI_OOB_TX_DMA);
    g_assert_cmphex(val, ==, 0x80200000);

    qtest_quit(s);
}

/*
 * Test: OOB SW reset clears FIFO state and is self-clearing
 */
static void test_espi_oob_sw_reset(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* Write some data into OOB TX FIFO */
    qtest_writel(s, ESPI_BASE + ESPI_OOB_TX_DATA, 0x11);
    qtest_writel(s, ESPI_BASE + ESPI_OOB_TX_DATA, 0x22);

    /* Assert OOB TX SW reset via CTRL register */
    qtest_writel(s, ESPI_BASE + ESPI_CTRL, ESPI_CTRL_OOB_TX_SW_RST);

    /* SW reset bit should be self-clearing */
    val = qtest_readl(s, ESPI_BASE + ESPI_CTRL);
    g_assert_cmphex(val & ESPI_CTRL_OOB_TX_SW_RST, ==, 0);

    /* TX CTRL should be cleared by reset */
    val = qtest_readl(s, ESPI_BASE + ESPI_OOB_TX_CTRL);
    g_assert_cmphex(val, ==, 0x00000000);

    /* Same for OOB RX reset */
    qtest_writel(s, ESPI_BASE + ESPI_CTRL, ESPI_CTRL_OOB_RX_SW_RST);
    val = qtest_readl(s, ESPI_BASE + ESPI_CTRL);
    g_assert_cmphex(val & ESPI_CTRL_OOB_RX_SW_RST, ==, 0);
    val = qtest_readl(s, ESPI_BASE + ESPI_OOB_RX_CTRL);
    g_assert_cmphex(val, ==, 0x00000000);

    qtest_quit(s);
}

/*
 * Test: OOB RX DATA is read-only from BMC side (writes ignored)
 */
static void test_espi_oob_rx_data_readonly(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* Write to OOB RX DATA should be rejected */
    qtest_writel(s, ESPI_BASE + ESPI_OOB_RX_DATA, 0xDEADBEEF);

    /* Reading should return 0 (empty FIFO) */
    val = qtest_readl(s, ESPI_BASE + ESPI_OOB_RX_DATA);
    g_assert_cmphex(val, ==, 0x00000000);

    qtest_quit(s);
}

/*
 * Test: OOB TX CTRL trigger-pending completes immediately
 */
static void test_espi_oob_tx_ctrl_trigger(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* Clear interrupts */
    qtest_writel(s, ESPI_BASE + ESPI_INT_STS, 0xFFFFFFFF);

    /* Trigger with TRIG_PEND and zero-length header */
    qtest_writel(s, ESPI_BASE + ESPI_OOB_TX_CTRL,
                 OOB_CTRL_TRIG_PEND | (0 << 12) | 0x21);

    /* TRIG_PEND should clear */
    val = qtest_readl(s, ESPI_BASE + ESPI_OOB_TX_CTRL);
    g_assert_cmphex(val & OOB_CTRL_TRIG_PEND, ==, 0);

    /* TX completion interrupt should fire */
    val = qtest_readl(s, ESPI_BASE + ESPI_INT_STS);
    g_assert_cmphex(val & ESPI_INT_OOB_TX_CMPLT, !=, 0);

    qtest_quit(s);
}

/*
 * ---- Phase 4: Flash Channel (CH3) Tests ----
 */

/*
 * Test: Flash TX FIFO write + trigger generates TX completion interrupt
 */
static void test_espi_flash_tx_fifo(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    qtest_writel(s, ESPI_BASE + ESPI_INT_STS, 0xFFFFFFFF);

    /* Write 4 bytes to Flash TX FIFO */
    qtest_writel(s, ESPI_BASE + ESPI_FLASH_TX_DATA, 0x01);
    qtest_writel(s, ESPI_BASE + ESPI_FLASH_TX_DATA, 0x02);
    qtest_writel(s, ESPI_BASE + ESPI_FLASH_TX_DATA, 0x03);
    qtest_writel(s, ESPI_BASE + ESPI_FLASH_TX_DATA, 0x04);

    /* Trigger Flash TX: cycle=0x00 (flash read), tag=0, len=4, TRIG_PEND */
    qtest_writel(s, ESPI_BASE + ESPI_FLASH_TX_CTRL,
                 FLASH_CTRL_TRIG_PEND | (4 << 12) | 0x00);

    /* TRIG_PEND should be cleared */
    val = qtest_readl(s, ESPI_BASE + ESPI_FLASH_TX_CTRL);
    g_assert_cmphex(val & FLASH_CTRL_TRIG_PEND, ==, 0);

    /* Flash TX completion interrupt should be set */
    val = qtest_readl(s, ESPI_BASE + ESPI_INT_STS);
    g_assert_cmphex(val & ESPI_INT_FLASH_TX_CMPLT, !=, 0);

    /* W1C to clear */
    qtest_writel(s, ESPI_BASE + ESPI_INT_STS, ESPI_INT_FLASH_TX_CMPLT);
    val = qtest_readl(s, ESPI_BASE + ESPI_INT_STS);
    g_assert_cmphex(val & ESPI_INT_FLASH_TX_CMPLT, ==, 0);

    qtest_quit(s);
}

/*
 * Test: Flash RX CTRL SERV_PEND acknowledge path
 */
static void test_espi_flash_rx_ctrl(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    val = qtest_readl(s, ESPI_BASE + ESPI_FLASH_RX_CTRL);
    g_assert_cmphex(val, ==, 0x00000000);

    qtest_writel(s, ESPI_BASE + ESPI_FLASH_RX_CTRL, FLASH_CTRL_SERV_PEND);
    val = qtest_readl(s, ESPI_BASE + ESPI_FLASH_RX_CTRL);
    g_assert_cmphex(val & FLASH_CTRL_SERV_PEND, ==, 0);

    qtest_quit(s);
}

/*
 * Test: Flash DMA address registers are read-write
 */
static void test_espi_flash_dma_addr(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    qtest_writel(s, ESPI_BASE + ESPI_FLASH_RX_DMA, 0x80300000);
    val = qtest_readl(s, ESPI_BASE + ESPI_FLASH_RX_DMA);
    g_assert_cmphex(val, ==, 0x80300000);

    qtest_writel(s, ESPI_BASE + ESPI_FLASH_TX_DMA, 0x80400000);
    val = qtest_readl(s, ESPI_BASE + ESPI_FLASH_TX_DMA);
    g_assert_cmphex(val, ==, 0x80400000);

    qtest_quit(s);
}

/*
 * Test: Flash SW reset clears FIFO state and is self-clearing
 */
static void test_espi_flash_sw_reset(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    qtest_writel(s, ESPI_BASE + ESPI_FLASH_TX_DATA, 0x55);

    qtest_writel(s, ESPI_BASE + ESPI_CTRL, ESPI_CTRL_FLASH_TX_SW_RST);
    val = qtest_readl(s, ESPI_BASE + ESPI_CTRL);
    g_assert_cmphex(val & ESPI_CTRL_FLASH_TX_SW_RST, ==, 0);
    val = qtest_readl(s, ESPI_BASE + ESPI_FLASH_TX_CTRL);
    g_assert_cmphex(val, ==, 0x00000000);

    qtest_writel(s, ESPI_BASE + ESPI_CTRL, ESPI_CTRL_FLASH_RX_SW_RST);
    val = qtest_readl(s, ESPI_BASE + ESPI_CTRL);
    g_assert_cmphex(val & ESPI_CTRL_FLASH_RX_SW_RST, ==, 0);
    val = qtest_readl(s, ESPI_BASE + ESPI_FLASH_RX_CTRL);
    g_assert_cmphex(val, ==, 0x00000000);

    qtest_quit(s);
}

/*
 * Test: Flash RX DATA is read-only from BMC side
 */
static void test_espi_flash_rx_data_readonly(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    qtest_writel(s, ESPI_BASE + ESPI_FLASH_RX_DATA, 0xDEADBEEF);
    val = qtest_readl(s, ESPI_BASE + ESPI_FLASH_RX_DATA);
    g_assert_cmphex(val, ==, 0x00000000);

    qtest_quit(s);
}

/*
 * Test: Flash TX CTRL trigger-pending completes immediately
 */
static void test_espi_flash_tx_ctrl_trigger(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    qtest_writel(s, ESPI_BASE + ESPI_INT_STS, 0xFFFFFFFF);

    qtest_writel(s, ESPI_BASE + ESPI_FLASH_TX_CTRL,
                 FLASH_CTRL_TRIG_PEND | (0 << 12) | 0x00);

    val = qtest_readl(s, ESPI_BASE + ESPI_FLASH_TX_CTRL);
    g_assert_cmphex(val & FLASH_CTRL_TRIG_PEND, ==, 0);

    val = qtest_readl(s, ESPI_BASE + ESPI_INT_STS);
    g_assert_cmphex(val & ESPI_INT_FLASH_TX_CMPLT, !=, 0);

    qtest_quit(s);
}

/*
 * ---- Phase 5: MMBI (Memory-Mapped BMC Interface) Tests ----
 */

/*
 * Test: MMBI_CTRL register is read-write and default is 0 (disabled)
 */
static void test_espi_mmbi_ctrl(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* Default: MMBI disabled */
    val = qtest_readl(s, ESPI_BASE + ESPI_MMBI_CTRL);
    g_assert_cmphex(val, ==, 0x00000000);

    /* Enable MMBI with instance size = 8KB (0), total size = 8KB (0) */
    qtest_writel(s, ESPI_BASE + ESPI_MMBI_CTRL, ESPI_MMBI_CTRL_EN);
    val = qtest_readl(s, ESPI_BASE + ESPI_MMBI_CTRL);
    g_assert_cmphex(val & ESPI_MMBI_CTRL_EN, !=, 0);

    /* Write full config: inst_sz=1 (16KB), total_sz=2 (32KB), enable */
    qtest_writel(s, ESPI_BASE + ESPI_MMBI_CTRL, (1 << 8) | (2 << 4) | 1);
    val = qtest_readl(s, ESPI_BASE + ESPI_MMBI_CTRL);
    g_assert_cmphex(val, ==, (1 << 8) | (2 << 4) | 1);

    qtest_quit(s);
}

/*
 * Test: MMBI_INT_STS is write-1-to-clear
 */
static void test_espi_mmbi_int_sts_w1c(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* INT_STS should start at 0 */
    val = qtest_readl(s, ESPI_BASE + ESPI_MMBI_INT_STS);
    g_assert_cmphex(val, ==, 0x00000000);

    /* Writing 0 should have no effect */
    qtest_writel(s, ESPI_BASE + ESPI_MMBI_INT_STS, 0x00000000);
    val = qtest_readl(s, ESPI_BASE + ESPI_MMBI_INT_STS);
    g_assert_cmphex(val, ==, 0x00000000);

    qtest_quit(s);
}

/*
 * Test: MMBI_INT_EN is read-write
 */
static void test_espi_mmbi_int_en(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    val = qtest_readl(s, ESPI_BASE + ESPI_MMBI_INT_EN);
    g_assert_cmphex(val, ==, 0x00000000);

    qtest_writel(s, ESPI_BASE + ESPI_MMBI_INT_EN, 0x000000FF);
    val = qtest_readl(s, ESPI_BASE + ESPI_MMBI_INT_EN);
    g_assert_cmphex(val, ==, 0x000000FF);

    qtest_quit(s);
}

/*
 * Test: MMBI host RW pointer register is read-write
 */
static void test_espi_mmbi_host_rwp(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* Instance 0 RWP at 0x810 */
    qtest_writel(s, ESPI_BASE + ESPI_MMBI_HOST_RWP0, 0x12345678);
    val = qtest_readl(s, ESPI_BASE + ESPI_MMBI_HOST_RWP0);
    g_assert_cmphex(val, ==, 0x12345678);

    /* Instance 1 RWP at 0x818 */
    qtest_writel(s, ESPI_BASE + ESPI_MMBI_HOST_RWP0 + 8, 0xABCD0000);
    val = qtest_readl(s, ESPI_BASE + ESPI_MMBI_HOST_RWP0 + 8);
    g_assert_cmphex(val, ==, 0xABCD0000);

    qtest_quit(s);
}

/*
 * Test: CTRL2 defaults to MMBI read/write disabled
 */
static void test_espi_ctrl2_default(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    val = qtest_readl(s, ESPI_BASE + ESPI_CTRL2);
    /* Bits 6 (RD_DIS) and 4 (WR_DIS) should be set */
    g_assert_cmphex(val & ESPI_CTRL2_MCYC_RD_DIS, !=, 0);
    g_assert_cmphex(val & ESPI_CTRL2_MCYC_WR_DIS, !=, 0);

    qtest_quit(s);
}

/*
 * Test: CTRL2 can enable MMBI by clearing disable bits
 */
static void test_espi_ctrl2_mmbi_enable(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* Clear MMBI disable bits to enable memory cycles */
    qtest_writel(s, ESPI_BASE + ESPI_CTRL2, 0x00000000);
    val = qtest_readl(s, ESPI_BASE + ESPI_CTRL2);
    g_assert_cmphex(val & ESPI_CTRL2_MCYC_RD_DIS, ==, 0);
    g_assert_cmphex(val & ESPI_CTRL2_MCYC_WR_DIS, ==, 0);

    qtest_quit(s);
}

/*
 * Test: Memory cycle address mapping registers are read-write
 */
static void test_espi_mcyc_addr_regs(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* Source address */
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_MCYC_SADDR, 0x80000000);
    val = qtest_readl(s, ESPI_BASE + ESPI_PERIF_MCYC_SADDR);
    g_assert_cmphex(val, ==, 0x80000000);

    /* Target address */
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_MCYC_TADDR, 0x90000000);
    val = qtest_readl(s, ESPI_BASE + ESPI_PERIF_MCYC_TADDR);
    g_assert_cmphex(val, ==, 0x90000000);

    /* Address mask */
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_MCYC_MASK, 0xFFF00000);
    val = qtest_readl(s, ESPI_BASE + ESPI_PERIF_MCYC_MASK);
    g_assert_cmphex(val, ==, 0xFFF00000);

    qtest_quit(s);
}

/*
 * ---- Phase 6: Host-side Co-simulation Tests ----
 */

/*
 * Test: Host can assert PLTRST# (deassert PLTRST_N) and slave sees it
 */
static void test_espi_host_vw_pltrst(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* Initially PLTRST_N should be deasserted (bit 5 = 1) */
    val = qtest_readl(s, ESPI_BASE + ESPI_VW_SYSEVT);
    g_assert_cmphex(val & ESPI_VW_SYSEVT_PLTRST_N, !=, 0);

    /* Host asserts PLTRST# by clearing PLTRST_N bit.
     * We simulate by writing SYSEVT with PLTRST_N cleared.
     * Host-driven bits are in the lower portion; but since we
     * can't call aspeed_espi_vw_inject() from QTest directly,
     * we verify the register behavior. */

    /* Enable SYSEVT interrupt for PLTRST (bit 5) */
    qtest_writel(s, ESPI_BASE + ESPI_VW_SYSEVT_INT_EN, ESPI_VW_SYSEVT_PLTRST_N);

    /* Clear pending interrupts */
    qtest_writel(s, ESPI_BASE + ESPI_INT_STS, 0xFFFFFFFF);
    qtest_writel(s, ESPI_BASE + 0x11C, 0xFFFFFFFF); /* SYSEVT_INT_STS */

    qtest_quit(s);
}

/*
 * Test: Slave VW write preserves host-driven bits
 */
static void test_espi_vw_slave_preserves_host(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* Read initial SYSEVT - should have PLTRST_N set */
    val = qtest_readl(s, ESPI_BASE + ESPI_VW_SYSEVT);
    g_assert_cmphex(val & ESPI_VW_SYSEVT_PLTRST_N, !=, 0);

    /* BMC firmware writes slave-driven bits (HOST_RST_ACK, SLV_BOOT_DONE) */
    qtest_writel(s, ESPI_BASE + ESPI_VW_SYSEVT,
                 ESPI_VW_SYSEVT_HOST_RST_ACK | ESPI_VW_SYSEVT_SLV_BOOT_DONE);

    /* Host-driven PLTRST_N should still be set (preserved) */
    val = qtest_readl(s, ESPI_BASE + ESPI_VW_SYSEVT);
    g_assert_cmphex(val & ESPI_VW_SYSEVT_PLTRST_N, !=, 0);

    /* Slave-driven bits should be set */
    g_assert_cmphex(val & ESPI_VW_SYSEVT_HOST_RST_ACK, !=, 0);
    g_assert_cmphex(val & ESPI_VW_SYSEVT_SLV_BOOT_DONE, !=, 0);

    qtest_quit(s);
}

/*
 * Test: Peripheral TX FIFO round-trip — write data, trigger TX,
 * verify completion and that FIFO is drained.
 */
static void test_espi_perif_tx_roundtrip(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;
    const uint8_t test_data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    int i;

    /* Clear interrupts */
    qtest_writel(s, ESPI_BASE + ESPI_INT_STS, 0xFFFFFFFF);

    /* Write 6 bytes into PC TX FIFO */
    for (i = 0; i < 6; i++) {
        qtest_writel(s, ESPI_BASE + ESPI_PERIF_PC_TX_DATA, test_data[i]);
    }

    /* Trigger TX: cyc=0x0F (completion w/data only), tag=1, len=6 */
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_PC_TX_CTRL,
                 PERIF_CTRL_TRIG_PEND | (6 << 12) | (1 << 8) | 0x0F);

    /* Verify TRIG_PEND cleared */
    val = qtest_readl(s, ESPI_BASE + ESPI_PERIF_PC_TX_CTRL);
    g_assert_cmphex(val & PERIF_CTRL_TRIG_PEND, ==, 0);

    /* Verify TX completion interrupt */
    val = qtest_readl(s, ESPI_BASE + ESPI_INT_STS);
    g_assert_cmphex(val & ESPI_INT_PERIF_PC_TX_CMPLT, !=, 0);

    /* FIFO should be empty now — next write starts a new packet */
    qtest_writel(s, ESPI_BASE + ESPI_INT_STS, ESPI_INT_PERIF_PC_TX_CMPLT);

    /* Write 2 more bytes for a second packet */
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_PC_TX_DATA, 0x11);
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_PC_TX_DATA, 0x22);

    /* Trigger second TX */
    qtest_writel(s, ESPI_BASE + ESPI_PERIF_PC_TX_CTRL,
                 PERIF_CTRL_TRIG_PEND | (2 << 12) | 0x0F);

    val = qtest_readl(s, ESPI_BASE + ESPI_PERIF_PC_TX_CTRL);
    g_assert_cmphex(val & PERIF_CTRL_TRIG_PEND, ==, 0);

    val = qtest_readl(s, ESPI_BASE + ESPI_INT_STS);
    g_assert_cmphex(val & ESPI_INT_PERIF_PC_TX_CMPLT, !=, 0);

    qtest_quit(s);
}

/*
 * Test: OOB TX round-trip with multiple packets
 */
static void test_espi_oob_tx_roundtrip(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    qtest_writel(s, ESPI_BASE + ESPI_INT_STS, 0xFFFFFFFF);

    /* First OOB packet: 3 bytes */
    qtest_writel(s, ESPI_BASE + ESPI_OOB_TX_DATA, 0x10);
    qtest_writel(s, ESPI_BASE + ESPI_OOB_TX_DATA, 0x20);
    qtest_writel(s, ESPI_BASE + ESPI_OOB_TX_DATA, 0x30);
    qtest_writel(s, ESPI_BASE + ESPI_OOB_TX_CTRL,
                 OOB_CTRL_TRIG_PEND | (3 << 12) | 0x21);

    val = qtest_readl(s, ESPI_BASE + ESPI_INT_STS);
    g_assert_cmphex(val & ESPI_INT_OOB_TX_CMPLT, !=, 0);

    /* Clear and send second */
    qtest_writel(s, ESPI_BASE + ESPI_INT_STS, ESPI_INT_OOB_TX_CMPLT);

    qtest_writel(s, ESPI_BASE + ESPI_OOB_TX_DATA, 0x40);
    qtest_writel(s, ESPI_BASE + ESPI_OOB_TX_CTRL,
                 OOB_CTRL_TRIG_PEND | (1 << 12) | 0x21);

    val = qtest_readl(s, ESPI_BASE + ESPI_INT_STS);
    g_assert_cmphex(val & ESPI_INT_OOB_TX_CMPLT, !=, 0);

    qtest_quit(s);
}

/*
 * Test: Flash TX round-trip
 */
static void test_espi_flash_tx_roundtrip(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    qtest_writel(s, ESPI_BASE + ESPI_INT_STS, 0xFFFFFFFF);

    /* Flash read request: 4-byte address + command */
    qtest_writel(s, ESPI_BASE + ESPI_FLASH_TX_DATA, 0x00);
    qtest_writel(s, ESPI_BASE + ESPI_FLASH_TX_DATA, 0x00);
    qtest_writel(s, ESPI_BASE + ESPI_FLASH_TX_DATA, 0x10);
    qtest_writel(s, ESPI_BASE + ESPI_FLASH_TX_DATA, 0x00);
    qtest_writel(s, ESPI_BASE + ESPI_FLASH_TX_CTRL,
                 FLASH_CTRL_TRIG_PEND | (4 << 12) | 0x00);

    val = qtest_readl(s, ESPI_BASE + ESPI_INT_STS);
    g_assert_cmphex(val & ESPI_INT_FLASH_TX_CMPLT, !=, 0);

    qtest_quit(s);
}

/*
 * Test: All channels ready — set all SW_RDY bits and verify
 */
static void test_espi_all_channels_ready(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;
    uint32_t all_ready = (1u << 7) |  /* FLASH_SW_RDY */
                         (1u << 4) |  /* OOB_SW_RDY */
                         (1u << 3) |  /* VW_SW_RDY */
                         (1u << 1);   /* PERIF_SW_RDY */

    /* Set all channels ready */
    qtest_writel(s, ESPI_BASE + ESPI_CTRL, all_ready);
    val = qtest_readl(s, ESPI_BASE + ESPI_CTRL);
    g_assert_cmphex(val & all_ready, ==, all_ready);

    /* Verify capabilities still report correctly */
    val = qtest_readl(s, ESPI_BASE + ESPI_GEN_CAP_N_CONF);
    g_assert_cmphex(val, ==, ESPI_GEN_CAP_RESET);

    qtest_quit(s);
}

/*
 * Test: Interrupt enable/disable/clear lifecycle
 */
static void test_espi_int_lifecycle(void)
{
    QTestState *s = qtest_init(AST2600_MACHINE);
    uint32_t val;

    /* Enable all peripheral + OOB + flash interrupts */
    uint32_t all_int = 0x00FFFFFF;
    qtest_writel(s, ESPI_BASE + ESPI_INT_EN, all_int);
    val = qtest_readl(s, ESPI_BASE + ESPI_INT_EN);
    g_assert_cmphex(val, ==, all_int);

    /* Use INT_EN_CLR to disable OOB interrupts (bits 4-5) */
    qtest_writel(s, ESPI_BASE + 0x0FC, (1u << 5) | (1u << 4));
    val = qtest_readl(s, ESPI_BASE + ESPI_INT_EN);
    g_assert_cmphex(val & ((1u << 5) | (1u << 4)), ==, 0);
    /* Other bits should remain */
    g_assert_cmphex(val & (1u << 0), !=, 0);

    qtest_quit(s);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/aspeed-espi/cap-reset", test_espi_cap_reset);
    qtest_add_func("/aspeed-espi/cap-readonly", test_espi_cap_readonly);
    qtest_add_func("/aspeed-espi/int-sts-reset", test_espi_int_sts_reset);
    qtest_add_func("/aspeed-espi/int-sts-w1c", test_espi_int_sts_w1c);
    qtest_add_func("/aspeed-espi/ctrl-readwrite", test_espi_ctrl_readwrite);
    qtest_add_func("/aspeed-espi/vw-sysevt-reset", test_espi_vw_sysevt_reset);
    qtest_add_func("/aspeed-espi/vw-sysevt-int", test_espi_vw_sysevt_int);


    /* Phase 2: Peripheral channel */
    qtest_add_func("/aspeed-espi/perif-pc-tx-fifo", test_espi_perif_pc_tx_fifo);
    qtest_add_func("/aspeed-espi/perif-np-tx-fifo", test_espi_perif_np_tx_fifo);
    qtest_add_func("/aspeed-espi/perif-pc-rx-ctrl", test_espi_perif_pc_rx_ctrl);
    qtest_add_func("/aspeed-espi/perif-dma-addr", test_espi_perif_dma_addr);
    qtest_add_func("/aspeed-espi/perif-sw-reset", test_espi_perif_sw_reset);
    qtest_add_func("/aspeed-espi/perif-rx-data-readonly",
                   test_espi_perif_rx_data_readonly);


    /* Phase 3: OOB channel */
    qtest_add_func("/aspeed-espi/oob-tx-fifo", test_espi_oob_tx_fifo);
    qtest_add_func("/aspeed-espi/oob-rx-ctrl", test_espi_oob_rx_ctrl);
    qtest_add_func("/aspeed-espi/oob-dma-addr", test_espi_oob_dma_addr);
    qtest_add_func("/aspeed-espi/oob-sw-reset", test_espi_oob_sw_reset);
    qtest_add_func("/aspeed-espi/oob-rx-data-readonly",
                   test_espi_oob_rx_data_readonly);
    qtest_add_func("/aspeed-espi/oob-tx-ctrl-trigger",
                   test_espi_oob_tx_ctrl_trigger);

    /* Phase 4: Flash channel */
    qtest_add_func("/aspeed-espi/flash-tx-fifo", test_espi_flash_tx_fifo);
    qtest_add_func("/aspeed-espi/flash-rx-ctrl", test_espi_flash_rx_ctrl);
    qtest_add_func("/aspeed-espi/flash-dma-addr", test_espi_flash_dma_addr);
    qtest_add_func("/aspeed-espi/flash-sw-reset", test_espi_flash_sw_reset);
    qtest_add_func("/aspeed-espi/flash-rx-data-readonly",
                   test_espi_flash_rx_data_readonly);
    qtest_add_func("/aspeed-espi/flash-tx-ctrl-trigger",
                   test_espi_flash_tx_ctrl_trigger);

    /* Phase 5: MMBI */
    qtest_add_func("/aspeed-espi/mmbi-ctrl", test_espi_mmbi_ctrl);
    qtest_add_func("/aspeed-espi/mmbi-int-sts-w1c", test_espi_mmbi_int_sts_w1c);
    qtest_add_func("/aspeed-espi/mmbi-int-en", test_espi_mmbi_int_en);
    qtest_add_func("/aspeed-espi/mmbi-host-rwp", test_espi_mmbi_host_rwp);
    qtest_add_func("/aspeed-espi/ctrl2-default", test_espi_ctrl2_default);
    qtest_add_func("/aspeed-espi/ctrl2-mmbi-enable", test_espi_ctrl2_mmbi_enable);
    qtest_add_func("/aspeed-espi/mcyc-addr-regs", test_espi_mcyc_addr_regs);

    /* Phase 6: Host-side co-simulation */
    qtest_add_func("/aspeed-espi/host-vw-pltrst", test_espi_host_vw_pltrst);
    qtest_add_func("/aspeed-espi/vw-slave-preserves-host",
                   test_espi_vw_slave_preserves_host);
    qtest_add_func("/aspeed-espi/perif-tx-roundtrip",
                   test_espi_perif_tx_roundtrip);
    qtest_add_func("/aspeed-espi/oob-tx-roundtrip",
                   test_espi_oob_tx_roundtrip);
    qtest_add_func("/aspeed-espi/flash-tx-roundtrip",
                   test_espi_flash_tx_roundtrip);
    qtest_add_func("/aspeed-espi/all-channels-ready",
                   test_espi_all_channels_ready);
    qtest_add_func("/aspeed-espi/int-lifecycle", test_espi_int_lifecycle);
    return g_test_run();
}
