/*
 * Copyright (c) 2025 Wolfgang Birkner
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/bshbus.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(ti_bshbus, CONFIG_BSHBUS_LOG_LEVEL);

#define DT_DRV_COMPAT ti_bshbus

/* SPI Header and Size Definitions */
#define TIBBUS_SPI_HDR_SIZE           	4u
#define TIBBUS_SPI_HDR_BYTE_CMD       	0u
#define TIBBUS_SPI_HDR_BYTE_ADDR_HIGH	1u
#define TIBBUS_SPI_HDR_BYTE_ADDR_LOW  	2u
#define TIBBUS_SPI_HDR_BYTE_DATA_LEN  	3u
#define TIBBUS_REG_SIZE               	4u

/* SPI Command Definitions */
#define TIBBUS_WRITE_H 	0x60
#define TIBBUS_WRITE_L 	0x61
#define TIBBUS_READ_H  	0x40
#define TIBBUS_READ_L  	0x41

/* Register Addresses */
#define TIBBUS_DEVICE_ID0_ADDR   0x0000u
#define TIBBUS_DEVICE_ID1_ADDR   0x0004u
#define TIBBUS_REVISION_ADDR     0x0008u
#define TIBBUS_STATUS_ADDR       0x000Cu
#define TIBBUS_SPI_CRC_CFG_ADDR  0x0014u
#define TIBBUS_MOPC_ADDR         0x0800u
#define TIBBUS_IPEC_ADDR         0x0814u
#define TIBBUS_EEPP_ADDR         0x0818u
#define TIBBUS_IF_ADDR           0x0820u
#define TIBBUS_IE_ADDR           0x0830u
#define TIBBUS_DBUS_CCCR_ADDR    0x4018u
#define TIBBUS_DBUS_DBR_ADDR     0x404Cu
#define TIBBUS_DBUS_IR_ADDR      0x4050u
#define TIBBUS_DBUS_IE_ADDR      0x4054u
#define TIBBUS_DBUS_BSA_ADDR     0x4060u
#define TIBBUS_DBUS_BCC_ADDR     0x406Cu
#define TIBBUS_DBUS_DPC_ADDR     0x4074u
#define TIBBUS_DBUS_SIDFC_ADDR   0x4084u

/* Register Bit Masks and Positions */
/* MOPC */
#define TIBBUS_MOPC_MODE_SEL_MASK     0x000000C0u
#define TIBBUS_MOPC_MODE_SEL_POS      6u
#define TIBBUS_MOPC_DEVICE_RESET_MASK 0x00000004u
#define TIBBUS_MOPC_STANDBY_MODE_MASK (1u << TIBBUS_MOPC_MODE_SEL_POS)

/* IPEC */
#define TIBBUS_IPEC_NWKRQ_DELAY_MASK 0x00000100u
#define TIBBUS_IPEC_NWKRQ_DELAY_POS  8u
#define TIBBUS_IPEC_DBUS_EN_MASK     0x00000004u
#define TIBBUS_IPEC_DBUS_EN_POS      2u
#define TIBBUS_IPEC_MCAN_EN_MASK     0x00000002u
#define TIBBUS_IPEC_MCAN_EN_POS      1u
#define TIBBUS_IPEC_CCE_MASK         0x00000001u
#define TIBBUS_IPEC_DBUS2CAN_MASK    0x00000200u
#define TIBBUS_IPEC_DBUS2CAN_POS     9u
#define TIBBUS_IPEC_CAN_BIAS_MASK    0x00000800u
#define TIBBUS_IPEC_CAN_BIAS_POS     11u

/* DBUS CCCR */
#define TIBBUS_DBUS_CCCR_CCE_MASK  0x00000002u
#define TIBBUS_DBUS_CCCR_INIT_MASK 0x00000001u

/* DBUS DBR */
#define TIBBUS_DBUS_DBR_DBR_MASK   0x00000007u
#define TIBBUS_DBUS_DBR_CLKIN_MASK 0x00000018u
#define TIBBUS_DBUS_DBR_CLKIN_POS  3u

/* Baudrate values */
#define TIBBUS_DBUS_BAUD_9600   0u
#define TIBBUS_DBUS_BAUD_19200  1u
#define TIBBUS_DBUS_BAUD_38400  2u
#define TIBBUS_DBUS_BAUD_57600  3u
#define TIBBUS_DBUS_BAUD_125K   4u
#define TIBBUS_DBUS_BAUD_250K   5u
#define TIBBUS_DBUS_BAUD_500K   6u
#define TIBBUS_DBUS_BAUD_1M     7u

/* DBUS SIDFC */
#define TIBBUS_DBUS_SIDFC_PID_MASK 0x000000F0u
#define TIBBUS_DBUS_SIDFC_PID_POS  4u
#define TIBBUS_DBUS_SIDFC_SID_MASK 0x0000000Fu

/* DBUS DPC */
#define TIBBUS_DBUS_DPC_ADV_PWR_MGMT_MASK 0x00010000u
#define TIBBUS_DBUS_DPC_ADV_PWR_MGMT_POS  16u
#define TIBBUS_DBUS_DPC_BVD_WAIT_EN_MASK  0x01000000u
#define TIBBUS_DBUS_DPC_BVD_WAIT_EN_POS   24u
#define TIBBUS_DBUS_DPC_BVD_THLD_MASK     0x02000000u
#define TIBBUS_DBUS_DPC_BVD_THLD_POS      25u
#define TIBBUS_DBUS_DPC_BVD_TO_NWKRQ_MASK 0x04000000u
#define TIBBUS_DBUS_DPC_BVD_TO_NWKRQ_POS  26u

/* DBUS BCC */
#define TIBBUS_DBUS_BCC_RXFIFO_CLR_MASK 0x00000004u
#define TIBBUS_DBUS_BCC_HARD_RESET_MASK 0x00000200u

/* DBUS BSC0 */
#define TIBBUS_DBUS_BSC0_RX_BUF_SIZE_POS 16u

/* IF */
#define TIBBUS_IF_ECCERR_INT_MASK  0x00000800u
#define TIBBUS_IF_PWRON_MASK       0x00100000u
#define TIBBUS_IF_UVCC_MASK        0x00020000u
#define TIBBUS_IF_DBUS_CAN_MASK    0x00000002u

