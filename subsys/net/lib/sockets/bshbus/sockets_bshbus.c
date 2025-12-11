#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_sock_bshbus, CONFIG_NET_BSHBUS_LOG_LEVEL);

#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/bshbus.h>
#include <zephyr/net/socket_bshbus.h>
#include <zephyr/net/bshbus/bshbus_proto_dbus2.h>
#include <zephyr/internal/syscall_handler.h>
#include <zephyr/sys/fdtable.h>
#include <zephyr/sys/util.h>

#include "sockets_internal.h"

#include <zephyr/drivers/bshbus.h> //TODO das muss nach socket_bshbus.h

BUILD_ASSERT(CONFIG_HEAP_MEM_POOL_SIZE > 0);

#if defined(CONFIG_NET_SOCKETS_BSHBUS_DBUS2) || \
	defined(CONFIG_NET_SOCKETS_BSHBUS_DBUS2_DUMMY)
typedef struct bshbus_dbus2_recv {
	/** Message ID order (0-1023). */
	uint16_t id_order;
	/** Bit mask for the particular message ID order. */
	uint64_t id_mask;
	/** Next object in linked list */
	struct bshbus_dbus2_recv *next;
} bshbus_dbus2_receiver;
#endif

struct bshbus_recv {
	struct net_if *iface;
	struct net_context *ctx;
	enum bshbus_proto_id proto_id;
	void *proto_receiver;
#if defined(CONFIG_NET_SOCKETS_BSHBUS_DBUS2) || \
	defined(CONFIG_NET_SOCKETS_BSHBUS_DBUS2_DUMMY)
	struct bshbus_dbus2_recv *dbus2_recv;
	uint8_t node_address;
#endif
};

/* TODO: Use later
static struct bshbus_proto protocols[] = {
#if defined(CONFIG_NET_SOCKETS_BSHBUS_DBUS2) || \
	defined(CONFIG_NET_SOCKETS_BSHBUS_DBUS2_DUMMY)
	{
		.register_receiver = bshbus2_register_receiver,
		.unregister_receiver = bshbus2_unregister_receiver
	}
#endif
}; */

#define BSHBUS_MAX_RECEIVERS \
	CONFIG_NET_SOCKETS_BSHBUS2_RECEIVERS + \
	0

static struct bshbus_recv receivers[BSHBUS_MAX_RECEIVERS];

extern const struct socket_op_vtable sock_fd_op_vtable;
static const struct socket_op_vtable bshbus_sock_fd_op_vtable;

static inline uint64_t bshbus_dbus2_get_id_bit(uint16_t msg_id)
{
	uint8_t bit_shift = msg_id % BSHBUS2_IDS_PER_ORDER;
	uint64_t id_bit = 1;

	id_bit = id_bit << bit_shift;

	return id_bit;
}

/**
 * @brief Get the BSH D-Bus-2 message ID order for a message ID.
 *
 * @param msg_id BSH D-Bus-2 message ID
 *
 * @return BSH D-Bus-2 ID order
 */
static inline uint16_t bshbus_dbus2_get_id_order(uint16_t msg_id)
{
	return (msg_id / BSHBUS2_IDS_PER_ORDER);
}

/* TODO: Use later
static int unregister_bshbus_receiver(struct bshbus_recv *receiver)
{
	int ret = 0;
	struct bshbus_proto *proto;

	if (receiver->proto_id < BSHBUS_PROTO_MAX) {
		proto = &protocols[receiver->proto_id];

	if (proto->unregister_receiver) {
			ret = proto->unregister_receiver(receiver->proto_receiver);
			if (ret) {
				NET_WARN("Unregister BSHBUS receiver protocol %d failed: %d",
						receiver->proto_id, ret);
			}
		}
	}
	else {
		NET_DBG("BSHBUS receiver has no protocol registered");
	}

	return ret;
} */

static inline int k_fifo_wait_non_empty(struct k_fifo *fifo, k_timeout_t timeout)
{
	struct k_poll_event events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_FIFO_DATA_AVAILABLE,
					 K_POLL_MODE_NOTIFY_ONLY, fifo),
	};

	return k_poll(events, ARRAY_SIZE(events), timeout);
}

