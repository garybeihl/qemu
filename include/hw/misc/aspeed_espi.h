/*
 * ASPEED eSPI Controller
 *
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Implementation of the AST2600 eSPI controller register interface.
 * Reference: Aspeed AST2600 datasheet, ast2600-espi.h Linux driver headers,
 * and Intel eSPI Base Specification Rev 1.0.
 */

#ifndef ASPEED_ESPI_H
#define ASPEED_ESPI_H

#include "hw/core/sysbus.h"

/*
 * The eSPI register space spans from 0x000 to at least 0x810+.
 * MMBI registers extend to 0x810 + (N * 8). We cover up to 0x900
 * to be safe for future expansion.
 */
#define ASPEED_ESPI_NR_REGS     (0x900 >> 2)

/* Maximum payload size for peripheral channel FIFO (bytes) */
#define ASPEED_ESPI_PERIF_FIFO_SIZE  256

/* Maximum payload size for OOB channel FIFO (bytes) */
#define ASPEED_ESPI_OOB_FIFO_SIZE    256

/* Maximum payload size for Flash channel FIFO (bytes) */
#define ASPEED_ESPI_FLASH_FIFO_SIZE  256

#define TYPE_ASPEED_ESPI "aspeed.espi"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedESPIState, ASPEED_ESPI)

struct AspeedESPIState {
    /* <private> */
    SysBusDevice parent;

    /* <public> */
    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t regs[ASPEED_ESPI_NR_REGS];

    /* Peripheral channel (CH0) FIFO buffers */
    uint8_t  pc_rx_buf[ASPEED_ESPI_PERIF_FIFO_SIZE];
    uint32_t pc_rx_len;
    uint32_t pc_rx_pos;

    uint8_t  pc_tx_buf[ASPEED_ESPI_PERIF_FIFO_SIZE];
    uint32_t pc_tx_len;

    uint8_t  np_tx_buf[ASPEED_ESPI_PERIF_FIFO_SIZE];
    uint32_t np_tx_len;

    /* OOB channel (CH2) FIFO buffers */
    uint8_t  oob_rx_buf[ASPEED_ESPI_OOB_FIFO_SIZE];
    uint32_t oob_rx_len;
    uint32_t oob_rx_pos;

    uint8_t  oob_tx_buf[ASPEED_ESPI_OOB_FIFO_SIZE];
    uint32_t oob_tx_len;

    /* Flash channel (CH3) FIFO buffers */
    uint8_t  flash_rx_buf[ASPEED_ESPI_FLASH_FIFO_SIZE];
    uint32_t flash_rx_len;
    uint32_t flash_rx_pos;

    uint8_t  flash_tx_buf[ASPEED_ESPI_FLASH_FIFO_SIZE];
    uint32_t flash_tx_len;

    /* DMA support */
    AddressSpace dma_as;
    MemoryRegion *dram_mr;
};

/* eSPI register offsets (from ast2600-espi.h in Aspeed Linux driver) */

/* Main control and status */
#define R_ESPI_CTRL             (0x000 / 4)
#define R_ESPI_STS              (0x004 / 4)
#define R_ESPI_INT_STS          (0x008 / 4)
#define R_ESPI_INT_EN           (0x00C / 4)

/* Peripheral channel (CH0) - Phase 2 */
#define R_ESPI_PERIF_PC_RX_DMA  (0x010 / 4)
#define R_ESPI_PERIF_PC_RX_CTRL (0x014 / 4)
#define R_ESPI_PERIF_PC_RX_DATA (0x018 / 4)
#define R_ESPI_PERIF_PC_TX_DMA  (0x020 / 4)
#define R_ESPI_PERIF_PC_TX_CTRL (0x024 / 4)
#define R_ESPI_PERIF_PC_TX_DATA (0x028 / 4)
#define R_ESPI_PERIF_NP_TX_DMA  (0x030 / 4)
#define R_ESPI_PERIF_NP_TX_CTRL (0x034 / 4)
#define R_ESPI_PERIF_NP_TX_DATA (0x038 / 4)


/* Peripheral channel CTRL register bit definitions */
#define ESPI_PERIF_PC_RX_CTRL_SERV_PEND   BIT(31)
#define ESPI_PERIF_PC_TX_CTRL_TRIG_PEND   BIT(31)
#define ESPI_PERIF_NP_TX_CTRL_TRIG_PEND   BIT(31)

/* OOB channel CTRL register bit definitions */
#define ESPI_OOB_RX_CTRL_SERV_PEND        BIT(31)
#define ESPI_OOB_TX_CTRL_TRIG_PEND        BIT(31)

/* Flash channel CTRL register bit definitions */
#define ESPI_FLASH_RX_CTRL_SERV_PEND      BIT(31)
#define ESPI_FLASH_TX_CTRL_TRIG_PEND      BIT(31)

