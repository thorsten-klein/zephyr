#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(net_sock_bshbus2, CONFIG_NET_SOCKETS_LOG_LEVEL);

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

int bshbus2_register_receiver(void **proto_receiver, void *proto_data)
{
	struct bshbus2_proto *proto;
	struct bshbus2_recv *receiver;

	proto = (struct bshbus2_proto *)proto_data;
	if (!proto->ids) {
		NET_ERR("No message IDs provided");
		return -EINVAL;
	}

	receiver = get_empty_receiver();
	receiver->proto = proto_data;
	*proto_receiver = receiver;

	return 0;
}

int bshbus2_unregister_receiver(void *proto_receiver)
{
	int i;
	struct bshbus2_recv *receiver;

	if (!proto_receiver) {
		NET_ERR("Invalid pointer");
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
