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

LOG_MODULE_REGISTER(dummy_bshbus, CONFIG_BSHBUS_LOG_LEVEL);

#define DT_DRV_COMPAT dummy_bshbus

struct dummy_bshbus_tx_data {
    void *user_data;
    bshbus_dbus2_tx_callback_t cb;
    struct k_thread thread;
    struct k_sem sem;
    K_KERNEL_STACK_MEMBER(stack, CONFIG_BSHBUS_DUMMY_THREAD_STACK_SIZE);
};

struct dummy_bshbus_rx_data {
    void *user_data;
    bshbus_dbus2_rx_callback_t cb;
    struct k_thread thread;
    K_KERNEL_STACK_MEMBER(stack, CONFIG_BSHBUS_DUMMY_THREAD_STACK_SIZE);
};

struct dummy_bshbus_data {
    struct dummy_bshbus_tx_data tx;
    struct dummy_bshbus_rx_data rx;
};

struct dummy_bshbus_config {
	struct spi_dt_spec spi;
	uint32_t clk_freq;
};

int dummybbus_start(const struct device *dev)
{
    LOG_DBG("Start device %s", dev->name);

    return 0;
}

int dummybbus_stop(const struct device *dev)
{
    LOG_DBG("Stop device %s", dev->name);

    return 0;
}

int dummybbus_send(const struct device *dev, const struct bshbus_frame_dbus2_tx *tx,
        bshbus_dbus2_tx_callback_t cb, void *user_data)
{
    struct dummy_bshbus_data *dummybbus_data = dev->data;

    if (!cb) {
        LOG_ERR("Callback is NULL");
        return -EINVAL;
    }

    LOG_DBG("Send frame on %s:", dev->name);
    LOG_DBG("\tunqiue_id: %d", tx->unique_id);
    LOG_DBG("\tdest_addr: %02x", tx->dest_addr);
    LOG_DBG("\tmsg_id: %04x", tx->msg_id);
    LOG_DBG("\tdlen: %d", tx->dlen);

    dummybbus_data->tx.user_data = user_data;
    dummybbus_data->tx.cb = cb;

    k_sem_give(&dummybbus_data->tx.sem);

    return 0;
}

int dummybbus_add_receiver(const struct device *dev,
            bshbus_dbus2_rx_callback_t cb, void *user_data)
{
    struct dummy_bshbus_data *dummybbus_data = dev->data;

    LOG_DBG("Add receiver to %s:", dev->name);

    if (!dummybbus_data->rx.cb) {
        dummybbus_data->rx.user_data = user_data;
        dummybbus_data->rx.cb = cb;
    }

    return 0;
}

int dummybbus_remove_receiver(const struct device *dev)
{
    LOG_DBG("Remove receiver from %s:", dev->name);

    return 0;
}

static void dummybbus_tx_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

    const struct device *dev = p1;
	struct dummy_bshbus_data *dummybbus_data = dev->data;

    LOG_DBG("TX Thread started...");

	while (true) {
		k_sem_take(&dummybbus_data->tx.sem, K_FOREVER);

        LOG_DBG("%s TX woken up", dev->name);

        if (dummybbus_data->tx.cb) {
            dummybbus_data->tx.cb(dev, BSHBUS_FRAME_STATUS_VALID, dummybbus_data->tx.user_data);
        }
	}
}

static void dummybbus_rx_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

    const struct device *dev = p1;
	struct dummy_bshbus_data *dummybbus_data = dev->data;
    struct bshbus_frame frame;
    uint8_t data = 0;

    LOG_DBG("RX thread started...");

	while (true) {
		k_sleep(K_SECONDS(5));

        LOG_DBG("%s RX message received", dev->name);

        bshbus_prepare_frame_dbus2_rx(&frame, 0xC0, 0xFCBF, sizeof(data), &data);

        if (dummybbus_data->rx.cb) {
            dummybbus_data->rx.cb(dev, &frame, dummybbus_data->rx.user_data);
        }

        if (data < UINT8_MAX) {
            data++;
        } else {
            data = 0;
        }
	}
}

static int dummybbus_init(const struct device *dev)
{
	struct dummy_bshbus_data *dummybbus_data = dev->data;
    k_tid_t tid;

    LOG_DBG("Calling init...");

    /* Initialize int_sem to 1 to ensure any pending IRQ is serviced */
	k_sem_init(&dummybbus_data->tx.sem, 1, 1);

	tid = k_thread_create(&dummybbus_data->tx.thread, dummybbus_data->tx.stack,
			      K_KERNEL_STACK_SIZEOF(dummybbus_data->tx.stack),
			      dummybbus_tx_thread, (void *)dev, NULL, NULL,
			      CONFIG_BSHBUS_DUMMY_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(tid, "dummybshbus_tx");

	tid = k_thread_create(&dummybbus_data->rx.thread, dummybbus_data->rx.stack,
			      K_KERNEL_STACK_SIZEOF(dummybbus_data->rx.stack),
			      dummybbus_rx_thread, (void *)dev, NULL, NULL,
			      CONFIG_BSHBUS_DUMMY_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(tid, "dummybshbus_rx");

    LOG_DBG("...init finished");

    return 0;
}

static DEVICE_API(bshbus, dummybbus_driver_api) = {
	.start = dummybbus_start,
	.stop = dummybbus_stop,
	.dbus2_send = dummybbus_send,
    .dbus2_add_receiver = dummybbus_add_receiver,
    .dbus2_remove_receiver = dummybbus_remove_receiver,
};

#define DUMMYBBUS_INIT(inst)                                                    \
    static const struct dummy_bshbus_config dummybbus_config_##inst = {         \
    };                                                                          \
                                                                                \
    static struct dummy_bshbus_data dummybbus_data_##inst;                      \
                                                                                \
    BSHBUS_DEVICE_DT_INST_DEFINE(inst, dummybbus_init, NULL, &dummybbus_data_##inst,  \
                  &dummybbus_config_##inst, POST_KERNEL,                           \
                  CONFIG_BSHBUS_INIT_PRIORITY, &dummybbus_driver_api);

DT_INST_FOREACH_STATUS_OKAY(DUMMYBBUS_INIT)
