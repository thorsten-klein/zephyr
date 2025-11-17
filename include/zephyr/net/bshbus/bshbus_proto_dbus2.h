#ifndef ZEPHYR_INCLUDE_NET_SOCKET_BSHBUS_PROTO_DBUS2_H_
#define ZEPHYR_INCLUDE_NET_SOCKET_BSHBUS_PROTO_DBUS2_H_

#include <zephyr/types.h>
#include <zephyr/net/bshbus/sockets_bshbus_priv.h>

#ifdef __cplusplus
extern "C" {
#endif

 /**
 * BSH D-Bus-2 message ID handling
 *
 * The message ID is divided into 1024 bit fields with 64 bit size, where every
 * BSH D-Bus-2 session can register several message ID ranges. Register the
 * message ID twice is not possible.
 *
 * The message IDs are mapped to the structure bshbus_dbus2_msg_id_range. For
 * example an entry for the message ID 0xFCB0 is represented with a single
 * range in the structure bshbus_dbus2_msg_id_ranges like this:
 *     id_order = 0xFCB0 / 64 = 1010
 *     id_mask = 1 << (0xFCB0 % 64) = 48
 */

 /* BSH D-Bus-2 maximum message ID order */
#define BSHBUS2_ID_MAX_RANGES 1024
#define BSHBUS2_IDS_PER_ORDER 64
#define BSHBUS2_ID_MAX_ORDER  (BSHBUS2_ID_MAX_RANGES - 1)

/**
 * @brief BSH D-Bus-2 protocol data structure.
 */
struct bshbus2_proto {
	/** BSH D-Bus-2 message IDs. */
	struct bshbus_dbus2_msg_id_ranges *ids;
};

/**
 * @brief BSH D-Bus-2 message ID structure.
 */
struct bshbus_dbus2_msg_id_range {
	uint16_t msg_id_start;
	uint16_t msg_id_end;
};

/**
 * @brief BSH D-Bus-2 message ID range structure.
 */
struct bshbus_dbus2_msg_id_ranges {
	/** Number of elements in the ranges array. */
	uint16_t range_cnt;
	/** Variable array of message ID ranges. */
	struct bshbus_dbus2_msg_id_range *ranges;
};

/**
 * @brief Convert message ID high and low byte to BSH D-Bus-2 message ID.
 *
 * @param msg_id_high Message ID high byte
 * @param msg_id_low Message ID low byte
 *
 * @return BSH D-Bus-2 message ID
 */
static inline uint16_t bshbus2_convert_to_msg_id(uint8_t msg_id_high,
		    uint8_t msg_id_low)
{
	return (msg_id_high << 8 | msg_id_low);
}

/**
 * @brief BSH D-Bus-2 receiver registration function.
 *
 * @param proto_receiver BSH D-Bus-2 protocol receiver
 * @param proto_data BSH D-Bus-2 protocol specific data for receiver
 *
 * @return 0 on success
 */
int bshbus2_register_receiver(void **proto_receiver, void *proto_data);

/**
 * @brief BSH D-Bus-2 receiver unregistration function.
 *
 * @param proto_receiver BSH D-Bus-2 protocol receiver
 *
 * @return 0 on success
 */
int bshbus2_unregister_receiver(void *proto_receiver);

#ifdef __cplusplus
}
#endif

#endif