static int zbshbus_socket(int family, int type, int proto)
{
	struct net_context *ctx;
	int fd, ret;

	fd = zvfs_reserve_fd();
	if (fd < 0) {
		return -1;
	}

	ret = net_context_get(family, type, proto, &ctx);
	if (ret < 0) {
		zvfs_free_fd(fd);
		errno = -ret;
		return -1;
	}

	/* Initialize user_data, all other calls will preserve it */
	ctx->user_data = NULL;

	k_fifo_init(&ctx->recv_q);

	/* Condition variable is used to avoid keeping lock for a long time
	 * when waiting data to be received
	 */
	k_condvar_init(&ctx->cond.recv);

	zvfs_finalize_typed_fd(fd, ctx,
				(const struct fd_op_vtable *)&bshbus_sock_fd_op_vtable,
				ZVFS_MODE_IFSOCK);

	return fd;
}

static bool dbus2_rx_check_deliver(uint16_t msg_id, struct bshbus_recv *receiver)
{
	struct bshbus_dbus2_recv *dbus2_recv;
	uint64_t id_bit;
	uint16_t id_order;

	if (receiver) {
		id_bit = bshbus_dbus2_get_id_bit(msg_id);
		id_order = bshbus_dbus2_get_id_order(msg_id);

		dbus2_recv = receiver->dbus2_recv;

		/* Ranges are ordered by their ID order, quit searching if only higher
		 * orders exist
		 */
		while (dbus2_recv && (dbus2_recv->id_order < id_order)) {
			dbus2_recv = dbus2_recv->next;
		}

		if (dbus2_recv && id_order == dbus2_recv->id_order && (id_bit & dbus2_recv->id_mask)) {
			/* Receiver found */
			return true;
		}
	}

	return false;
}

static void zbshbus_received_cb(struct net_context *ctx, struct net_pkt *pkt,
				 union net_ip_header *ip_hdr,
				 union net_proto_header *proto_hdr,
				 int status, void *user_data)
{
	struct bshbus_frame *frame = (struct bshbus_frame *)net_pkt_data(pkt);
	int i;

	ctx = NULL;

	for (i = 0; i < ARRAY_SIZE(receivers); i++) {
		if (!receivers[i].ctx || receivers[i].iface != net_pkt_iface(pkt)) {
			continue;
		}

		switch (bshbus_frame_get_flag(frame)) {
			case BSHBUS_FRAME_DBUS2_TX_IND:
				if (receivers[i].ctx == net_pkt_context(pkt)) {
					LOG_DBG("Receiver for D-Bus-2 TX IND found");
					ctx = receivers[i].ctx;
					break;
				}
				break;
			case BSHBUS_FRAME_DBUS2_RX:
				if (dbus2_rx_check_deliver(bshbus_frame_to_dbus2_rx(frame)->msg_id,
											&receivers[i]))
				{
					LOG_DBG("Receiver for D-Bus-2 RX MSG found");
					ctx = receivers[i].ctx;
					break;
				}
				break;
			default:
				LOG_ERR("Invalid frame format %d", bshbus_frame_get_flag(frame));
				return;
				break;
		}

		if (!ctx) {
			LOG_ERR("No receiver found");
			net_pkt_unref(pkt);
			return;
		}

		/* To prevent the reader from missing the wake-up signal
		 *  as described in commit 1184089 and implemented in sockets.c
		 */
		if (ctx->cond.lock) {
			(void)k_mutex_lock(ctx->cond.lock, K_FOREVER);
		}

		/* if pkt is NULL, EOF */
		if (!pkt) {
			struct net_pkt *last_pkt =
				k_fifo_peek_tail(&ctx->recv_q);

			if (!last_pkt) {
				/* If there're no packets in the queue,
				 * recv() may be blocked waiting on it to
				 * become non-empty, so cancel that wait.
				 */
				sock_set_eof(ctx);
				k_fifo_cancel_wait(&ctx->recv_q);

				NET_DBG("Marked socket %p as peer-closed", ctx);
			} else {
				net_pkt_set_eof(last_pkt, true);

				NET_DBG("Set EOF flag on pkt %p", ctx);
			}
		} else {
			/* Normal packet */
			net_pkt_set_eof(pkt, false);
			k_fifo_put(&ctx->recv_q, pkt);
		}

		if (ctx->cond.lock) {
			k_mutex_unlock(ctx->cond.lock);
		}
		k_condvar_signal(&ctx->cond.recv);
	}
}

