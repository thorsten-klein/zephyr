#ifndef ZEPHYR_INCLUDE_NET_SOCKET_BSHBUS_H_
#define ZEPHYR_INCLUDE_NET_SOCKET_BSHBUS_H_

#include <zephyr/types.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_if.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Protocols of the protocol family PF_BSHBUS */
enum bshbus_proto_id {
	BSHBUS_DBUS2 = 0, /**< BSH D-Bus-2 protocol */
	BSHBUS_PROTO_MAX  /**< Used internally */
};

/* SocketCAN options */
#define SOL_BSHBUS_BASE 100
#define SOL_BSHBUS_DBUS2 (SOL_BSHBUS_BASE + BSHBUS_DBUS2)

enum {
	BSHBUS_DBUS2_RECEIVER = 1,
	BSHBUS_DBUS2_NODE = 2,
};

/**
 * struct sockaddr_bshbus - The sockaddr structure for BSH Bus sockets
 *
 */
struct sockaddr_bshbus {
	sa_family_t bshbus_family; /**< Address family */
	int bshbus_ifindex; /**< SocketBSHBus network interface index */
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NET_SOCKET_BSHBUS_H_ */