/* IE (Interrupt Enable) */
#define TIBBUS_DBUS_IE_ALL_BIT_MASK     0x0007FFFFu
#define TIBBUS_DBUS_IE_DBUSSLNT_EN_MASK 0x00000100u

/* SPI CRC */
#define TIBBUS_SPI_CRC_CFG_EN_MASK 0x00000001u

/* EEPP Register Format Masks (matching tibbus_cfg bit positions) */
#define TIBBUS_EEP_DBR_MASK           0xE0000000u
#define TIBBUS_EEP_CLKIN_MASK         0x18000000u
#define TIBBUS_EEP_BVD_WAIT_EN_MASK   0x04000000u
#define TIBBUS_EEP_BVD_THLD_MASK      0x02000000u
#define TIBBUS_EEP_ADV_PWR_MGMT_MASK  0x01000000u
#define TIBBUS_EEP_NODE_ID_MASK       0x00F00000u
#define TIBBUS_EEP_SUBNODE_ID_MASK    0x000F0000u
#define TIBBUS_EEP_GP_MEM_MASK        0x0000FE00u
#define TIBBUS_EEP_DBUS2CAN_MASK      0x00000100u
#define TIBBUS_EEP_CAN_DR_MASK        0x000000E0u
#define TIBBUS_EEP_FD_DR_MASK         0x00000010u
#define TIBBUS_EEP_CAN_BIAS_MASK      0x00000008u
#define TIBBUS_EEP_DBUS_EN_MASK       0x00000004u
#define TIBBUS_EEP_MCAN_EN_MASK       0x00000002u
#define TIBBUS_EEP_BVD_TO_NWKRQ_MASK  0x00000001u

/* Combined EEPP masks for configuration groups */
#define TIBBUS_EEP_BITS_REG_IPEC_MASK \
	(TIBBUS_EEP_MCAN_EN_MASK | TIBBUS_EEP_DBUS_EN_MASK | TIBBUS_EEP_DBUS2CAN_MASK | \
	 TIBBUS_EEP_CAN_BIAS_MASK)
#define TIBBUS_EEP_BITS_REG_CAN_MASK  (TIBBUS_EEP_CAN_DR_MASK | TIBBUS_EEP_FD_DR_MASK)
#define TIBBUS_EEP_BITS_REG_DBUS_MASK (~(TIBBUS_EEP_BITS_REG_IPEC_MASK | TIBBUS_EEP_BITS_REG_CAN_MASK))
#define TIBBUS_EEP_BITS_REG_DBUS_DPC_MASK \
	(TIBBUS_EEP_ADV_PWR_MGMT_MASK | TIBBUS_EEP_BVD_WAIT_EN_MASK | \
	 TIBBUS_EEP_BVD_THLD_MASK | TIBBUS_EEP_BVD_TO_NWKRQ_MASK)
#define TIBBUS_EEP_BITS_REG_DBUS_SIDFC_MASK (TIBBUS_EEP_SUBNODE_ID_MASK | TIBBUS_EEP_NODE_ID_MASK)
#define TIBBUS_EEP_BITS_REG_DBUS_DBR_MASK   (TIBBUS_EEP_DBR_MASK | TIBBUS_EEP_CLKIN_MASK)


#define TIBBUSDRV_RESET_TIME_US           (700u)
#define TIBBUSDRV_DBUS_TXS_FIFO_SIZE      (64u)
#define TIBBUSDRV_DBUS_TX_FIFO_SIZE       (512u)	// TODO Device Tree
#define TIBBUSDRV_DBUS_RX_FIFO_SIZE       (512u)	// TODO Device Tree
#define TIBBUSDRV_DBUS_BASE_RAM_ADDR      (0u)
#define TIBBUS_REVISION_WITH_ALL_FEATURES 0x03000200u

typedef struct {
	union {
		uint32_t word;
		struct {
			uint32_t BVD_TO_NWKRQ: 1;
			uint32_t MCAN_EN: 1;
			uint32_t DBUS_EN: 1;
			uint32_t CAN_BIAS: 1;
			uint32_t FD_DR: 1;
			uint32_t CAN_DR: 3;
			uint32_t DBUS2CAN: 1;
			uint32_t GP_MEM: 7;
			uint32_t SUBNODE_ID: 4;
			uint32_t NODE_ID: 4;
			uint32_t ADV_PWR_MGMT: 1;
			uint32_t BVD_THLD: 1;
			uint32_t BVD_WAIT_EN: 1;
			uint32_t CLKIN: 2;
			uint32_t reserved: 3;
		};
	};
} tibbus_cfg;	// TODO Rework and move to Device Tree

struct ti_bshbus_data {
	struct k_thread int_thread;
	struct k_sem int_sem;
	struct gpio_callback int_gpio_cb;
	const struct device *dev;

	K_KERNEL_STACK_MEMBER(int_stack, CONFIG_BSHBUS_TIDBUS_THREAD_STACK_SIZE);
};

struct ti_bshbus_config {
	struct spi_dt_spec spi;
	struct gpio_dt_spec irq_gpio;
	uint32_t clk_freq;
};

static int tibbus_transceive(const struct device *dev, const uint8_t *tx_buf, size_t tx_len,
			     uint8_t *rx_buf, size_t rx_len)
{
	const struct ti_bshbus_config *config = dev->config;
	const struct spi_buf tx_bufs[] = {{.buf = (void *)tx_buf, .len = tx_len}};
	const struct spi_buf rx_bufs[] = {{.buf = rx_buf, .len = rx_len}};
	const struct spi_buf_set tx = {.buffers = tx_bufs, .count = 1};
	const struct spi_buf_set rx = {.buffers = rx_bufs, .count = 1};

	return spi_transceive_dt(&config->spi, &tx, &rx);
}

static void tibbus_set_spi_hdr(uint8_t *buf, uint16_t addr, uint16_t len, uint8_t cmd)
{
	buf[TIBBUS_SPI_HDR_BYTE_CMD] = cmd;
	buf[TIBBUS_SPI_HDR_BYTE_ADDR_HIGH] = (uint8_t)(addr >> 8);
	buf[TIBBUS_SPI_HDR_BYTE_ADDR_LOW] = (uint8_t)(addr & 0xFF);
	buf[TIBBUS_SPI_HDR_BYTE_DATA_LEN] = (uint8_t)(len / 4); /* Length in words */
}

