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

struct bshbus_recv {
	struct net_if *iface;
	struct net_context *ctx;
	enum bshbus_proto_id proto_id;
	void *proto_receiver;
};

// Compile Zeit
static struct bshbus_proto protocols[] = {
#if defined(CONFIG_NET_SOCKETS_BSHBUS_DBUS2) || \
	defined(CONFIG_NET_SOCKETS_BSHBUS_DBUS2_DUMMY)
	{
		.register_receiver = bshbus2_register_receiver,
		.unregister_receiver = bshbus2_unregister_receiver
	}
#endif
};

#define BSHBUS_MAX_RECEIVERS \
	CONFIG_NET_SOCKETS_BSHBUS2_RECEIVERS + \
	0

static struct bshbus_recv receivers[BSHBUS_MAX_RECEIVERS];

extern const struct socket_op_vtable sock_fd_op_vtable;
static const struct socket_op_vtable bshbus_sock_fd_op_vtable;

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
}

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

static void zbshbus_received_cb(struct net_context *ctx, struct net_pkt *pkt,
			     union net_ip_header *ip_hdr,
			     union net_proto_header *proto_hdr,
			     int status, void *user_data)
{
	struct bshbus_frame *frame = (struct bshbus_frame *)net_pkt_data(pkt);
	int i;

	switch (bshbus_frame_get_flag(frame)) {
		case BSHBUS_FRAME_DBUS2_TX_IND:
	LOG_DBG("unique_id: %d", bshbus_frame_to_dbus2_tx_ind(frame)->unique_id);
	LOG_DBG("status: %x", bshbus_frame_to_dbus2_tx_ind(frame)->status);
			break;
		default:
			LOG_ERR("Invalid frame format %d", bshbus_frame_get_flag(frame));
			// TODO clena up
			break;
	}

	for (i = 0; i < ARRAY_SIZE(receivers); i++) {
		if (!receivers[i].ctx ||
		    receivers[i].iface != net_pkt_iface(pkt)) {
			continue;
		}

		switch (bshbus_frame_get_flag(frame)) {
			case BSHBUS_FRAME_DBUS2_TX_IND:
				if (receivers[i].ctx == net_pkt_context(pkt)) {
					LOG_DBG("Receiver for D-Bus-2 TX IND found");
				}
				break;
			default:
				LOG_ERR("Invalid frame format %d", bshbus_frame_get_flag(frame));
				return;
				break;
		}

		ctx = receivers[i].ctx;

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
//TODO Remove
receivers[0].ctx = ctx;
receivers[0].iface = iface;

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

/* TODO prüfen ob hier wirklich abgebrochen wird */
	NET_ASSERT(recv_len == sizeof(struct bshbus_frame));

LOG_DBG("frame format %d", bshbus_frame_get_flag((struct bshbus_frame *)pkt->frags->data));

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

static int zbshbus_setsockopt_ctx(struct net_context *ctx, int level, int optname,
			       const void *optval, socklen_t optlen)
{
	return sock_fd_op_vtable.setsockopt(ctx, level, optname,
					    optval, optlen);
}

static int bshbus_sock_setsockopt_vmeth(void *obj, int level, int optname,
				     const void *optval, socklen_t optlen)
{
	const struct bshbus_api *api;
	struct net_if *iface;
	const struct device *dev;
	int ret;

	if (level != SOL_BSHBUS_DBUS2) {
		return zbshbus_setsockopt_ctx(obj, level, optname, optval, optlen);
	}

	if (optval == NULL) {
		errno = EINVAL;
		return -1;
	}

	iface = net_context_get_iface(obj);
	dev = net_if_get_device(iface);
	api = dev->api;

	if (!api || !api->setsockopt) {
		errno = ENOTSUP;
		return -1;
	}

	return api->setsockopt(dev, obj, level, optname, optval, optlen);
}

static int bshbus_close_socket(struct net_context *ctx)
{
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(receivers); i++) {
		if (receivers[i].ctx == ctx &&
			receivers[i].iface == net_context_get_iface(ctx)) {
				ret = unregister_bshbus_receiver(&receivers[i]);
				if (!ret) {
					receivers[i].ctx = NULL;
				}
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