static int zbshbus_bind_ctx(struct net_context *ctx, const struct sockaddr *addr,
			 socklen_t addrlen)
{
	struct sockaddr_bshbus *bshbus_addr = (struct sockaddr_bshbus *)addr;
	struct net_if *iface;
	int ret;

	if (addrlen != sizeof(struct sockaddr_bshbus)) {
		return -EINVAL;
	}

	iface = net_if_get_by_index(bshbus_addr->bshbus_ifindex);
	if (!iface) {
		return -ENOENT;
	}

	net_context_set_iface(ctx, iface);

	ret = net_context_bind(ctx, addr, addrlen);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}

	/* For BSH Bus socket, we expect to receive packets after call to bind().
	 */
	ret = net_context_recv(ctx, zbshbus_received_cb, K_NO_WAIT,
				   ctx->user_data);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}

	return 0;
}

ssize_t zbshbus_sendto_ctx(struct net_context *ctx, const void *buf, size_t len,
			int flags, const struct sockaddr *dest_addr,
			socklen_t addrlen)
{
	struct sockaddr_bshbus bshbus_addr;
	struct bshbus_frame *frame;
	k_timeout_t timeout = K_FOREVER;
	int ret;

	if (!buf || len != sizeof(struct bshbus_frame)) {
		LOG_ERR("Invalid BSHBus frame");
		return -1;
	}

	frame = (struct bshbus_frame *)buf;
	NET_ASSERT(len == sizeof(struct bshbus_frame));

	if ((flags & ZSOCK_MSG_DONTWAIT) || sock_is_nonblock(ctx)) {
		timeout = K_NO_WAIT;
	} else {
		net_context_get_option(ctx, NET_OPT_SNDTIMEO, &timeout, NULL);
	}

	if (addrlen == 0) {
		addrlen = sizeof(struct sockaddr_bshbus);
	}

	if (dest_addr == NULL) {
		memset(&bshbus_addr, 0, sizeof(bshbus_addr));

		bshbus_addr.bshbus_ifindex = -1;
		bshbus_addr.bshbus_family = AF_BSHBUS;

		dest_addr = (struct sockaddr *)&bshbus_addr;
	}

	ret = net_context_sendto(ctx, frame, sizeof(*frame),
				 dest_addr, addrlen, NULL, timeout,
				 ctx->user_data);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}

	return len;
}

static ssize_t zbshbus_recvfrom_ctx(struct net_context *ctx, void *buf,
				 size_t max_len, int flags, struct sockaddr *src_addr,
				 socklen_t *addrlen)
{
	size_t recv_len = 0;
	k_timeout_t timeout = K_FOREVER;
	struct net_pkt *pkt;

	if ((flags & ZSOCK_MSG_DONTWAIT) || sock_is_nonblock(ctx)) {
		timeout = K_NO_WAIT;
	} else {
		net_context_get_option(ctx, NET_OPT_RCVTIMEO, &timeout, NULL);
	}

	if (flags & ZSOCK_MSG_PEEK) {
		int ret;

		ret = k_fifo_wait_non_empty(&ctx->recv_q, timeout);
		/* EAGAIN when timeout expired, EINTR when cancelled */
		if (ret && ret != -EAGAIN && ret != -EINTR) {
			errno = -ret;
			return -1;
		}

		pkt = k_fifo_peek_head(&ctx->recv_q);
	} else {
		/* Mechanism as in sockets.c to allow parallel rx/tx
		 */
		if (!K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
			int res;

			res = zsock_wait_data(ctx, &timeout);
			if (res < 0) {
				errno = -res;
				return -1;
			}
		}

		pkt = k_fifo_get(&ctx->recv_q, timeout);
	}

	if (!pkt) {
		errno = EAGAIN;
		return -1;
	}

	/* We do not handle any headers here, just pass the whole packet to
	 * the caller.
	 */
	recv_len = net_pkt_get_len(pkt);
	if (recv_len > max_len) {
		recv_len = max_len;
	}

	NET_ASSERT(recv_len == sizeof(struct bshbus_frame));

	if (net_pkt_read(pkt, buf, recv_len)) {
		net_pkt_unref(pkt);
		errno = EIO;
		return -1;
	}

	net_pkt_unref(pkt);

	return recv_len;
}