static int tibbus_write_reg(const struct device *dev, uint16_t addr, uint32_t val)
{
	uint8_t buf[TIBBUS_SPI_HDR_SIZE + TIBBUS_REG_SIZE];
	uint16_t len = TIBBUS_SPI_HDR_SIZE + TIBBUS_REG_SIZE;

	tibbus_set_spi_hdr(buf, addr, TIBBUS_REG_SIZE, TIBBUS_WRITE_L);
	sys_put_be32(val, &buf[TIBBUS_SPI_HDR_SIZE]);

	return tibbus_transceive(dev, buf, len, NULL, 0);
}

static int tibbus_read_reg(const struct device *dev, uint16_t addr, uint32_t *val)
{
	/* Must send 8 bytes (header + dummy) to clock out the response */
	uint8_t tx_buf[TIBBUS_SPI_HDR_SIZE + TIBBUS_REG_SIZE] = {0};
	uint8_t rx_buf[TIBBUS_SPI_HDR_SIZE + TIBBUS_REG_SIZE] = {0};
	uint16_t len = TIBBUS_SPI_HDR_SIZE + TIBBUS_REG_SIZE;

	tibbus_set_spi_hdr(tx_buf, addr, TIBBUS_REG_SIZE, TIBBUS_READ_L);

	int ret = tibbus_transceive(dev, tx_buf, len, rx_buf, len);
	if (ret) {
		*val = sys_get_be32(&rx_buf[TIBBUS_SPI_HDR_SIZE]);
	}

	return ret;
}

static int tibbus_write_reg_ipec(const struct device *dev, uint32_t bit_val, uint32_t bit_pos,
				 uint32_t bit_mask)
{
	uint32_t reg_val;
	int ret;

	ret = tibbus_read_reg(dev, TIBBUS_IPEC_ADDR, &reg_val);
	if (ret) {
		return ret;
	}

	if (((reg_val & bit_mask) >> bit_pos) == bit_val) {
		return 0;
	}

	reg_val |= TIBBUS_IPEC_CCE_MASK;
	ret = tibbus_write_reg(dev, TIBBUS_IPEC_ADDR, reg_val);
	if (ret) {
		return ret;
	}

	reg_val &= ~bit_mask;
	reg_val |= (bit_val << bit_pos);
	reg_val &= ~TIBBUS_IPEC_CCE_MASK;

	return tibbus_write_reg(dev, TIBBUS_IPEC_ADDR, reg_val);
}

static int tibbus_set_mode_standby(const struct device *dev)
{
	/* Precalculated CRC value for the SPI frame which sets device to STANDBY power mode */
	const uint32_t SPI_CRC_FOR_SET_STANDBY_CMD = 0xE6EBu;
	uint8_t tx_buf[TIBBUS_SPI_HDR_SIZE + TIBBUS_REG_SIZE * 2];
	uint32_t reg_val;
	uint32_t chip_mode;
	int ret;

	tibbus_set_spi_hdr(tx_buf, TIBBUS_MOPC_ADDR, TIBBUS_REG_SIZE, TIBBUS_WRITE_L);
	sys_put_be32(TIBBUS_MOPC_STANDBY_MODE_MASK, &tx_buf[TIBBUS_SPI_HDR_SIZE]);
	sys_put_be32(SPI_CRC_FOR_SET_STANDBY_CMD, &tx_buf[TIBBUS_SPI_HDR_SIZE + TIBBUS_REG_SIZE]);

	/*
	 * Send the command twice - once without CRC and once with CRC.
	 * This handles the case where the chip might have SPI-CRC enabled or disabled.
	 */

	/* Send without CRC */
	ret = tibbus_transceive(dev, tx_buf, TIBBUS_SPI_HDR_SIZE + TIBBUS_REG_SIZE, NULL, 0);
	if (ret != 0) {
		LOG_WARN("STANDBY command (no CRC) failed: %d", ret);
		return ret;
	}

	/* Send with CRC */
	ret = tibbus_transceive(dev, tx_buf, TIBBUS_SPI_HDR_SIZE + TIBBUS_REG_SIZE * 2, NULL, 0);
	if (ret != 0) {
		LOG_ERR("STANDBY command (with CRC) failed: %d", ret);
		return ret;
	}

	/* Small delay to let the chip process the command */
	k_busy_wait(100);

	/* Verify the chip is in STANDBY mode */
	ret = tibbus_read_reg(dev, TIBBUS_MOPC_ADDR, &reg_val);
	if (!ret) {
		chip_mode = (reg_val & TIBBUS_MOPC_MODE_SEL_MASK) >> TIBBUS_MOPC_MODE_SEL_POS;
		if (chip_mode != 1u) { /* 1u is STANDBY */
			LOG_ERR("Chip not in STANDBY mode: mode=%u", chip_mode);
			return -EIO;
		}
	}
	else {
		LOG_ERR("Failed to read MOPC for verification: %d", ret);
		return ret;
	}

	return 0;
}

