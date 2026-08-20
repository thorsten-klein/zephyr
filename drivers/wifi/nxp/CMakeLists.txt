# Copyright 2022-2024 NXP
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_WIFI_NXP)

# Compile nxp_wifi_drv.c for auto-init OR manual-init (manual-init still
# needs the driver symbols: nxp_wifi_wlan_init, nxp_wifi_wlan_start, etc.)
if(CONFIG_NXP_WIFI_AUTO_INIT OR CONFIG_NXP_WIFI_MANUAL_INIT)
  zephyr_library_sources(nxp_wifi_drv.c)
endif()

zephyr_library_sources_ifdef(CONFIG_NXP_WIFI_SHELL nxp_wifi_shell.c)
zephyr_library_link_libraries_ifdef(CONFIG_WIFI_NM_WPA_SUPPLICANT hostap)
endif()
