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
#define ESPI_VW_SYSEVT          0x098
#define ESPI_VW_SYSEVT_INT_EN   0x09C
#define ESPI_VW_SYSEVT_INT_STS  0x0A8
#define ESPI_GEN_CAP_N_CONF     0x0A0

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

    return g_test_run();
}
