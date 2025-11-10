/*
 * Copyright (c) 2025 Wolfgang Birkner
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/bshbus.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>

#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/can/can_mcan.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(ti_bshbus, CONFIG_BSHBUS_LOG_LEVEL);

#define DT_DRV_COMPAT ti_bshbus

struct ti_bshbus_data {
	struct k_thread int_thread;
	struct k_sem int_sem;

    void *user_data;
    bshbus_dbus2_tx_callback_t cb;

	K_KERNEL_STACK_MEMBER(int_stack, CONFIG_BSHBUS_TIDBUS_THREAD_STACK_SIZE);
};

struct ti_bshbus_config {
	struct spi_dt_spec spi;
	uint32_t clk_freq;
};

int tibbus_start(const struct device *dev)
{
//TODO
    LOG_DBG("Start device %s", dev->name);

    return 0;
}

int tibbus_stop(const struct device *dev)
{
    LOG_DBG("Stop device %s", dev->name);

    return 0;
}

int tibbus_send(const struct device *dev, const struct bshbus_frame_dbus2_tx *tx,
        bshbus_dbus2_tx_callback_t cb, void *user_data)
{
    if (!cb) {
        LOG_ERR("Callback is NULL");
        return -EINVAL;
    }

    LOG_DBG("Send frame on %s:", dev->name);
    LOG_DBG("\tunqiue_id: %d", tx->unique_id);
    LOG_DBG("\tdest_addr: %02x", tx->dest_addr);
    LOG_DBG("\tmsg_id: %04x", tx->msg_id);
    LOG_DBG("\tdlen: %d", tx->dlen);

    return 0;
}

int tibbus_add_receiver(const struct device *dev)
{
    LOG_DBG("Add receiver to %s:", dev->name);

    return 0;
}

int tibbus_remove_receiver(const struct device *dev)
{
    LOG_DBG("Remove receiver from %s:", dev->name);

    return 0;
}

static void tibbus_int_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

    const struct device *dev = p1;
	struct ti_bshbus_data *tibbus_data = dev->data;

    LOG_DBG("Thread started...");

	while (true) {
		k_sem_take(&tibbus_data->int_sem, K_FOREVER);
	}
}

static int tibbus_init(const struct device *dev)
{
    const struct ti_bshbus_config *tibbus_config = dev->config;
	struct ti_bshbus_data *tibbus_data = dev->data;
    k_tid_t tid;

    LOG_DBG("Calling init...");

    /* Initialize int_sem to 1 to ensure any pending IRQ is serviced */
	k_sem_init(&tibbus_data->int_sem, 1, 1);

    if (!spi_is_ready_dt(&tibbus_config->spi)) {
		LOG_ERR("SPI bus not ready");
		return -ENODEV;
	}

	tid = k_thread_create(&tibbus_data->int_thread, tibbus_data->int_stack,
			      K_KERNEL_STACK_SIZEOF(tibbus_data->int_stack),
			      tibbus_int_thread, (void *)dev, NULL, NULL,
			      CONFIG_BSHBUS_TIDBUS_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(tid, "tibshbus");

    LOG_DBG("...init finished");

    return 0;
}

static DEVICE_API(bshbus, tibbus_driver_api) = {
	.start = tibbus_start,
	.stop = tibbus_stop,
	.dbus2_send = tibbus_send,
    .dbus2_add_receiver = tibbus_add_receiver,
    .dbus2_remove_receiver = tibbus_remove_receiver,
};

#define TIBBUS_INIT(inst)                                                       \
    static const struct ti_bshbus_config tibbus_config_##inst = {                  \
        .spi = SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8), 0),                  \
        .clk_freq = DT_INST_PROP(inst, clock_frequency)                         \
    };                                                                          \
                                                                                \
    static struct ti_bshbus_data tibbus_data_##inst;                               \
                                                                                \
    BSHBUS_DEVICE_DT_INST_DEFINE(inst, tibbus_init, NULL, &tibbus_data_##inst,  \
                  &tibbus_config_##inst, POST_KERNEL,                           \
                  CONFIG_BSHBUS_INIT_PRIORITY, &tibbus_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TIBBUS_INIT)