static int tibbus_disable_spi_crc(const struct device *dev)
{
	/* Precalculated CRC value for the SPI frame which disables SPI CRC */
	const uint32_t SPI_CRC_FOR_DISABLE_CMD = 0xF20Au;
	uint8_t tx_buf[TIBBUS_SPI_HDR_SIZE + TIBBUS_REG_SIZE * 2];
	uint32_t reg_val;
	int ret;

	tibbus_set_spi_hdr(tx_buf, TIBBUS_SPI_CRC_CFG_ADDR, TIBBUS_REG_SIZE, TIBBUS_WRITE_L);
	sys_put_be32(0u, &tx_buf[TIBBUS_SPI_HDR_SIZE]);
	sys_put_be32(SPI_CRC_FOR_DISABLE_CMD, &tx_buf[TIBBUS_SPI_HDR_SIZE + TIBBUS_REG_SIZE]);

	/*
	 * Send the command twice - once without CRC and once with CRC.
	 * This handles the case where the chip might have SPI-CRC enabled.
	 */

	/* Send without CRC */
	ret = tibbus_transceive(dev, tx_buf, TIBBUS_SPI_HDR_SIZE + TIBBUS_REG_SIZE, NULL, 0);
	if (ret) {
		LOG_WARN("Disable SPI CRC (no CRC) failed: %d", ret);
		return ret;
	}

	/* Send with CRC */
	ret = tibbus_transceive(dev, tx_buf, TIBBUS_SPI_HDR_SIZE + TIBBUS_REG_SIZE * 2, NULL, 0);
	if (ret) {
		LOG_ERR("Disable SPI CRC (with CRC) failed: %d", ret);
		return ret;
	}

	/* Verify CRC was disabled by reading the register */
	ret = tibbus_read_reg(dev, TIBBUS_SPI_CRC_CFG_ADDR, &reg_val);
	if (ret) {
		LOG_ERR("Failed to read SPI_CRC_CFG: %d", ret);
		return ret;
	}

	if ((reg_val & TIBBUS_SPI_CRC_CFG_EN_MASK) != 0u) {
		LOG_ERR("Failed to disable SPI CRC: reg=0x%08x", reg_val);
		return -EIO;
	}

	return 0;
}

static int tibbus_set_mode_normal(const struct device *dev)
{
	uint32_t reg_val;
	uint32_t chip_mode;
	int ret;

	ret = tibbus_read_reg(dev, TIBBUS_MOPC_ADDR, &reg_val);
	if (ret) {
		return ret;
	}

	chip_mode = (reg_val & TIBBUS_MOPC_MODE_SEL_MASK) >> TIBBUS_MOPC_MODE_SEL_POS;
	if (chip_mode != 2u) {         /* 2u is NORMAL */
		if (chip_mode != 1u) { /* Must be in STANDBY before NORMAL */
			tibbus_set_mode_standby(dev);
		}

		reg_val &= ~TIBBUS_MOPC_MODE_SEL_MASK;
		reg_val |= (2u << TIBBUS_MOPC_MODE_SEL_POS);
		ret = tibbus_write_reg(dev, TIBBUS_MOPC_ADDR, reg_val);
		if (ret != 0) {
			return ret;
		}
	}
	return 0;
}

static int tibbus_reset_hard(const struct device *dev)
{
	tibbus_write_reg(dev, TIBBUS_DBUS_BCC_ADDR, TIBBUS_DBUS_BCC_HARD_RESET_MASK);
	k_busy_wait(TIBBUSDRV_RESET_TIME_US);

	return 0;
}

static int tibbus_reset_full(const struct device *dev)
{
	int ret;

	ret = tibbus_set_mode_standby(dev);
	if (ret) {
		return ret;
	}

	ret = tibbus_write_reg(dev, TIBBUS_MOPC_ADDR, TIBBUS_MOPC_DEVICE_RESET_MASK);
	if (ret) {
		return ret;
	}

	k_busy_wait(TIBBUSDRV_RESET_TIME_US);

	return 0;
}

static bool tibbus_is_all_feature_revision(const struct device *dev)
{
	uint32_t revision;

	if (tibbus_read_reg(dev, TIBBUS_REVISION_ADDR, &revision)) {
		return false;
	}
	return (revision >= TIBBUS_REVISION_WITH_ALL_FEATURES);
}

static int tibbus_do_reset(const struct device *dev)
{
	uint32_t reg_val;
	int ret;

	ret = tibbus_read_reg(dev, TIBBUS_IF_ADDR, &reg_val);
	if (ret != 0) {
		return ret;
	}

	/* Check if an Internal EEPROM CRC error is detected */
	if ((reg_val & TIBBUS_IF_ECCERR_INT_MASK) != 0u) {
		tibbus_reset_hard(dev);
		LOG_ERR("Internal EEPROM CRC error detected!");
		return -EIO;
	}

	/* For chips with all features, always use hard reset */
	if (tibbus_is_all_feature_revision(dev)) {
		return tibbus_reset_hard(dev);
	}

	/* For older revisions: check power-on reset and advanced power management */
	bool is_pwr_on_reset = ((reg_val & TIBBUS_IF_PWRON_MASK) != 0u);

	ret = tibbus_read_reg(dev, TIBBUS_DBUS_DPC_ADDR, &reg_val);
	if (ret != 0) {
		return ret;
	}

	bool is_adv_pwr_mgmt = ((reg_val & TIBBUS_DBUS_DPC_ADV_PWR_MGMT_MASK) != 0u);

	if (is_pwr_on_reset && !is_adv_pwr_mgmt) {
		/*
		 * Full reset as a workaround for the issue of the 1p0 DBusCAN chip
		 * (not signalling interrupts when booting in simple power management)
		 */
		LOG_DBG("Performing full reset (workaround for 1p0 chip)");
		return tibbus_reset_full(dev);
	}

	return tibbus_reset_hard(dev);
}

static int tibbus_enable_and_clear_irq_flags(const struct device *dev)
{
	uint32_t reg_val;
	int ret;

	ret = tibbus_read_reg(dev, TIBBUS_IE_ADDR, &reg_val);
	if (ret) {
		LOG_ERR("Failed to read IE register: %d", ret);
		return ret;
	}

	reg_val |= TIBBUS_IF_DBUS_CAN_MASK;
	reg_val &= ~TIBBUS_IF_UVCC_MASK;
	ret = tibbus_write_reg(dev, TIBBUS_IE_ADDR, reg_val);
	if (ret) {
		LOG_ERR("Failed to write IE register: %d", ret);
		return ret;
	}

	/* Enable all DBus interrupts except D-Bus silent flag */
	ret = tibbus_write_reg(dev, TIBBUS_DBUS_IE_ADDR,
			       TIBBUS_DBUS_IE_ALL_BIT_MASK & ~TIBBUS_DBUS_IE_DBUSSLNT_EN_MASK);
	if (ret) {
		LOG_ERR("Failed to write DBUS IE register: %d", ret);
		return ret;
	}

	/* Clear D-Bus interrupt flags */
	ret = tibbus_read_reg(dev, TIBBUS_DBUS_IR_ADDR, &reg_val);
	if (!ret) {
		ret = tibbus_write_reg(dev, TIBBUS_DBUS_IR_ADDR, reg_val);
		if (ret) {
			LOG_ERR("Failed to write DBUS IR register: %d", ret);
			return ret;
		}
	}
	else {
		LOG_ERR("Failed to read DBUS IR register: %d", ret);
		return ret;
	}

	/* Clear global interrupt flags */
	ret = tibbus_read_reg(dev, TIBBUS_IF_ADDR, &reg_val);
	if (!ret) {
		ret = tibbus_write_reg(dev, TIBBUS_IF_ADDR, reg_val);
		if (ret) {
			LOG_ERR("Failed to write IF register: %d", ret);
			return ret;
		}
	}
	else {
		LOG_ERR("Failed to read IF register: %d", ret);
		return ret;
	}

	/* Clear SPI status flags */
	ret = tibbus_read_reg(dev, TIBBUS_STATUS_ADDR, &reg_val);
	if (!ret) {
		ret = tibbus_write_reg(dev, TIBBUS_STATUS_ADDR, reg_val);
		if (ret) {
			return ret;
		}
	}
	else {
		LOG_ERR("Failed to read STATUS register: %d", ret);
		return ret;
	}

	return 0;
}