static int zbshbus_getsockopt_ctx(struct net_context *ctx, int level, int optname,
				   void *optval, socklen_t *optlen)
{
	if (!optval || !optlen) {
		errno = EINVAL;
		return -1;
	}

	return sock_fd_op_vtable.getsockopt(ctx, level, optname,
						optval, optlen);
}

static int bshbus_sock_getsockopt_vmeth(void *obj, int level, int optname,
					 void *optval, socklen_t *optlen)
{
	if (level == SOL_BSHBUS_DBUS2) {
		const struct bshbus_api *api;
		struct net_if *iface;
		const struct device *dev;

		if (optval == NULL) {
			errno = EINVAL;
			return -1;
		}

		iface = net_context_get_iface(obj);
		dev = net_if_get_device(iface);
		api = dev->api;
/*
		if (!api || !api->getsockopt) {
			errno = ENOTSUP;
			return -1;
		} */

		return api->getsockopt(dev, obj, level, optname, optval,
					   optlen);
	}

	return zbshbus_getsockopt_ctx(obj, level, optname, optval, optlen);
}

static struct bshbus_recv *get_receiver(const struct net_context *ctx)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(receivers); i++) {
		if (receivers[i].iface && receivers[i].ctx == ctx) {
			/* Receiver already exists, return it */
			return &receivers[i];
		}
	}

	/* Receiver has not been registered before, find an empty slot */
	for (i = 0; i < ARRAY_SIZE(receivers); i++) {
		if (!receivers[i].iface && !receivers[i].ctx) {
			return &receivers[i];
		}
	}

	LOG_ERR("All receivers occupied");

	return NULL;
}

static void free_dbus2_receiver_list(struct bshbus_recv *receiver)
{
	struct bshbus_dbus2_recv *dbus2_recv;
	struct bshbus_dbus2_recv *next;

	if (!receiver || !receiver->dbus2_recv) {
		return;
	}

	dbus2_recv = receiver->dbus2_recv;

	while (dbus2_recv) {
		next = dbus2_recv->next;
		k_free(dbus2_recv);
		dbus2_recv = next;
	}

	receiver->dbus2_recv = NULL;
}

static void cleanup_dbus2_receiver_partial(struct bshbus_recv *receiver, uint16_t id_order_limit)
{
	struct bshbus_dbus2_recv *dbus2_recv;
	struct bshbus_dbus2_recv *prev = NULL;
	struct bshbus_dbus2_recv *next;

	if (!receiver || !receiver->dbus2_recv) {
		return;
	}

	dbus2_recv = receiver->dbus2_recv;

	while (dbus2_recv && dbus2_recv->id_order < id_order_limit) {
		next = dbus2_recv->next;
		k_free(dbus2_recv);
		dbus2_recv = next;
	}

	/* Update list head if we freed from the beginning */
	if (!prev) {
		receiver->dbus2_recv = dbus2_recv;
	}
}

static int create_dbus2_receiver_entry(struct bshbus_recv *receiver,
			uint16_t id_order, uint64_t id_mask)
{
	struct bshbus_dbus2_recv *dbus2_recv = receiver->dbus2_recv;
	struct bshbus_dbus2_recv *prev_dbus2_recv = NULL;

	while (dbus2_recv && dbus2_recv->id_order < id_order) {
		prev_dbus2_recv = dbus2_recv;
		dbus2_recv = dbus2_recv->next;
	}

	if (dbus2_recv && dbus2_recv->id_order == id_order) {
		/* Add message IDs to the already existing message ID order band */
		dbus2_recv->id_mask |= id_mask;
	}
	else {
		/* Create new receiver entry */
		dbus2_recv = k_malloc(sizeof(*dbus2_recv));
		if (!dbus2_recv) {
			LOG_ERR("Not enough memory to create the receiver entry");
			return -ENOMEM;
		}

		if (prev_dbus2_recv) {
			/* Merge entry into the list */
			dbus2_recv->next = prev_dbus2_recv->next;
			prev_dbus2_recv->next = dbus2_recv;
		}
		else {
			/* First entry in the list */
			dbus2_recv->next = receiver->dbus2_recv;
			receiver->dbus2_recv = dbus2_recv;
		}

		dbus2_recv->id_order = id_order;
		dbus2_recv->id_mask = id_mask;
	}

	return 0;
}

