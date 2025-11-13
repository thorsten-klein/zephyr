/*
 * Copyright (c) 2025 Wolfgang Birkner
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief BSH Bus driver API.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_BSHBUS_H_
#define ZEPHYR_INCLUDE_DRIVERS_BSHBUS_H_

#include <errno.h>

#include <zephyr/types.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief BSH Bus Interface
 * @defgroup bshbus_interface BSH Bus Interface
 * @since x.x.x
 * @version 0.0.0
 * @ingroup io_interfaces
 * @{
 */

/**
 * @name BSH Bus frame definitions
 * @{
 */

/**
 * @brief Maximum data length specified in the BSH D-Bus-2 protocol
 */
#define BSHBUS_DBUS2_PROT_MAX_DLEN 252

/**
 * @brief Configured maximum data length for a BSH D-Bus-2 message.
 */
#if defined(CONFIG_BSHBUS_DBUS2_MAX_DLEN) && (CONFIG_BSHBUS_DBUS2_MAX_DLEN < BSHBUS_DBUS2_PROT_MAX_DLEN) // TODO Kconfig
#define BSHBUS_DBUS2_MAX_DLEN CONFIG_BSHBUS_DBUS2_MAX_DLEN
#else /* defined(CONFIG_BSHBUS_DBUS2_MAX_DLEN) && (CONFIG_BSHBUS_DBUS2_MAX_DLEN < BSHBUS_DBUS2_PROT_MAX_DLEN) */
#define BSHBUS_DBUS2_MAX_DLEN BSHBUS_DBUS2_PROT_MAX_DLEN
#endif /* defined(CONFIG_BSHBUS_DBUS2_MAX_DLEN) && (CONFIG_BSHBUS_DBUS2_MAX_DLEN < BSHBUS_DBUS2_PROT_MAX_DLEN) */

/**
 * @brief Frame for receiption of a BSH D-Bus-2 message
 */
struct bshbus_frame_dbus2_rx {
    /** Destination address of the message. */
    uint8_t dest_addr;
    /** Data length of the message. */
    uint8_t dlen;
    /** Message ID. */
    uint16_t msg_id;
    /** Payload data. */
    uint8_t data[BSHBUS_DBUS2_MAX_DLEN];
};

/**
 * @brief Frame for transmission of a BSH D-Bus-2 message
 */
struct bshbus_frame_dbus2_tx {
    /** Unique message ID. */
    uint16_t unique_id;
    /** Destination address of the message. */
    uint8_t dest_addr;
    /* Data length of the message. */
    uint8_t dlen;
    /** Message ID. */
    uint16_t msg_id;
    /** Payload data. */
    uint8_t data[BSHBUS_DBUS2_MAX_DLEN];
};

/**
 * @brief Frame for a BSH D-Bus-2 transmit indication
 */
struct bshbus_frame_dbus2_tx_ind {
    /** Unique message ID. */
    uint16_t unique_id;
    /** Status of transmitted message. @see @ref BSHBUS_FRAME_STATUS. */
    uint16_t status;
};

/**
 * @brief BSH Bus frame structure
 */
struct bshbus_frame {
	/** Data Length Code (DLC) indicating data length in bytes. */
	uint16_t dlc; // TODO DLC nötig?
	/** Flags. @see @ref BSHBUS_FRAME_FLAGS. */
	uint16_t flag;
	/** @cond INTERNAL_HIDDEN */
	/** Padding. */
	uint16_t reserved;
	/** @endcond */
	/** The frame payload data. */
	union {
		/** Received BSH D-Bus-2 message. */
		struct bshbus_frame_dbus2_rx rx;
		/** BSH D-Bus-2 message to transmit. */
		struct bshbus_frame_dbus2_tx tx;
        /** Transmit indication for a transmitted BSH D-Bus-2 message. */
        struct bshbus_frame_dbus2_tx_ind tx_ind;
	};
};

/** Frame for receiption of a D-Bus-2 message. @see @ref bshbus_frame_dbus2_rx */
#define BSHBUS_FRAME_DBUS2_RX BIT(0)

/** Frame for transmission of a D-Bus-2 message. @see @ref bshbus_frame_dbus2_tx  */
#define BSHBUS_FRAME_DBUS2_TX BIT(1)