#if LOG_LEVEL >= LOG_LEVEL_DBG
static void tibbus_get_dev_info(const struct device *dev)
{
	uint32_t reg_val;
	int ret;

	LOG_DBG("Device info:");

	ret = tibbus_read_reg(dev, TIBBUS_DEVICE_ID0_ADDR, &reg_val);
	if (!ret) {
		LOG_DBG("\tid0: %x", reg_val);
	}
	else {
		LOG_ERR("Read device ID0 failed: %d", ret);
	}

	ret = tibbus_read_reg(dev, TIBBUS_DEVICE_ID1_ADDR, &reg_val);
	if (!ret) {
		LOG_DBG("\tid1: %x", reg_val);
	}
	else {
		LOG_ERR("Read device ID1 failed: %d", ret);
	}

	ret = tibbus_read_reg(dev, TIBBUS_REVISION_ADDR, &reg_val);
	if (!ret) {
		LOG_DBG("\trev: %x", reg_val);
	}
	else {
		LOG_ERR("Read device revision failed: %d", ret);
	}
}
#endif /* LOG_LEVEL >= LOG_LEVEL_DBG */

static int tibbus_enable_cfg_dbus(const struct device *dev)
{
	uint32_t reg_val;
	int ret;

	ret = tibbus_read_reg(dev, TIBBUS_DBUS_CCCR_ADDR, &reg_val);
	if (ret) {
		return ret;
	}

	if ((reg_val & TIBBUS_DBUS_CCCR_INIT_MASK) != TIBBUS_DBUS_CCCR_INIT_MASK) {
		reg_val |= TIBBUS_DBUS_CCCR_INIT_MASK;
		ret = tibbus_write_reg(dev, TIBBUS_DBUS_CCCR_ADDR, reg_val);
		if (ret) {
			return ret;
		}
	}

	reg_val |= TIBBUS_DBUS_CCCR_CCE_MASK;

	return tibbus_write_reg(dev, TIBBUS_DBUS_CCCR_ADDR, reg_val);
}

static int tibbus_disable_cfg_dbus(const struct device *dev)
{
	uint32_t reg_val;
	int ret;

	ret = tibbus_read_reg(dev, TIBBUS_DBUS_CCCR_ADDR, &reg_val);
	if (ret) {
		return ret;
	}

	reg_val &= ~(TIBBUS_DBUS_CCCR_CCE_MASK | TIBBUS_DBUS_CCCR_INIT_MASK);

	return tibbus_write_reg(dev, TIBBUS_DBUS_CCCR_ADDR, reg_val);
}

static tibbus_cfg tibbus_get_config(void)
{
	tibbus_cfg cfg = {{0}};

	cfg.CLKIN = 0; /* 20MHz */
	cfg.GP_MEM = 0u;
	cfg.DBUS_EN = 1u;
	cfg.ADV_PWR_MGMT = 0u;
	cfg.NODE_ID = CONFIG_BSHBUS_NODE_ADDRESS >> 4;
	cfg.SUBNODE_ID = CONFIG_BSHBUS_NODE_ADDRESS & 0x0F;
	cfg.BVD_WAIT_EN = 0u;
	cfg.BVD_THLD = 0u;
	cfg.BVD_TO_NWKRQ = 0u;
	cfg.DBUS2CAN = 0u;
	cfg.MCAN_EN = 0u;
	cfg.CAN_BIAS = 0u;
	cfg.FD_DR = 0u;
	cfg.CAN_DR = 0u;
	return cfg;
}

static int tibbus_set_node_address(const struct device *dev, uint8_t node_address)
 {
	 uint32_t reg_val;
	 uint8_t node_id = node_address >> 4;
	 uint8_t subnode_id = node_address & 0x0F;
	 int ret;

	 ret = tibbus_read_reg(dev, TIBBUS_DBUS_SIDFC_ADDR, &reg_val);
	 if (ret) {
		 LOG_ERR("Failed to read SIDFC: %d", ret);
		 return ret;
	 }

	 reg_val &= ~TIBBUS_DBUS_SIDFC_SID_MASK;
	 reg_val |= ((uint32_t)subnode_id);

	 reg_val &= ~TIBBUS_DBUS_SIDFC_PID_MASK;
	 reg_val |= ((uint32_t)node_id << TIBBUS_DBUS_SIDFC_PID_POS);

	 ret = tibbus_write_reg(dev, TIBBUS_DBUS_SIDFC_ADDR, reg_val);
	 if (ret) {
		 LOG_ERR("Failed to write SIDFC: %d", ret);
		 return ret;
	 }

	 LOG_DBG("Node address %02x activated", node_address);

	 return 0;
 }

 /**
 * @brief Convert baudrate to DBR register value.
 *
 * @param baudrate Baudrate
 * @return DBR register value (0-7)
 */
