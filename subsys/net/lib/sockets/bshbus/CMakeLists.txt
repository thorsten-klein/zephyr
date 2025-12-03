# SPDX-License-Identifier: Apache-2.0

zephyr_library_include_directories(.)

zephyr_library_sources_ifdef(CONFIG_NET_SOCKETS_BSHBUS                sockets_bshbus.c)
zephyr_library_sources_ifdef(CONFIG_NET_SOCKETS_BSHBUS_DBUS2          bshbus_proto_dbus2.c)
zephyr_library_sources_ifdef(CONFIG_NET_SOCKETS_BSHBUS_DBUS2_DUMMY    bshbus_proto_dummy.c)