/* CTRL register field masks and shifts */
#define ESPI_PERIF_CTRL_LEN_MASK     0x00FFF000
#define ESPI_PERIF_CTRL_LEN_SHIFT    12
#define ESPI_PERIF_CTRL_TAG_MASK     0x00000F00
#define ESPI_PERIF_CTRL_TAG_SHIFT    8
#define ESPI_PERIF_CTRL_CYC_MASK     0x000000FF
#define ESPI_PERIF_CTRL_CYC_SHIFT    0

/* OOB CTRL register field masks (same layout as peripheral channel) */
#define ESPI_OOB_CTRL_LEN_MASK       0x00FFF000
#define ESPI_OOB_CTRL_LEN_SHIFT      12
#define ESPI_OOB_CTRL_TAG_MASK       0x00000F00
#define ESPI_OOB_CTRL_TAG_SHIFT      8
#define ESPI_OOB_CTRL_CYC_MASK       0x000000FF
#define ESPI_OOB_CTRL_CYC_SHIFT      0

/* Flash CTRL register field masks (same layout as peripheral/OOB) */
#define ESPI_FLASH_CTRL_LEN_MASK     0x00FFF000
#define ESPI_FLASH_CTRL_LEN_SHIFT    12
#define ESPI_FLASH_CTRL_TAG_MASK     0x00000F00
#define ESPI_FLASH_CTRL_TAG_SHIFT    8
#define ESPI_FLASH_CTRL_CYC_MASK     0x000000FF
#define ESPI_FLASH_CTRL_CYC_SHIFT    0

/* OOB channel (CH2) - Phase 3 */
#define R_ESPI_OOB_RX_DMA      (0x040 / 4)
#define R_ESPI_OOB_RX_CTRL     (0x044 / 4)
#define R_ESPI_OOB_RX_DATA     (0x048 / 4)
#define R_ESPI_OOB_TX_DMA      (0x050 / 4)
#define R_ESPI_OOB_TX_CTRL     (0x054 / 4)
#define R_ESPI_OOB_TX_DATA     (0x058 / 4)


/* MMBI (Memory-Mapped BMC Interface) registers - Phase 5 */
#define R_ESPI_MMBI_CTRL        (0x800 / 4)
#define R_ESPI_MMBI_INT_STS     (0x808 / 4)
#define R_ESPI_MMBI_INT_EN      (0x80C / 4)
/* Per-instance host RW pointers: 0x810 + (instance * 8) */
#define R_ESPI_MMBI_HOST_RWP(x) ((0x810 + ((x) * 8)) / 4)

/* MMBI_CTRL bit definitions */
#define ESPI_MMBI_CTRL_EN               BIT(0)
#define ESPI_MMBI_CTRL_TOTAL_SZ_MASK    GENMASK(6, 4)
#define ESPI_MMBI_CTRL_TOTAL_SZ_SHIFT   4
#define ESPI_MMBI_CTRL_INST_SZ_MASK     GENMASK(10, 8)
#define ESPI_MMBI_CTRL_INST_SZ_SHIFT    8

/* CTRL2 register MMBI-related bits */
#define ESPI_CTRL2_VW_TX_SORT           BIT(30)
#define ESPI_CTRL2_MCYC_RD_DIS_WDT     BIT(11)
#define ESPI_CTRL2_MCYC_WR_DIS_WDT     BIT(10)
#define ESPI_CTRL2_MCYC_RD_DIS         BIT(6)
#define ESPI_CTRL2_MCYC_WR_DIS         BIT(4)

/* Maximum MMBI instances (up to 8 per register space) */
#define ASPEED_ESPI_MMBI_MAX_INST       8
/* Flash channel (CH3) - Phase 4 */
#define R_ESPI_FLASH_RX_DMA    (0x060 / 4)
#define R_ESPI_FLASH_RX_CTRL   (0x064 / 4)
#define R_ESPI_FLASH_RX_DATA   (0x068 / 4)
#define R_ESPI_FLASH_TX_DMA    (0x070 / 4)
#define R_ESPI_FLASH_TX_CTRL   (0x074 / 4)
#define R_ESPI_FLASH_TX_DATA   (0x078 / 4)

/* Control register 2 and memory cycle mapping */
#define R_ESPI_CTRL2            (0x080 / 4)
#define R_ESPI_PERIF_MCYC_SADDR (0x084 / 4)
#define R_ESPI_PERIF_MCYC_TADDR (0x088 / 4)
#define R_ESPI_PERIF_MCYC_MASK  (0x08C / 4)
#define R_ESPI_FLASH_SAFS_TADDR (0x090 / 4)