static uint8_t tibbus_baudrate_to_dbr(uint32_t baudrate)
{
	switch (baudrate) {
	case 9600:
		return TIBBUS_DBUS_BAUD_9600;
	case 19200:
		return TIBBUS_DBUS_BAUD_19200;
	case 38400:
		return TIBBUS_DBUS_BAUD_38400;
	case 57600:
		return TIBBUS_DBUS_BAUD_57600;
	case 125000:
		return TIBBUS_DBUS_BAUD_125K;
	case 250000:
		return TIBBUS_DBUS_BAUD_250K;
	case 500000:
		return TIBBUS_DBUS_BAUD_500K;
	case 1000000:
		return TIBBUS_DBUS_BAUD_1M;
	default:
		LOG_WRN("Baudrate %u not supported, using default 9600", baudrate);
		return TIBBUS_DBUS_BAUD_9600;
	}
}

static int tibbus_configure_dbus(const struct device *dev, tibbus_cfg cfg) // TODO Rework with Device Tree Settings
{
	uint32_t reg_val;
	uint32_t bit_mask;
	tibbus_cfg curr_cfg;
	int ret;

	/* Check if current configuration matches */
	ret = tibbus_read_reg(dev, TIBBUS_EEPP_ADDR, &curr_cfg.word);
	if (!ret) {
		if (curr_cfg.word == cfg.word) {
			return 0;
		}
	}
	else {
		return ret;
	}

	/* Configure general purpose memory bits */
	if (curr_cfg.GP_MEM != cfg.GP_MEM) {
		reg_val = curr_cfg.word;
		reg_val &= ~TIBBUS_EEP_GP_MEM_MASK;
		reg_val |= (cfg.word & TIBBUS_EEP_GP_MEM_MASK);
		ret = tibbus_write_reg(dev, TIBBUS_EEPP_ADDR, reg_val);
		if (ret) {
			return ret;
		}
	}

	/* Configure D-Bus register bits */
	if ((curr_cfg.word & TIBBUS_EEP_BITS_REG_DBUS_MASK) !=
	    (cfg.word & TIBBUS_EEP_BITS_REG_DBUS_MASK)) {

		/* Enable D-Bus part of chip */
		ret = tibbus_write_reg_ipec(dev, 1, TIBBUS_IPEC_DBUS_EN_POS,
					    TIBBUS_IPEC_DBUS_EN_MASK);
		if (ret) {
			return ret;
		}

		/* Unlock configuration registers for write access */
		ret = tibbus_enable_cfg_dbus(dev);
		if (ret) {
			return ret;
		}

		/* Configure power control register bits */
		if ((curr_cfg.word & TIBBUS_EEP_BITS_REG_DBUS_DPC_MASK) !=
		    (cfg.word & TIBBUS_EEP_BITS_REG_DBUS_DPC_MASK)) {
			ret = tibbus_read_reg(dev, TIBBUS_DBUS_DPC_ADDR, &reg_val);
			if (ret != 0) {
				return ret;
			}
			reg_val &= ~TIBBUS_DBUS_DPC_ADV_PWR_MGMT_MASK;
			reg_val |= ((uint32_t)cfg.ADV_PWR_MGMT << TIBBUS_DBUS_DPC_ADV_PWR_MGMT_POS);
			reg_val &= ~TIBBUS_DBUS_DPC_BVD_WAIT_EN_MASK;
			reg_val |= ((uint32_t)cfg.BVD_WAIT_EN << TIBBUS_DBUS_DPC_BVD_WAIT_EN_POS);
			reg_val &= ~TIBBUS_DBUS_DPC_BVD_THLD_MASK;
			reg_val |= ((uint32_t)cfg.BVD_THLD << TIBBUS_DBUS_DPC_BVD_THLD_POS);
			reg_val &= ~TIBBUS_DBUS_DPC_BVD_TO_NWKRQ_MASK;
			reg_val |= ((uint32_t)cfg.BVD_TO_NWKRQ << TIBBUS_DBUS_DPC_BVD_TO_NWKRQ_POS);
			ret = tibbus_write_reg(dev, TIBBUS_DBUS_DPC_ADDR, reg_val);
			if (ret != 0) {
				return ret;
			}
		}

		ret = tibbus_set_node_address(dev, CONFIG_BSHBUS_NODE_ADDRESS);
		if (ret) {
			return ret;
		}

		/* Configure baudrate register bits */
		ret = tibbus_read_reg(dev, TIBBUS_DBUS_DBR_ADDR, &reg_val);
		if (ret) {
			return ret;
		}
		reg_val &= ~TIBBUS_DBUS_DBR_DBR_MASK;
		reg_val |= tibbus_baudrate_to_dbr(CONFIG_BSHBUS_BAUDRATE);
		reg_val &= ~TIBBUS_DBUS_DBR_CLKIN_MASK;
		reg_val |= ((uint32_t)cfg.CLKIN << TIBBUS_DBUS_DBR_CLKIN_POS);	// TODO Get from Device Tree
		ret = tibbus_write_reg(dev, TIBBUS_DBUS_DBR_ADDR, reg_val);
		if (ret) {
			return ret;
		}

		/* Lock configuration registers for write access */
		ret = tibbus_disable_cfg_dbus(dev);
		if (ret) {
			return ret;
		}
	}

	/* Configure IPEC register bits */
	if ((curr_cfg.word & TIBBUS_EEP_BITS_REG_IPEC_MASK) !=
	    (cfg.word & TIBBUS_EEP_BITS_REG_IPEC_MASK)) {
		reg_val = 0u;
		bit_mask = 0u;

		bit_mask |= TIBBUS_IPEC_MCAN_EN_MASK;
		reg_val |= ((uint32_t)cfg.MCAN_EN << TIBBUS_IPEC_MCAN_EN_POS);

		bit_mask |= TIBBUS_IPEC_DBUS_EN_MASK;
		reg_val |= ((uint32_t)cfg.DBUS_EN << TIBBUS_IPEC_DBUS_EN_POS);

		bit_mask |= TIBBUS_IPEC_DBUS2CAN_MASK;
		reg_val |= ((uint32_t)cfg.DBUS2CAN << TIBBUS_IPEC_DBUS2CAN_POS);

		bit_mask |= TIBBUS_IPEC_CAN_BIAS_MASK;
		reg_val |= ((uint32_t)cfg.CAN_BIAS << TIBBUS_IPEC_CAN_BIAS_POS);

		ret = tibbus_write_reg_ipec(dev, reg_val, 0u, bit_mask);
		if (ret) {
			return ret;
		}
	}

	/* Verify configuration by re-reading EEPP */
	ret = tibbus_read_reg(dev, TIBBUS_EEPP_ADDR, &curr_cfg.word);
	if (!ret) {
		uint32_t compare_mask = ~TIBBUS_EEP_DBR_MASK;
		compare_mask |= TIBBUS_EEP_BITS_REG_DBUS_SIDFC_MASK;
		if ((curr_cfg.word & compare_mask) != (cfg.word & compare_mask)) {
			LOG_ERR("Configuration verification failed: expected 0x%08x, got 0x%08x",
				cfg.word & compare_mask, curr_cfg.word & compare_mask);
			return -EIO;
		}
	}
	else {
		return ret;
	}

	return 0;
}