/** Frame for D-Bus-2 transmit indication. @see @ref bshbus_frame_dbus2_tx_ind */
#define BSHBUS_FRAME_DBUS2_TX_IND BIT(2)

static inline void bshbus_frame_set_flag(struct bshbus_frame *frame,
			uint32_t flag)
{
	frame->flag = flag;
}

static inline uint16_t bshbus_frame_get_flag(struct bshbus_frame *frame)
{
	return frame->flag;
}

static inline struct bshbus_frame_dbus2_tx
		*bshbus_frame_to_dbus2_tx(struct bshbus_frame *frame)
{
	return &frame->tx;
};

static inline int bshbus_prepare_frame_dbus2_tx(struct bshbus_frame *frame,
		   uint16_t unique_id, uint8_t dest_addr, uint16_t msg_id, uint8_t dlen,
		   uint8_t *data)
{
	if (!frame || (dlen > 0 && !data)) {
		return -EINVAL;
	}

	if (dlen > BSHBUS_DBUS2_MAX_DLEN) {
		return -ENOBUFS;
	}

	bshbus_frame_set_flag(frame, BSHBUS_FRAME_DBUS2_TX);

	bshbus_frame_to_dbus2_tx(frame)->unique_id = unique_id;
	bshbus_frame_to_dbus2_tx(frame)->dest_addr = dest_addr;
	bshbus_frame_to_dbus2_tx(frame)->msg_id = msg_id;
	bshbus_frame_to_dbus2_tx(frame)->dlen = dlen;
	memcpy(bshbus_frame_to_dbus2_tx(frame)->data, data,
			bshbus_frame_to_dbus2_tx(frame)->dlen);

	return 0;
};

static inline struct bshbus_frame_dbus2_tx_ind
		*bshbus_frame_to_dbus2_tx_ind(struct bshbus_frame *frame)
{
	return &frame->tx_ind;
};

static inline void bshbus_prepare_frame_dbus2_tx_ind(struct bshbus_frame *frame,
		   uint16_t unique_id, uint16_t status)
{
	bshbus_frame_set_flag(frame, BSHBUS_FRAME_DBUS2_TX_IND);

	bshbus_frame_to_dbus2_tx_ind(frame)->unique_id = unique_id;
	bshbus_frame_to_dbus2_tx_ind(frame)->status = status;
};

static inline struct bshbus_frame_dbus2_rx
		*bshbus_frame_to_dbus2_rx(struct bshbus_frame *frame)
{
	return &frame->rx;
};

static inline void bshbus_prepare_frame_dbus2_rx(struct bshbus_frame *frame,
		   uint8_t dest_addr, uint16_t msg_id, uint8_t dlen,
		   uint8_t *data)
{
	bshbus_frame_set_flag(frame, BSHBUS_FRAME_DBUS2_RX);

	bshbus_frame_to_dbus2_rx(frame)->dest_addr = dest_addr;
	bshbus_frame_to_dbus2_rx(frame)->msg_id = msg_id;
	bshbus_frame_to_dbus2_rx(frame)->dlen = dlen;
	memcpy(bshbus_frame_to_dbus2_rx(frame)->data, data,
			bshbus_frame_to_dbus2_rx(frame)->dlen);
};

static inline bool bshbus_is_dbus2_tx_frame(struct bshbus_frame *frame)
{
	return (bshbus_frame_get_flag(frame) & BSHBUS_FRAME_DBUS2_TX ? true : false);
}

static inline bool bshbus_is_dbus2_tx_ind_frame(struct bshbus_frame *frame)
{
	return (bshbus_frame_get_flag(frame) & BSHBUS_FRAME_DBUS2_TX_IND ? true : false);
}

static inline bool bshbus_is_dbus2_rx_frame(struct bshbus_frame *frame)
{
	return (bshbus_frame_get_flag(frame) & BSHBUS_FRAME_DBUS2_RX ? true : false);
}

/**
 * @brief Defines the application callback handler function signature
 *
 * @param dev       Pointer to the device structure for the driver instance.
 * @param status    Status of the performed send operation. See the list of
 *                  return values for @a bshbus_send() for value descriptions.
 * @param user_data User data provided when the frame was sent.
 */
