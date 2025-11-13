/*
 * Copyright (c) 2025 Wolfgang Birkner
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(net_l2_bshbus, CONFIG_BSHBUS_LOG_LEVEL);

#include <zephyr/net/net_core.h>
#include <zephyr/net/net_l2.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/bshbus.h>

static inline enum net_verdict bshbus_recv(struct net_if *iface,
					   struct net_pkt *pkt)
{
	memset(net_pkt_lladdr_src(pkt)->addr, 0, sizeof(net_pkt_lladdr_src(pkt)->addr));
	net_pkt_lladdr_src(pkt)->len = 0U;
	net_pkt_lladdr_src(pkt)->type = NET_LINK_BSHBUS_RAW;
	memset(net_pkt_lladdr_dst(pkt)->addr, 0, sizeof(net_pkt_lladdr_dst(pkt)->addr));
	net_pkt_lladdr_dst(pkt)->len = 0U;
	net_pkt_lladdr_dst(pkt)->type = NET_LINK_BSHBUS_RAW;

	net_pkt_set_family(pkt, AF_BSHBUS);

	return NET_CONTINUE;
}

static inline int bshbus_send(struct net_if *iface, struct net_pkt *pkt)
{
	const struct bshbus_api *api = net_if_get_device(iface)->api;
	const struct device *dev = net_if_get_device(iface);
	int ret;

	if (!api) {
		LOG_ERR("No entry for BSHBus API");
		return -ENOENT;
	}

	ret = net_l2_send(api->send, dev, iface, pkt);
	if (!ret) {
		ret = net_pkt_get_len(pkt);
		net_pkt_unref(pkt);
	}
	else {
		LOG_ERR("%s send request failed: %d", dev->name, ret);
	}

	return ret;
}

NET_L2_INIT(BSHBUS_RAW_L2, bshbus_recv, bshbus_send, NULL, NULL);