/* TODO: Needed for SPAU
static int tibbus_set_baudrate(const struct device *dev, uint32_t baudrate)	// TODO Add IOCTL
{
	uint32_t reg_val;
	uint8_t dbr_val;
	int ret;

	dbr_val = tibbus_baudrate_to_dbr(baudrate);

	ret = tibbus_read_reg(dev, TIBBUS_DBUS_DBR_ADDR, &reg_val);
	if (!ret) {
		if ((reg_val & TIBBUS_DBUS_DBR_DBR_MASK) == dbr_val) {
			LOG_DBG("Baudrate already set to %u", baudrate);
			return 0;
		}
	}
	else {
		LOG_ERR("Failed to read DBR: %d", ret);
		return ret;
	}

	ret = tibbus_enable_cfg_dbus(dev);
	if (ret) {
		return ret;
	}

	reg_val &= ~TIBBUS_DBUS_DBR_DBR_MASK;
	reg_val |= dbr_val;
	ret = tibbus_write_reg(dev, TIBBUS_DBUS_DBR_ADDR, reg_val);
	if (ret) {
		LOG_ERR("Failed to write DBR: %d", ret);
		tibbus_disable_cfg_dbus(dev);
		return ret;
	}

	ret = tibbus_disable_cfg_dbus(dev);
	if (ret) {
		LOG_ERR("Failed to lock configuration registers: %d", ret);
		return ret;
	}

	LOG_INF("Baudrate set to %u", baudrate);

	return 0;
} */

static int tibbus_configure_buffers(const struct device *dev)
{
	uint8_t buf[TIBBUS_SPI_HDR_SIZE + TIBBUS_REG_SIZE * 3];
	uint32_t val;

	/* Set base RAM address */
	tibbus_set_spi_hdr(buf, TIBBUS_DBUS_BSA_ADDR, TIBBUS_REG_SIZE * 3, TIBBUS_WRITE_L);
	val = TIBBUSDRV_DBUS_BASE_RAM_ADDR;
	sys_put_be32(val, &buf[TIBBUS_SPI_HDR_SIZE]);

	/* Set RX and TX buffer sizes */
	val = ((uint32_t)TIBBUSDRV_DBUS_RX_FIFO_SIZE << TIBBUS_DBUS_BSC0_RX_BUF_SIZE_POS) +
	      TIBBUSDRV_DBUS_TX_FIFO_SIZE;	// TODO Device Tree
	sys_put_be32(val, &buf[TIBBUS_SPI_HDR_SIZE + TIBBUS_REG_SIZE]);

	/* Set TX status buffer size */
	val = TIBBUSDRV_DBUS_TXS_FIFO_SIZE; // TODO Device Tree
	sys_put_be32(val, &buf[TIBBUS_SPI_HDR_SIZE + TIBBUS_REG_SIZE * 2]);

	return tibbus_transceive(dev, buf, sizeof(buf), NULL, 0);
}

int tibbus_start(const struct device *dev)
{
	/* TODO: Implement later */
	LOG_DBG("Start device %s", dev->name);
	return 0;
}

int tibbus_stop(const struct device *dev)
{
	/* TODO: Implement later */
	LOG_DBG("Stop device %s", dev->name);
	return 0;
}

int tibbus_add_receiver(const struct device *dev, bshbus_dbus2_rx_callback_t cb,
			void *user_data)
{
	LOG_DBG("Add receiver to %s:", dev->name);

	return 0;
}

int tibbus_remove_receiver(const struct device *dev)
{
	/* For now no use case for it */
	LOG_DBG("Remove receiver from %s:", dev->name);
	return 0;
}

static void tibbus_int_gpio_callback(const struct device *port,
				     struct gpio_callback *cb,
				     gpio_port_pins_t pins)
{
	struct ti_bshbus_data *data = CONTAINER_OF(cb, struct ti_bshbus_data, int_gpio_cb);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	/* Signal the interrupt thread */
	k_sem_give(&data->int_sem);
}

