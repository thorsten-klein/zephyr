/** @file
 * @brief BSHBus Socket related functions
 *
 * This is not to be included by the application.
 */

/*
 * Copyright (c) 2025 Wolfgang Birkner
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __BSHBUS_SOCKET_H
#define __BSHBUS_SOCKET_H

#include <zephyr/types.h>

/**
 * @brief Called by net_core.c when a BSHBus packet is received.
 *
 * @param pkt Network packet
 *
 * @return NET_OK if the packet was consumed, NET_DROP if
 * the packet parsing failed and the caller should handle
 * the received packet.
 */
#if defined(CONFIG_NET_SOCKETS_BSHBUS)
enum net_verdict net_bshbus_socket_input(struct net_pkt *pkt);
#else
static inline enum net_verdict net_bshbus_socket_input(struct net_pkt *pkt)
{
	return NET_CONTINUE;
}
#endif

#endif /* __BSHBUS_SOCKET_H */