/* MMBI address mapping (aliases for peripheral memory cycle registers) */
#define R_ESPI_PERIF_MMBI_SADDR R_ESPI_PERIF_MCYC_SADDR
#define R_ESPI_PERIF_MMBI_TADDR R_ESPI_PERIF_MCYC_TADDR
#define R_ESPI_PERIF_MMBI_MASK  R_ESPI_PERIF_MCYC_MASK

/* Virtual Wire channel (CH1) - Phase 1 */
#define R_ESPI_VW_SYSEVT_INT_EN (0x094 / 4)
#define R_ESPI_VW_SYSEVT       (0x098 / 4)
#define R_ESPI_VW_GPIO_VAL     (0x09C / 4)

/* Capabilities and configuration */
#define R_ESPI_GEN_CAP_N_CONF  (0x0A0 / 4)
#define R_ESPI_CH0_CAP_N_CONF  (0x0A4 / 4)
#define R_ESPI_CH1_CAP_N_CONF  (0x0A8 / 4)
#define R_ESPI_CH2_CAP_N_CONF  (0x0AC / 4)
#define R_ESPI_CH3_CAP_N_CONF  (0x0B0 / 4)
#define R_ESPI_CH3_CAP_N_CONF2 (0x0B4 / 4)

/* Virtual Wire GPIO direction and grouping */
#define R_ESPI_VW_GPIO_DIR     (0x0C0 / 4)
#define R_ESPI_VW_GPIO_GRP     (0x0C4 / 4)

/* Interrupt enable clear */
#define R_ESPI_INT_EN_CLR      (0x0FC / 4)

/* Extended system events (SYSEVT1) */
#define R_ESPI_VW_SYSEVT1_INT_EN (0x100 / 4)
#define R_ESPI_VW_SYSEVT1     (0x104 / 4)

/* System event interrupt type and status */
#define R_ESPI_VW_SYSEVT_INT_T0 (0x110 / 4)
#define R_ESPI_VW_SYSEVT_INT_T1 (0x114 / 4)
#define R_ESPI_VW_SYSEVT_INT_T2 (0x118 / 4)
#define R_ESPI_VW_SYSEVT_INT_STS (0x11C / 4)

/* SYSEVT1 interrupt type and status */
#define R_ESPI_VW_SYSEVT1_INT_T0 (0x120 / 4)
#define R_ESPI_VW_SYSEVT1_INT_T1 (0x124 / 4)
#define R_ESPI_VW_SYSEVT1_INT_T2 (0x128 / 4)
#define R_ESPI_VW_SYSEVT1_INT_STS (0x12C / 4)

/* ESPI_CTRL bit definitions */
#define ESPI_CTRL_FLASH_TX_SW_RST       BIT(31)
#define ESPI_CTRL_FLASH_RX_SW_RST       BIT(30)
#define ESPI_CTRL_OOB_TX_SW_RST         BIT(29)
#define ESPI_CTRL_OOB_RX_SW_RST         BIT(28)
#define ESPI_CTRL_PERIF_NP_TX_SW_RST    BIT(27)
#define ESPI_CTRL_PERIF_NP_RX_SW_RST    BIT(26)
#define ESPI_CTRL_PERIF_PC_TX_SW_RST    BIT(25)
#define ESPI_CTRL_PERIF_PC_RX_SW_RST    BIT(24)
#define ESPI_CTRL_FLASH_TX_DMA_EN       BIT(23)
#define ESPI_CTRL_FLASH_RX_DMA_EN       BIT(22)
#define ESPI_CTRL_OOB_TX_DMA_EN         BIT(21)
#define ESPI_CTRL_OOB_RX_DMA_EN         BIT(20)
#define ESPI_CTRL_PERIF_NP_TX_DMA_EN    BIT(19)
#define ESPI_CTRL_PERIF_PC_TX_DMA_EN    BIT(17)
#define ESPI_CTRL_PERIF_PC_RX_DMA_EN    BIT(16)
#define ESPI_CTRL_FLASH_SW_RDY          BIT(7)
#define ESPI_CTRL_OOB_SW_RDY            BIT(4)
#define ESPI_CTRL_VW_SW_RDY             BIT(3)
#define ESPI_CTRL_PERIF_SW_RDY          BIT(1)