static int tibbus_init_irq_gpio(const struct device *dev)
{
	const struct ti_bshbus_config *config = dev->config;
	struct ti_bshbus_data *data = dev->data;
	int ret;

	if (!gpio_is_ready_dt(&config->irq_gpio)) {
		LOG_ERR("Interrupt GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->irq_gpio, GPIO_INPUT);
	if (ret) {
		LOG_ERR("Failed to configure interrupt GPIO: %d", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&config->irq_gpio,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (ret) {
		LOG_ERR("Failed to configure interrupt: %d", ret);
		return ret;
	}

	gpio_init_callback(&data->int_gpio_cb,
			   tibbus_int_gpio_callback,
			   BIT(config->irq_gpio.pin));

	ret = gpio_add_callback(config->irq_gpio.port, &data->int_gpio_cb);
	if (ret != 0) {
		LOG_ERR("Failed to add GPIO callback: %d", ret);
		return ret;
	}

	LOG_DBG("Interrupt GPIO configured on pin %d", config->irq_gpio.pin);

	return 0;
}

static int tibbus_handle_irq(const struct device *dev)
{
	uint32_t global_flags;
	uint32_t dbus_flags;
	int ret;

	ret = tibbus_read_reg(dev, TIBBUS_IF_ADDR, &global_flags);
	if (ret) {
		LOG_ERR("Failed to read IF: %d", ret);
		return ret;
	}

	ret = tibbus_read_reg(dev, TIBBUS_DBUS_IR_ADDR, &dbus_flags);
	if (ret) {
		LOG_ERR("Failed to read DBUS_IR: %d", ret);
		return ret;
	}

	/* Clear D-Bus interrupt flags */
	if (dbus_flags != 0) {
		ret = tibbus_write_reg(dev, TIBBUS_DBUS_IR_ADDR, dbus_flags);
		if (ret) {
			LOG_ERR("Failed to clear DBUS_IR: %d", ret);
			return ret;
		}
	}

	/* Clear global interrupt flags */
	if (global_flags != 0) {
		ret = tibbus_write_reg(dev, TIBBUS_IF_ADDR, global_flags);
		if (ret) {
			LOG_ERR("Failed to clear IF: %d", ret);
			return ret;
		}
	}

	if (dbus_flags & TIBBUS_DBUS_IR_RF0L_MASK) {
		LOG_WRN("RX FIFO message lost");
	}

	if (dbus_flags & TIBBUS_DBUS_IR_RF0F_MASK) {
		LOG_WRN("RX FIFO full");
	}

	return 0;
}

static void tibbus_int_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct device *dev = p1;
	struct ti_bshbus_data *tibbus_data = dev->data;
	const struct ti_bshbus_config *tibbus_config = dev->config;

	LOG_DBG("Thread started...");

	while (true) {
		k_sem_take(&tibbus_data->int_sem, K_FOREVER);

		/* Handle interrupt while pin is active (low) */
		while (gpio_pin_get_dt(&tibbus_config->irq_gpio) == 1) {
			tibbus_handle_irq(dev);
		}
	}
}

static int tibbus_init(const struct device *dev)
{
	const struct ti_bshbus_config *tibbus_config = dev->config;
	struct ti_bshbus_data *tibbus_data = dev->data;
	k_tid_t tid;
	int ret;
	tibbus_cfg cfg;

	LOG_DBG("Calling init...");

	k_sem_init(&tibbus_data->int_sem, 0, 1);

	if (!spi_is_ready_dt(&tibbus_config->spi)) {
		LOG_ERR("SPI bus not ready");
		return -ENODEV;
	}

	/* Store device reference for use in callback */
	tibbus_data->dev = dev;

	ret = tibbus_init_irq_gpio(dev);
	if (ret != 0) {
		return ret;
	}

	tid = k_thread_create(&tibbus_data->int_thread, tibbus_data->int_stack,
			      K_KERNEL_STACK_SIZEOF(tibbus_data->int_stack),
				  tibbus_int_thread, (void *)dev, NULL, NULL,
				  CONFIG_BSHBUS_TIDBUS_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(tid, "tibshbus");

	// TODO Move some parts to device tree
	cfg = tibbus_get_config();

	ret = tibbus_set_mode_standby(dev);
	if (ret) {
		LOG_ERR("Failed to set Standby mode: %d", ret);
		return ret;
	}

	ret = tibbus_disable_spi_crc(dev);
	if (ret) {
		LOG_ERR("Failed to disable SPI CRC: %d", ret);
		return ret;
	}

	ret = tibbus_do_reset(dev);
	if (ret) {
		return ret;
	}

#if LOG_LEVEL >= LOG_LEVEL_DBG
	tibbus_get_dev_info(dev);
#endif /* LOG_LEVEL >= LOG_LEVEL_DBG */

	ret = tibbus_configure_dbus(dev, cfg);
	if (ret) {
		LOG_ERR("Failed to configure: %d", ret);
		return ret;
	}

	/* Clear NWKRQ_DELAY bit to disable nWKRQ pin delayed de-assertion in sleep mode */
	ret = tibbus_write_reg_ipec(dev, 0, TIBBUS_IPEC_NWKRQ_DELAY_POS,
				    TIBBUS_IPEC_NWKRQ_DELAY_MASK);
	if (ret) {
		LOG_ERR("Failed to clear NWKRQ_DELAY: %d", ret);
		return ret;
	}

	/* Unlock configuration registers for write access */
	ret = tibbus_enable_cfg_dbus(dev);
	if (ret) {
		LOG_ERR("Failed to enable DBus config: %d", ret);
		return ret;
	}

	/* Clear RX FIFO */
	ret = tibbus_write_reg(dev, TIBBUS_DBUS_BCC_ADDR, TIBBUS_DBUS_BCC_RXFIFO_CLR_MASK);
	if (ret) {
		LOG_ERR("Failed to clear RX FIFO: %d", ret);
		return ret;
	}

	ret = tibbus_enable_and_clear_irq_flags(dev);
	if (ret) {
		LOG_ERR("Failed to enable/clear IRQ flags: %d", ret);
		return ret;
	}

	ret = tibbus_configure_buffers(dev);
	if (ret) {
		LOG_ERR("Failed to configure DBus buffers: %d", ret);
		return ret;
	}

	/* Lock configuration registers for write access */
	ret = tibbus_disable_cfg_dbus(dev);
	if (ret != 0) {
		LOG_ERR("Failed to disable DBus config: %d", ret);
		return ret;
	}

	ret = tibbus_set_mode_normal(dev);
	if (ret != 0) {
		LOG_ERR("Failed to set Normal mode: %d", ret);
		return ret;
	}

	LOG_DBG("...init finished successfully");

	return 0;
}

static DEVICE_API(bshbus, tibbus_driver_api) = {
	.start = tibbus_start,
	.stop = tibbus_stop,
	.dbus2_send = tibbus_send,
	.dbus2_add_receiver = tibbus_add_receiver,
	.dbus2_remove_receiver = tibbus_remove_receiver,
};

#define TIBBUS_INIT(inst) \
	static const struct ti_bshbus_config tibbus_config_##inst = { \
		.spi = SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8), 0), \
		.irq_gpio = GPIO_DT_SPEC_INST_GET(inst, irq_gpios), \
	}; \
	\
	static struct ti_bshbus_data tibbus_data_##inst; \
	\
	BSHBUS_DEVICE_DT_INST_DEFINE(inst, tibbus_init, NULL, &tibbus_data_##inst, \
				     &tibbus_config_##inst, POST_KERNEL, \
				     CONFIG_BSHBUS_INIT_PRIORITY, &tibbus_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TIBBUS_INIT)
