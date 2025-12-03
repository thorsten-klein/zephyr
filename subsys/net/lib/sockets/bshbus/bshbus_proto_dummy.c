#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(net_sock_bshbus2_dummy, CONFIG_NET_SOCKETS_LOG_LEVEL);

#include <errno.h>
#include <stdbool.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/bshbus/bshbus_proto_dbus2.h>

struct bshbus2_recv {
	struct bshbus2_proto *proto;
};

struct bshbus2_recv receivers[CONFIG_NET_SOCKETS_BSHBUS2_RECEIVERS];

static struct bshbus2_recv *get_empty_receiver(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(receivers); i++) {
		if (!receivers[i].proto) {
			return &receivers[i];
		}
	}

	NET_WARN("All receivers occupied");

	return NULL;
}

static int check_id_ranges(uint16_t ids_count, struct bshbus_dbus2_msg_id_range *ids)
{
	int i;
	int previous_id_order = -1;

	for (i = 0; i < ids_count; i++) {
		if (BSHBUS2_ID_MAX_ORDER < ids[i].id_order) {
			NET_ERR("Message ID order %d exceeds the limit %d",
					ids[ids_count].id_order, BSHBUS2_ID_MAX_ORDER);
			return -EINVAL;
		}
		else if (previous_id_order >= ids[i].id_order) {
			NET_ERR("Incorrect message ID order\n");
			return -EINVAL;
		}
	}

	return 0;
}

static bool is_msg_id_registered(uint16_t msg_id_high, uint16_t msg_id_low,
		struct bshbus_dbus2_msg_id_ranges *ids)
{
	struct bshbus_dbus2_msg_id_range *range;
	uint64_t id_bit;
	uint16_t cnt, id_order, msg_id;

	msg_id = bshbus2_convert_to_msg_id(msg_id_high, msg_id_low);
	id_bit = bshbus_dbus2_get_id_bit(msg_id);
	id_order = bshbus_dbus2_get_id_order(msg_id);

	for (cnt = 0; cnt < ids->range_cnt; cnt++) {
		range = &ids->ranges[cnt];

		if (id_order == range->id_order && (id_bit & range->id_mask)) {
			/* Entry found */
			return true;
		}
		else if (id_order < range->id_order) {
			/* Ranges are ordered by their ID order, quit searching if only higher
			 * orders exist
			 */
			break;
		}
	}

	return false;
}


static bool is_receiver_attached(struct bshbus2_recv *receiver)
{
	int i;
	struct bshbus_dbus2_msg_id_range *range;

	for (i = 0; i < ARRAY_SIZE(receivers); i++) {
	}

	return false;
}

static int bshbus2_register_receiver(void **proto_receiver, void *proto_data)
{
	int ret;
	struct bshbus2_proto *proto;
	struct bshbus2_recv *receiver;

	if (!proto_receiver || !proto_data) {
		NET_ERR("Invalid pointer");
		return -EINVAL;
	}

	proto = (struct bshbus2_proto *)proto_data;
	if (!proto->ids) { //iface prüfen?
		NET_ERR("No message IDs provided");
		return -EINVAL;
	}

	ret = check_id_ranges(proto->ids->range_cnt, proto->ids->ranges);
	if (ret) {
		return ret;
	}

	receiver = get_empty_receiver();
	if (!receiver) {
		return -EBUSY;
	}

	receiver->proto = proto_data;
	*proto_receiver = &receiver;

	return 0;
}

int bshbus2_unregister_receiver(void *proto_receiver)
{
	int i;
	struct bshbus2_recv *receiver;

	if (!proto_receiver) {
		NET_ERR("bshbus2: Invalid pointer");
		return -EINVAL;
	}

	receiver = (struct bshbus2_recv *)proto_receiver;

	for (i = 0; i < ARRAY_SIZE(receivers); i++) {
		if (!(&receivers[i].proto == &receiver->proto)) {
			receivers[i].proto = NULL;
		}
	}

	return 0;
}