/* ESPI_INT_STS / ESPI_INT_EN bit definitions */
#define ESPI_INT_RST_DEASSERT           BIT(31)
#define ESPI_INT_OOB_RX_TMOUT           BIT(23)
#define ESPI_INT_VW_SYSEVT1             BIT(22)
#define ESPI_INT_FLASH_TX_ERR           BIT(21)
#define ESPI_INT_OOB_TX_ERR             BIT(20)
#define ESPI_INT_FLASH_TX_ABT           BIT(19)
#define ESPI_INT_OOB_TX_ABT             BIT(18)
#define ESPI_INT_PERIF_NP_TX_ABT        BIT(17)
#define ESPI_INT_PERIF_PC_TX_ABT        BIT(16)
#define ESPI_INT_FLASH_RX_ABT           BIT(15)
#define ESPI_INT_OOB_RX_ABT             BIT(14)
#define ESPI_INT_PERIF_NP_RX_ABT        BIT(13)
#define ESPI_INT_PERIF_PC_RX_ABT        BIT(12)
#define ESPI_INT_PERIF_NP_TX_ERR        BIT(11)
#define ESPI_INT_PERIF_PC_TX_ERR        BIT(10)
#define ESPI_INT_VW_GPIO                BIT(9)
#define ESPI_INT_VW_SYSEVT              BIT(8)
#define ESPI_INT_FLASH_TX_CMPLT         BIT(7)
#define ESPI_INT_FLASH_RX_CMPLT         BIT(6)
#define ESPI_INT_OOB_TX_CMPLT           BIT(5)
#define ESPI_INT_OOB_RX_CMPLT           BIT(4)
#define ESPI_INT_PERIF_NP_TX_CMPLT      BIT(3)
#define ESPI_INT_PERIF_PC_TX_CMPLT      BIT(1)
#define ESPI_INT_PERIF_PC_RX_CMPLT      BIT(0)

/* ESPI_VW_SYSEVT bit definitions - host→BMC system events */
#define ESPI_VW_SYSEVT_HOST_RST_ACK     BIT(27)
#define ESPI_VW_SYSEVT_RST_CPU_INIT     BIT(26)
#define ESPI_VW_SYSEVT_SLV_BOOT_STS     BIT(23)
#define ESPI_VW_SYSEVT_NON_FATAL_ERR    BIT(22)
#define ESPI_VW_SYSEVT_FATAL_ERR        BIT(21)
#define ESPI_VW_SYSEVT_SLV_BOOT_DONE    BIT(20)
#define ESPI_VW_SYSEVT_OOB_RST_ACK      BIT(16)
#define ESPI_VW_SYSEVT_NMI_OUT          BIT(10)
#define ESPI_VW_SYSEVT_SMI_OUT          BIT(9)
#define ESPI_VW_SYSEVT_HOST_RST_WARN    BIT(8)
#define ESPI_VW_SYSEVT_OOB_RST_WARN     BIT(6)
#define ESPI_VW_SYSEVT_PLTRST_N         BIT(5)
#define ESPI_VW_SYSEVT_SUSPEND          BIT(4)
#define ESPI_VW_SYSEVT_S5_SLEEP         BIT(2)
#define ESPI_VW_SYSEVT_S4_SLEEP         BIT(1)
#define ESPI_VW_SYSEVT_S3_SLEEP         BIT(0)

/* ESPI_VW_SYSEVT1 bit definitions */
#define ESPI_VW_SYSEVT1_SUSPEND_ACK     BIT(20)
#define ESPI_VW_SYSEVT1_SUSPEND_WARN    BIT(0)

/*
 * AST2600 A3 eSPI general capabilities reset value.
 * Indicates support for all 4 channels, 66MHz max freq, single slave.
 */
#define ESPI_GEN_CAP_N_CONF_RESET       0x0000F759

/*
 * Per-channel capability reset values.
 * These report what the HW supports; firmware reads them during probe.
 */
#define ESPI_CH0_CAP_N_CONF_RESET       0x00000073
#define ESPI_CH1_CAP_N_CONF_RESET       0x00000033
#define ESPI_CH2_CAP_N_CONF_RESET       0x00000033
#define ESPI_CH3_CAP_N_CONF_RESET       0x00000003
#define ESPI_CH3_CAP_N_CONF2_RESET      0x00000000

/*
 * Inject a peripheral channel RX packet (simulates host-to-BMC traffic).
 * Used by QTest and potentially by a future host-side eSPI master model.
 */
void aspeed_espi_perif_pc_rx_inject(AspeedESPIState *s, uint8_t cyc,
                                     uint8_t tag, const uint8_t *data,
                                     uint32_t len);

/*
 * Inject an OOB channel RX packet (simulates host-to-BMC OOB message).
 * Used by QTest and potentially by a future host-side eSPI master model.
 */
void aspeed_espi_oob_rx_inject(AspeedESPIState *s, uint8_t cyc,
                                uint8_t tag, const uint8_t *data,
                                uint32_t len);

/*
 * Inject a Flash channel RX packet (simulates host-to-BMC flash response).
 * Used by QTest and potentially by a future host-side eSPI master model.
 */
void aspeed_espi_flash_rx_inject(AspeedESPIState *s, uint8_t cyc,
                                  uint8_t tag, const uint8_t *data,
                                  uint32_t len);

#endif /* ASPEED_ESPI_H */
