/*
 * Copyright (c) 2025 BSH Hausgeraete
 * Copyright (c) 2025 Wolfgang Birkner
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <zephyr/net/net_pkt.h>
#include <zephyr/net/bshbus.h>
#include <zephyr/net/socket_bshbus.h>
#include <zephyr/net/bshbus/bshbus_proto_dbus2.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(net_bshbus, CONFIG_NET_BSHBUS_LOG_LEVEL);

#define SEND_TIMEOUT K_MSEC(100)

struct net_bshbus_context {
	struct net_if *iface;
};

struct net_bshbus_config {
	const struct device *bshbus_dev;
};

static void net_bshbus_close(const struct device *dev, int filter_id)
{
//	const struct net_bus_config *cfg = dev->config;

//	can_remove_rx_filter(cfg->can_dev, filter_id);
}

static void net_bshbus_dbus2_recv(const struct device *dev, struct bshbus_frame *frame, void *user_data)
{
	struct net_bshbus_context *ctx = user_data;
	struct net_pkt *pkt;
	int ret;

	ARG_UNUSED(dev);

	LOG_DBG("Received packet on interface %p", ctx->iface);
	LOG_DBG("\tdest_addr: %02x", bshbus_frame_to_dbus2_rx(frame)->dest_addr);
	LOG_DBG("\tmsg_id: %04x", bshbus_frame_to_dbus2_rx(frame)->msg_id);
	LOG_DBG("\tdlen: %d", bshbus_frame_to_dbus2_rx(frame)->dlen);

	pkt = net_pkt_rx_alloc_with_buffer(ctx->iface, sizeof(*frame), AF_BSHBUS, 0,
						K_NO_WAIT);
	if (pkt == NULL) {
		LOG_ERR("Failed to obtain net_pkt");
		return;
	}

	bshbus_frame_set_flag(frame, BSHBUS_FRAME_DBUS2_RX);

	if (net_pkt_write(pkt, frame, sizeof(*frame))) {
		LOG_ERR("Failed to append RX data");
		net_pkt_unref(pkt);
		return;
	}

	ret = net_recv_data(ctx->iface, pkt);
	if (ret < 0) {
		LOG_DBG("net_recv_data failed: %d", ret);
		net_pkt_unref(pkt);
	}
}

static void net_bshbus_dbus2_send_cb(const struct device *dev, uint16_t status,
			void *user_data) //TODO könnte nach dbus2 specific
{
	ARG_UNUSED(dev);

	struct net_pkt *tx_pkt = (struct net_pkt *)user_data;
	struct net_pkt *tx_ind_pkt = NULL;
	struct bshbus_frame *tx_frame;
	struct bshbus_frame tx_ind_frame = {0};
	int ret;

	LOG_DBG("...send message on %s finished: %d", dev->name, status);

	if (!tx_pkt) {
		LOG_ERR("Packet is invalid");
		return;
	}

	tx_ind_pkt = net_pkt_rx_alloc_with_buffer(tx_pkt->iface, sizeof(tx_ind_frame),
						AF_BSHBUS, 0, K_NO_WAIT);
	if (!tx_ind_pkt) {
		LOG_ERR("Allocate TX IND packet failed");
		goto free_pkt;
	}

	tx_frame = (struct bshbus_frame *)tx_pkt->frags->data;
	LOG_DBG("unique_id: %d", bshbus_frame_to_dbus2_tx(tx_frame)->unique_id);
	LOG_DBG("dest_addr: %02x", bshbus_frame_to_dbus2_tx(tx_frame)->dest_addr);
	LOG_DBG("msg_id: %04x", bshbus_frame_to_dbus2_tx(tx_frame)->msg_id);
	LOG_DBG("dlen: %d", bshbus_frame_to_dbus2_tx(tx_frame)->dlen);

	bshbus_prepare_frame_dbus2_tx_ind(&tx_ind_frame,
						bshbus_frame_to_dbus2_tx(tx_frame)->unique_id,
						status);

	if (net_pkt_write(tx_ind_pkt, &tx_ind_frame, sizeof(tx_ind_frame))) {
		LOG_ERR("Failed to append TX_IND data");
		goto free_pkt;
	}

	ret = net_recv_data(tx_pkt->iface, tx_ind_pkt);
	if (ret < 0) {
		LOG_DBG("net_recv_data failed: %d", ret);
		goto free_pkt;
	}

	net_pkt_unref(tx_pkt);

	return;

free_pkt:
	net_pkt_unref(tx_pkt);

	if (tx_ind_pkt) {
		net_pkt_unref(tx_ind_pkt);
	}
}

static int net_bshbus_dbus2_send(const struct device *dev, struct net_pkt *pkt) // TODO könnte nach dbus2 specific
{
	struct bshbus_frame *frame = (struct bshbus_frame *)pkt->frags->data;
	struct net_pkt *cb_pkt;

	cb_pkt = net_pkt_clone(pkt, K_NO_WAIT);
	if (!cb_pkt) {
		LOG_ERR("Clone net_pkt failed");
		return -ENOMEM;
	}

	LOG_DBG("flag: %d", bshbus_frame_get_flag(frame));
	LOG_DBG("reserved: %d", frame->reserved);
	LOG_DBG("unique_id: %d", bshbus_frame_to_dbus2_tx(frame)->unique_id);
	LOG_DBG("dest_addr: %02x", bshbus_frame_to_dbus2_tx(frame)->dest_addr);
	LOG_DBG("msg_id: %04x", bshbus_frame_to_dbus2_tx(frame)->msg_id);
	LOG_DBG("dlen: %d", bshbus_frame_to_dbus2_tx(frame)->dlen);

	return bshbus_dbus2_send(dev, &frame->tx, net_bshbus_dbus2_send_cb, cb_pkt);
}

static int net_bshbus_send(const struct device *dev, struct net_pkt *pkt)
{
	const struct net_bshbus_config *cfg = dev->config;
	struct bshbus_frame *frame;
	int ret;

	if (net_pkt_family(pkt) != AF_BSHBUS) {
		return -EPFNOSUPPORT;
	}

	frame = (struct bshbus_frame *)pkt->frags->data;
	switch (bshbus_frame_get_flag(frame)) {
		case BSHBUS_FRAME_DBUS2_TX:
			ret = net_bshbus_dbus2_send(cfg->bshbus_dev, pkt); // TODO könnte pointer sein
			break;
		default:
			LOG_ERR("Invalid frame format %d", bshbus_frame_get_flag(frame));
			return -EINVAL;
			break;
	}

	if (ret == 0) {
		net_pkt_unref(pkt);
		LOG_DBG("%s waiting until message was sent...", dev->name);
	} else {
		LOG_ERR("Cannot send BSHBus msg: %d", ret);
	}

	/* If something went wrong, then we need to return negative value to
	 * net_if.c:net_if_tx() so that the net_pkt will get released.
	 */
	return ret;
}