typedef void (*bshbus_dbus2_tx_callback_t)(const struct device *dev, uint16_t status, void *user_data);

/**
 * @brief Defines the application callback handler function signature for receiving
 *
 * @param dev       Pointer to the device structure for the driver instance.
 * @param frame     Received frame.
 * @param user_data User data provided when the receiver was added.
 */
typedef void (*bshbus_dbus2_rx_callback_t)(const struct device *dev,
				  struct bshbus_frame *frame, void *user_data);

/**
 * @name BSH Bus frame flags
 * @anchor BSHBUS_FRAME_FLAGS
 *
 * @{
 */

/** @} */

/**
 * @name BSH Bus frame status
 * @anchor BSHBUS_FRAME_STATUS
 *
 * @{
 */

/** Frame status unknown. */
#define BSHBUS_FRAME_STATUS_UNKOWN BIT(0)

/** Frame status valid message.  */
#define BSHBUS_FRAME_STATUS_VALID BIT(1)

/** Frame status invalid CRC */
#define BSHBUS_FRAME_STATUS_CRC_INVALID BIT(2)

/** Frame status receiver busy */
#define BSHBUS_FRAME_STATUS_BUSY BIT(3)

/** Frame status invalid acknowledge. */
#define BSHBUS_FRAME_STATUS_ACK_CORRUPT BIT(4)

/** Frame status no acknowledge received. */
#define BSHBUS_FRAME_STAUTS_ACK_TIMEOUT BIT(5)

/** Frame status collision */
#define BSHBUS_FRAME_STATUS_COLLISION BIT(6)

/** Frame status interbyte timeout */
#define BSHBUS_FRAME_STATUS_INTREBYTE_TIMEOUT BIT(7)

/** @} */

/**
 * @cond INTERNAL_HIDDEN
 *
 * For internal driver use only, skip these in public documentation.
 */

/**
 * @brief Callback API upon starting BSH Bus controller
 * See @a bshbus_start() for argument description
 */
typedef int (*bshbus_start_t)(const struct device *dev);

/**
 * @brief Callback API upon stopping BSH Bus controller
 * See @a bshbus_stop() for argument description
 */
typedef int (*bshbus_stop_t)(const struct device *dev);

/**
 * @brief Callback API upon sending a BSH D-Bus-2 frame
 * See @a bshbus_dbus2_send() for argument description
 */
typedef int (*bshbus_dbus2_send_t)(const struct device *dev,
			  const struct bshbus_frame_dbus2_tx *tx,
			  bshbus_dbus2_tx_callback_t cb, void *user_data);

/**
 * @brief Callback API upon adding a BSH D-Bus-2 receiver
 * See @a bshbus_dbus2_add_receiver() for argument description
 */
typedef int (*bshbus_dbus2_add_receiver_t)(const struct device *dev,
			  bshbus_dbus2_rx_callback_t cb, void *user_data);

/**
 * @brief Callback API upon removing a BSH D-Bus-2 receiver
 * See @a bshbus_dbus2_remove_receiver() for argument description
 */
typedef int (*bshbus_dbus2_remove_receiver_t)(const struct device *dev);

__subsystem struct bshbus_driver_api {
	bshbus_start_t start;
	bshbus_stop_t stop;
	bshbus_dbus2_send_t dbus2_send;
	bshbus_dbus2_add_receiver_t dbus2_add_receiver;
	bshbus_dbus2_remove_receiver_t dbus2_remove_receiver;
};

/** @endcond */

#define BSHBUS_DEVICE_DT_DEFINE(node_id, init_fn, pm, data, config, level,	\
			     prio, api, ...)				\
	DEVICE_DT_DEFINE(node_id, init_fn, pm, data, config, level,	\
			 prio, api, __VA_ARGS__)

/**
 * @brief Like BSHBUS_DEVICE_DT_DEFINE() for an instance of a DT_DRV_COMPAT compatible
 *
 * @param inst Instance number. This is replaced by <tt>DT_DRV_COMPAT(inst)</tt>
 *             in the call to BSHBUS_DEVICE_DT_DEFINE().
 * @param ...  Other parameters as expected by BSHBUS_DEVICE_DT_DEFINE().
 */
