#ifndef ZEPHYR_INCLUDE_NET_SOCKET_BSHBUS_PRIV_H_
#define ZEPHYR_INCLUDE_NET_SOCKET_BSHBUS_PRIV_H_

#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief BSH Bus protocol structure.
 */
struct bshbus_proto {
	/** Protocol receiver register function. */
	int (*register_receiver)(void **proto_receiver, void *proto_data);
	/** Protocol receiver unregister function. */
	int (*unregister_receiver)(void *proto_receiver);
};

#ifdef __cplusplus
}
#endif

#endif
