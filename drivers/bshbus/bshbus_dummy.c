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

struct dummy_bshbus_cb_data {
    void *user_data;
    bshbus_dbus2_tx_callback_t cb;
};

struct dummy_bshbus_data {
	struct k_thread int_thread;
	struct k_sem int_sem;
    struct dummy_bshbus_cb_data dbus2_tx_cb;

	K_KERNEL_STACK_MEMBER(int_stack, CONFIG_BSHBUS_DUMMY_THREAD_STACK_SIZE);
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

    dummybbus_data->dbus2_tx_cb.cb = cb;
    dummybbus_data->dbus2_tx_cb.user_data = user_data;

    k_sem_give(&dummybbus_data->int_sem);

    return 0;
}

int dummybbus_add_receiver(const struct device *dev)
{
    LOG_DBG("Add receiver to %s:", dev->name);

    return 0;
}

int dummybbus_remove_receiver(const struct device *dev)
{
    LOG_DBG("Remove receiver from %s:", dev->name);

    return 0;
}

static void dummybbus_int_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

    const struct device *dev = p1;
	struct dummy_bshbus_data *dummybbus_data = dev->data;

    LOG_DBG("Thread started...");

	while (true) {
		k_sem_take(&dummybbus_data->int_sem, K_FOREVER);

        LOG_DBG("%s woken up", dev->name);

        if (dummybbus_data->dbus2_tx_cb.cb) {
            dummybbus_data->dbus2_tx_cb.cb(dev, BSHBUS_FRAME_STATUS_VALID, dummybbus_data->dbus2_tx_cb.user_data);
        }
	}
}

static int dummybbus_init(const struct device *dev)
{
	struct dummy_bshbus_data *dummybbus_data = dev->data;
    k_tid_t tid;

    LOG_DBG("Calling init...");

    /* Initialize int_sem to 1 to ensure any pending IRQ is serviced */
	k_sem_init(&dummybbus_data->int_sem, 1, 1);

	tid = k_thread_create(&dummybbus_data->int_thread, dummybbus_data->int_stack,
			      K_KERNEL_STACK_SIZEOF(dummybbus_data->int_stack),
			      dummybbus_int_thread, (void *)dev, NULL, NULL,
			      CONFIG_BSHBUS_DUMMY_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(tid, "dummybshbus");

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