static int create_dbus2_receiver(struct bshbus_recv *receiver,
			uint16_t msg_id_start, uint16_t msg_id_end)
{
	uint64_t id_mask, bit_val;
	uint32_t range_cnt;
	uint16_t id_order, id_order_start, id_order_end, id_bit;
	int ret, bit;

	id_order_start = bshbus_dbus2_get_id_order(msg_id_start);
	id_order_end = bshbus_dbus2_get_id_order(msg_id_end);
	range_cnt = id_order_end - id_order_start + 1;

	for (uint16_t i = 0; i < range_cnt; i++) {
		id_order = id_order_start + i;
		id_mask = 0;
		if (i == 0) {
			/* first range */
			id_order = id_order_start;
			id_bit = bshbus_dbus2_get_id_bit(msg_id_start);

			for (bit = 63; bit >= 0; bit--) {
				bit_val = 1 << bit;
				id_mask |= bit_val;
				if (id_bit & bit_val) {
					break;
				}
			}
		}
		else if (i == (range_cnt - 1)) {
			/* last range */
			id_order = id_order_end;
			id_bit = bshbus_dbus2_get_id_bit(msg_id_end);

			for (bit = 0; bit <= 63; bit++) {
				bit_val = 1 << bit;
				id_mask |= bit_val;
				if (id_bit & bit_val) {
					break;
				}
			}
		}
		else {
			/* full range */
			id_mask = UINT64_MAX;
		}

		ret = create_dbus2_receiver_entry(receiver, id_order, id_mask);
		if (ret) {
			cleanup_dbus2_receiver_partial(receiver, id_order);
			LOG_ERR("Failed to create receiver entries");
			return ret;
		}
	}

	return 0;
}

static int bshbus2_add_receiver(struct net_context *ctx, int level, int optname,
							const struct bshbus_dbus2_msg_id_range *id_range, socklen_t optlen)
{
	const struct bshbus_api *api;
	const struct device *dev;
	struct bshbus_recv *receiver;
	int ret;

	if (!id_range || optlen != sizeof(*id_range)) {
		LOG_ERR("Invalid message ID range structure");
		return -EINVAL;
	}

	if (id_range->msg_id_start > id_range->msg_id_end) {
		LOG_ERR("Invalid message ID range, start: %04x, end %04x\n",
			id_range->msg_id_start, id_range->msg_id_end);
			return -EINVAL;
	}

	receiver = get_receiver(ctx);
	if (!receiver) {
		return -EBUSY;
	}

	ret = create_dbus2_receiver(receiver, id_range->msg_id_start, id_range->msg_id_end);
	if (ret) {
		LOG_ERR("Create receiver failed: %d", ret);
		return ret;
	}

	if (!receiver->ctx) {
		receiver->iface = net_context_get_iface(ctx);
		receiver->ctx = ctx;
	}

	dev = net_if_get_device(receiver->iface);
	api = dev->api;

	ret = api->setsockopt(dev, ctx, level, optname, id_range, optlen);
	if (ret) {
		LOG_ERR("Adding D-Bus-2 receiver failed: %d", ret);
		/* Clean up the receiver list on driver failure */
		free_dbus2_receiver_list(receiver);
		return ret;
	}

	return 0;
}

static int bshbus2_register_node(struct net_context *ctx, int level, int optname,
				   const uint8_t *node_address, socklen_t optlen)
{
	const struct bshbus_api *api;
	const struct device *dev;
	struct bshbus_recv *receiver;
	int ret;

	if (!node_address || optlen != sizeof(*node_address)) {
		LOG_ERR("Invalid node address");
		return -EINVAL;
	}

	if (*node_address < 0x10) {
		LOG_ERR("Invalid node address: %02x", *node_address);
		return -EINVAL;
	}

	receiver = get_receiver(ctx);
	if (!receiver) {
		return -EBUSY;
	}

	if (receiver->node_address) {
		LOG_ERR("Node already registered");
		return 0;
	}

	receiver->node_address = *node_address;

	if (!receiver->ctx) {
		receiver->iface = net_context_get_iface(ctx);
		receiver->ctx = ctx;
	}

	dev = net_if_get_device(receiver->iface);
	api = dev->api;

	ret = api->setsockopt(dev, ctx, level, optname, node_address, optlen);
	if (ret) {
		LOG_ERR("Registering D-Bus-2 node failed: %d", ret);
		receiver->node_address = 0;
		return ret;
	}

	return 0;
}

