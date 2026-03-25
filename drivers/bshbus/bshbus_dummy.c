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
/* This is done to use the same thread for handling both Tx and WUP events */
/* This can be later adapted in bshbus.h, if required in actual driver implementation */
#define DUMMYBBUS_PENDING_EVENT_TX BIT(1)

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

struct dummy_bshbus_wup_tx_data {
    const struct device *dev;
    void *user_data;
    bshbus_dbus2_tx_callback_t cb;
    struct k_timer wakeup_check_timer;
    uint8_t wakeup_retry_count;
};

struct dummy_bshbus_data {
    struct dummy_bshbus_tx_data tx;
    struct dummy_bshbus_rx_data rx;
    struct dummy_bshbus_wup_tx_data wup_tx;
    uint16_t pending_events;
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
    /* BIT(1) is used for Tx here , later can be adapted in actual driver if needed*/
    /* This is done inorder to use the same thread for handling both Tx and WUP events */
    dummybbus_data->pending_events |= DUMMYBBUS_PENDING_EVENT_TX;

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

int dummybbus_check_wakeup_pulse(const struct dummy_bshbus_wup_tx_data *wup_data)
{
    LOG_DBG("Checking wakeup pulse, retry count: %d", wup_data->wakeup_retry_count);

    if(wup_data->wakeup_retry_count == 0) {
        LOG_DBG("Simulating failed wakeup pulse check");
        return -EAGAIN;
    } 
    LOG_DBG("Simulating successful wakeup pulse check");
    if (wup_data->cb) {
        wup_data->cb(wup_data->dev, BSHBUS_FRAME_STATUS_VALID, wup_data->user_data);
    }

    return 0;
}

int dummybbus_send_wakeup_pulse(const struct device *dev,
            bshbus_dbus2_tx_callback_t cb, void *user_data)
{
    struct dummy_bshbus_data *dummybbus_data = dev->data;

    LOG_DBG("Send wakeup pulse on %s:", dev->name);

    dummybbus_data->wup_tx.dev = dev;
    dummybbus_data->wup_tx.user_data = user_data;
    dummybbus_data->wup_tx.cb = cb;
    dummybbus_data->wup_tx.wakeup_retry_count = 0;
    dummybbus_data->pending_events |= BSHBUS_PENDING_EVENT_WUP_TX;
    k_timer_start(&dummybbus_data->wup_tx.wakeup_check_timer, K_MSEC(15), K_NO_WAIT);
    LOG_DBG("Timer started");

    return 0;
}

static void dummybbus_wakeup_pulse_timer_handler(struct k_timer *timer)
{
    struct dummy_bshbus_wup_tx_data *wup_data = CONTAINER_OF(timer, struct dummy_bshbus_wup_tx_data, wakeup_check_timer);
    struct dummy_bshbus_data *data = CONTAINER_OF(wup_data, struct dummy_bshbus_data, wup_tx);

    k_sem_give(&data->tx.sem);
}

int dummybbus_remove_receiver(const struct device *dev)
{
    LOG_DBG("Remove receiver from %s:", dev->name);

    return 0;
}

static void dummybbus_handle_wakeup_pulse_timeout(struct dummy_bshbus_data *data)
{
    int ret;

    ret = dummybbus_check_wakeup_pulse(&data->wup_tx);

    if (!ret) {
        data->wup_tx.wakeup_retry_count = 0;
        data->pending_events &= ~BSHBUS_PENDING_EVENT_WUP_TX;
        LOG_DBG("Wakeup pulse sent successfully");
    }
    else {
        data->wup_tx.wakeup_retry_count++;
        if (data->wup_tx.wakeup_retry_count <= 5) {
            k_timer_start(&data->wup_tx.wakeup_check_timer, K_MSEC(1), K_NO_WAIT);
            LOG_DBG("Timer restarted for next wakeup pulse check");
        } else {
            LOG_DBG("Wakeup pulse check timed out after %d retries", data->wup_tx.wakeup_retry_count);
            data->wup_tx.wakeup_retry_count = 0;
            data->pending_events &= ~BSHBUS_PENDING_EVENT_WUP_TX;
            if (data->wup_tx.cb) {
                data->wup_tx.cb(data->wup_tx.dev, BSHBUS_FRAME_STATUS_IO_ERROR, data->wup_tx.user_data);
            }
        }
    }
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

        if (dummybbus_data->pending_events & BSHBUS_PENDING_EVENT_WUP_TX) {
            dummybbus_handle_wakeup_pulse_timeout(dummybbus_data);
        }

        if (dummybbus_data->pending_events & DUMMYBBUS_PENDING_EVENT_TX) {
            LOG_DBG("%s TX woken up", dev->name);

            if (dummybbus_data->tx.cb) {
                dummybbus_data->tx.cb(dev, BSHBUS_FRAME_STATUS_VALID, dummybbus_data->tx.user_data);
            }
            dummybbus_data->pending_events &= ~DUMMYBBUS_PENDING_EVENT_TX;
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

        LOG_DBG("%s Wakeup RX message received", dev->name);

        bshbus_prepare_frame_dbus2_wakeup_pulse_rx(&frame);

        if (dummybbus_data->rx.cb) {
            dummybbus_data->rx.cb(dev, &frame, dummybbus_data->rx.user_data);
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
	k_timer_init(&dummybbus_data->wup_tx.wakeup_check_timer, dummybbus_wakeup_pulse_timer_handler, NULL);
	dummybbus_data->wup_tx.wakeup_retry_count = 0;

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
	.dbus2_send_wakeup_pulse = dummybbus_send_wakeup_pulse,
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
