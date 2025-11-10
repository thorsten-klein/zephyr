/*
 * Copyright (c) 2025 Wolfgang Birkner
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/bshbus.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/check.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bshbus_common, CONFIG_BSHBUS_LOG_LEVEL);

int z_impl_bshbus_dbus2_send(const struct device *dev,
        const struct bshbus_frame_dbus2_tx *frame,
        bshbus_dbus2_tx_callback_t cb, void *user_data)
{
	const struct bshbus_driver_api *api = (const struct bshbus_driver_api *)dev->api;
	uint32_t id_mask;

    LOG_DBG("Send request");

    CHECKIF(frame == NULL) {
        LOG_ERR("Frame is NULL");
        return -EINVAL;
	}

    CHECKIF(cb == NULL) {
        LOG_ERR("Callback is NULL");
        return -EINVAL;
    }

    CHECKIF(user_data == NULL) {
        LOG_ERR("User data is NULL");
        return -EINVAL;
    }
/*
	if ((frame->flags & CAN_FRAME_IDE) != 0U) {
		id_mask = CAN_EXT_ID_MASK;
	} else {
		id_mask = CAN_STD_ID_MASK;
	}

	CHECKIF((frame->id & ~(id_mask)) != 0U) {
		LOG_ERR("invalid frame with %s (%d-bit) CAN ID 0x%0*x",
			(frame->flags & CAN_FRAME_IDE) != 0 ? "extended" : "standard",
			(frame->flags & CAN_FRAME_IDE) != 0 ? 29 : 11,
			(frame->flags & CAN_FRAME_IDE) != 0 ? 8 : 3, frame->id);
		return -EINVAL;
	} */

	return api->dbus2_send(dev, frame, cb, user_data);
}
