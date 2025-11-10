/*
 * Copyright (c) 2025 BSH Hausgeraete
 * Copyright (c) 2025 Wolfgang Birkner
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file
 *  @brief BSH Bus socket API definitions.
 */

#ifndef ZEPHYR_INCLUDE_NET_BSHBUS_H_
#define ZEPHYR_INCLUDE_NET_BSHBUS_H_

#include <zephyr/types.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_if.h>
#include <zephyr/drivers/bshbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * BSH Bus L2 network driver API.
 */
struct bshbus_api {
	/**
	 * The net_if_api must be placed in first position in this
	 * struct so that we are compatible with network interface API.
	 */
	struct net_if_api iface_api;

	/** Send a BSH Bus packet by socket */
	int (*send)(const struct device *dev, struct net_pkt *pkt);

	/** Close the related BSH Bus socket */
	void (*close)(const struct device *dev, int filter_id);

	/** Set socket BSH Bus option */
	int (*setsockopt)(const struct device *dev, void *obj, int level,
			  int optname,
			  const void *optval, socklen_t optlen);

	/** Get socket BSH Bus option */
	int (*getsockopt)(const struct device *dev, void *obj, int level,
			  int optname,
			  const void *optval, socklen_t *optlen);
};

/* Make sure that the network interface API is properly setup inside
 * BSH Bus API struct (it is the first one).
 */
BUILD_ASSERT(offsetof(struct bshbus_api, iface_api) == 0);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NET_BSHBUS_H_ */
