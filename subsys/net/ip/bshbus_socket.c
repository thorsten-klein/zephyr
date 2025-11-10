/** @file
 * @brief BSHBus Sockets related functions
 */

/*
 * Copyright (c) 2025 Wolfgang Birkner
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_sockets_bshbus, CONFIG_NET_SOCKETS_LOG_LEVEL);

#include <errno.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/socket_bshbus.h>

#include "connection.h"

enum net_verdict net_bshbus_socket_input(struct net_pkt *pkt)
{
	__ASSERT_NO_MSG(net_pkt_family(pkt) == AF_BSHBUS);

	if (net_if_l2(net_pkt_iface(pkt)) == &NET_L2_GET_NAME(BSHBUS_RAW)) {
		return net_conn_bshbus_input(pkt, BSHBUS_DBUS2);
	}

	return NET_CONTINUE;
}
