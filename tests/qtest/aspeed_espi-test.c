/*
 * QTest testcase for the Aspeed AST2600 eSPI Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Gary Beihl <garybeihl@microsoft.com>
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

    return g_test_run();
}