static void net_bshbus_iface_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	struct net_bshbus_context *ctx = dev->data;

	ctx->iface = iface;

	LOG_DBG("Init BSHBus interface for %s", dev->name);
}

static int net_bshbus_init(const struct device *dev)
{
	const struct net_bshbus_config *cfg = dev->config;

	if (!device_is_ready(cfg->bshbus_dev)) {
		LOG_ERR("BSHBus device %s not ready", dev->name);
		return -ENODEV;
	}

	return 0;
}

static int net_bshbus_setsockopt(const struct device *dev, void *obj, int level,
				 int optname, const void *optval, socklen_t optlen)
{
	const struct net_bshbus_config *cfg = dev->config;
	struct net_bshbus_context *ctx = dev->data;

	if (level != SOL_BSHBUS_DBUS2) {
		return -EINVAL;
	}

	switch (optname) {
		case BSHBUS_DBUS2_RECEIVER:
			return bshbus_dbus2_add_receiver(cfg->bshbus_dev, net_bshbus_dbus2_recv, ctx);
		case BSHBUS_DBUS2_NODE:
			return bshbus_dbus2_register_node(cfg->bshbus_dev, *(const uint8_t *)optval);
		default:
			LOG_ERR("Invalid option name %d", optname);
			return -EINVAL;
	}

	return -EINVAL;
}

static struct bshbus_api net_bshbus_api = {
	.iface_api.init = net_bshbus_iface_init,
	.send = net_bshbus_send,
	.close = net_bshbus_close,
	.setsockopt = net_bshbus_setsockopt,
};

static struct net_bshbus_context net_bshbus_ctx;

static const struct net_bshbus_config net_bshbus_cfg = {
	.bshbus_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_bshbus))
};

NET_DEVICE_INIT(net_bshbus, "NET_BSHBUS", net_bshbus_init, NULL, &net_bshbus_ctx,
		&net_bshbus_cfg, CONFIG_NET_BSHBUS_INIT_PRIORITY, &net_bshbus_api, BSHBUS_RAW_L2,
		NET_L2_GET_CTX_TYPE(BSHBUS_RAW_L2), 255); //TODO MTU