#define BSHBUS_DEVICE_DT_INST_DEFINE(inst, ...)			\
	BSHBUS_DEVICE_DT_DEFINE(DT_DRV_INST(inst), __VA_ARGS__)

/** @} */

 /**
 * @}
 */

 /**
 * @name Transmitting CAN frames
 *
 * @{
 */

/** TODO
 * @brief Start the CAN controller
 *
 * Bring the CAN controller out of `CAN_STATE_STOPPED`. This will reset the RX/TX error counters,
 * enable the CAN controller to participate in CAN communication, and enable the CAN transceiver, if
 * supported.
 *
 * Starting the CAN controller resets all the CAN controller statistics.
 *
 * @see can_stop()
 * @see can_transceiver_enable()
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @retval 0 if successful.
 * @retval -EALREADY if the device is already started.
 * @retval -EIO General input/output error, failed to start device.
 */
__syscall int bshbus_start(const struct device *dev);

static inline int z_impl_bshbus_start(const struct device *dev)
{
	const struct bshbus_driver_api *api = (const struct bshbus_driver_api *)dev->api;

	return api->start(dev);
}

__syscall int bshbus_dbus2_add_receiver(const struct device *dev,
            bshbus_dbus2_rx_callback_t cb, void *user_data);

static inline int z_impl_bshbus_dbus2_add_receiver(const struct device *dev,
            bshbus_dbus2_rx_callback_t cb, void *user_data)
{
	const struct bshbus_driver_api *api = (const struct bshbus_driver_api *)dev->api;

	if (api->dbus2_add_receiver) {
		return api->dbus2_add_receiver(dev, cb, user_data);
	}
	else {
		return 0;
	}
}

/** TODO
 * @brief Queue a CAN frame for transmission on the CAN bus
 *
 * Queue a CAN frame for transmission on the CAN bus with optional timeout and
 * completion callback function.
 *
 * Queued CAN frames are transmitted in order according to the their priority:
 * - The lower the CAN-ID, the higher the priority.
 * - Data frames have higher priority than Remote Transmission Request (RTR)
 *   frames with identical CAN-IDs.
 * - Frames with standard (11-bit) identifiers have higher priority than frames
 *   with extended (29-bit) identifiers with identical base IDs (the higher 11
 *   bits of the extended identifier).
 * - Transmission order for queued frames with the same priority is hardware
 *   dependent.
 *
 * @note If transmitting segmented messages spanning multiple CAN frames with
 * identical CAN-IDs, the sender must ensure to only queue one frame at a time
 * if FIFO order is required.
 *
 * By default, the CAN controller will automatically retry transmission in case
 * of lost bus arbitration or missing acknowledge. Some CAN controllers support
 * disabling automatic retransmissions via ``CAN_MODE_ONE_SHOT``.
 *
 * @param dev       Pointer to the device structure for the driver instance.
 * @param frame     CAN frame to transmit.
 * @param timeout   Timeout waiting for a empty TX mailbox or ``K_FOREVER``.
 * @param callback  Optional callback for when the frame was sent or a
 *                  transmission error occurred. If ``NULL``, this function is
 *                  blocking until frame is sent. The callback must be ``NULL``
 *                  if called from user mode.
 * @param user_data User data to pass to callback function.
 *
 * @retval 0 if successful.
 * @retval -EINVAL if an invalid parameter was passed to the function.
 * @retval -ENOTSUP if an unsupported parameter was passed to the function.
 * @retval -ENETDOWN if the CAN controller is in stopped state.
 * @retval -ENETUNREACH if the CAN controller is in bus-off state.
 * @retval -EBUSY if CAN bus arbitration was lost (only applicable if automatic
 *                retransmissions are disabled).
 * @retval -EIO if a general transmit error occurred (e.g. missing ACK if
 *              automatic retransmissions are disabled).
 * @retval -EAGAIN on timeout.
 */
__syscall int bshbus_dbus2_send(const struct device *dev,
			   const struct bshbus_frame_dbus2_tx *frame,
			   bshbus_dbus2_tx_callback_t cb, void *user_data);

/** @} */

#ifdef __cplusplus
}
#endif

#include <zephyr/syscalls/bshbus.h>

#endif /* ZEPHYR_INCLUDE_DRIVERS_BSHBUS_H_ */