static int zbshbus_setsockopt_ctx(struct net_context *ctx, int level, int optname,
				   const void *optval, socklen_t optlen)
{
	const struct bshbus_api *api;
	struct net_if *iface;
	const struct device *dev;

	if (level != SOL_BSHBUS_DBUS2) {
		return sock_fd_op_vtable.setsockopt(ctx, level, optname, optval, optlen);
	}

	if (optval == NULL) {
		return -EINVAL;
	}

	iface = net_context_get_iface(ctx);
	dev = net_if_get_device(iface);
	api = dev->api;

	if (!api || !api->setsockopt) {
		errno = ENOTSUP;
		return -1;
	}

	switch (optname) {
		case BSHBUS_DBUS2_RECEIVER:
			return bshbus2_add_receiver(ctx, level, optname, optval, optlen);
			break;
		case BSHBUS_DBUS2_NODE:
			return bshbus2_register_node(ctx, level, optname, optval, optlen);
			break;
		default:
			LOG_ERR("Invalid option name %d", optname);
			return -EINVAL;
			break;
	}

	return -EINVAL;
}

static int bshbus_sock_setsockopt_vmeth(void *obj, int level, int optname,
					 const void *optval, socklen_t optlen)
{
	return zbshbus_setsockopt_ctx(obj, level, optname, optval, optlen);
}

static int bshbus_close_socket(struct net_context *ctx)
{
	int i, ret = 0;

	// TODO: sync with recv

	for (i = 0; i < ARRAY_SIZE(receivers); i++) {
		if (receivers[i].ctx == ctx &&
			receivers[i].iface == net_context_get_iface(ctx)) {
			free_dbus2_receiver_list(&receivers[i]);

			receivers[i].ctx = NULL;
			receivers[i].iface = NULL;
			receivers[i].node_address = 0;
			receivers[i].proto_id = 0;
			receivers[i].proto_receiver = NULL;
		}
	}

	return ret;
}

static int bshbus_sock_close_vmeth(void *obj)
{
	int ret;

	ret = bshbus_close_socket(obj);
	if (ret < 0) {
		NET_DBG("Cannot detach net_context %p (%d)", obj, ret);

		errno = -ret;
		ret = -1;
	}

	return ret;
}

static int bshbus_sock_ioctl_vmeth(void *obj, unsigned int request, va_list args)
{
	return sock_fd_op_vtable.fd_vtable.ioctl(obj, request, args);
}

static int bshbus_sock_bind_vmeth(void *obj, const struct sockaddr *addr,
				   socklen_t addrlen)
{
	return zbshbus_bind_ctx(obj, addr, addrlen);
}

static ssize_t bshbus_sock_write_vmeth(void *obj, const void *buffer,
					size_t count)
{
	return zbshbus_sendto_ctx(obj, buffer, count, 0, NULL, 0);
}

static ssize_t bshbus_sock_sendto_vmeth(void *obj, const void *buf, size_t len,
					 int flags,
					 const struct sockaddr *dest_addr,
					 socklen_t addrlen)
{
	return zbshbus_sendto_ctx(obj, buf, len, flags, dest_addr, addrlen);
}

static ssize_t bshbus_sock_recvfrom_vmeth(void *obj, void *buf, size_t max_len,
					   int flags, struct sockaddr *src_addr,
					   socklen_t *addrlen)
{
	return zbshbus_recvfrom_ctx(obj, buf, max_len, flags, src_addr, addrlen);
}

static const struct socket_op_vtable bshbus_sock_fd_op_vtable = {
	.fd_vtable = {
		.close = bshbus_sock_close_vmeth,
		.write = bshbus_sock_write_vmeth,
		.ioctl = bshbus_sock_ioctl_vmeth,
	},
	.bind = bshbus_sock_bind_vmeth,
	.sendto = bshbus_sock_sendto_vmeth,
	.recvfrom = bshbus_sock_recvfrom_vmeth,
	.getsockopt = bshbus_sock_getsockopt_vmeth,
	.setsockopt = bshbus_sock_setsockopt_vmeth,
};

static bool bshbus_is_supported(int family, int type, int proto)
{
	if (type != SOCK_RAW || proto >= BSHBUS_PROTO_MAX) {
		return false;
	}

	return true;
}

NET_SOCKET_REGISTER(af_bshbus, NET_SOCKET_DEFAULT_PRIO, AF_BSHBUS,
			bshbus_is_supported, zbshbus_socket);
