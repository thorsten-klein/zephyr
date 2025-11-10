# SPDX-License-Identifier: Apache-2.0

zephyr_syscall_header(${ZEPHYR_BASE}/include/zephyr/drivers/bshbus.h)

zephyr_library()

# BSH Bus subsystem common files
# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_BSHBUS                  bshbus_common.c)
zephyr_library_sources_ifdef(CONFIG_BSHBUS_SHELL            bshbus_shell.c)
#zephyr_library_sources_ifdef(CONFIG_USERSPACE               can_handlers.c)
# zephyr-keep-sorted-stop

# BSH Bus subsystem driver files
# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_BSHBUS_DUMMY            bshbus_dummy.c)
zephyr_library_sources_ifdef(CONFIG_BSHBUS_TIDBUS           bshbus_tidbus.c)
# zephyr-keep-sorted-stop

#add_subdirectory(transceiver)
