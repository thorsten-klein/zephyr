/**
 * Copyright 2023-2024 NXP
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file nxp_wifi_shell.c
 * Wi-Fi driver shell adapter layer
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

#include "nxp_wifi_drv.h"

static struct nxp_wifi_dev *nxp_wifi;

void nxp_wifi_shell_register(struct nxp_wifi_dev *dev)
{
	/* only one instance supported */
	if (nxp_wifi) {
		return;
	}

	nxp_wifi = dev;
}

static int nxp_wifi_shell_cmd(const struct shell *sh, size_t argc, char **argv)
{
	if (nxp_wifi == NULL) {
		shell_print(sh, "no nxp_wifi device registered");
		return -ENOEXEC;
	}

	if (argc < 2) {
		shell_help(sh);
		return -ENOEXEC;
	}

	k_mutex_lock(&nxp_wifi->mutex, K_FOREVER);

	nxp_wifi_request(argc, argv);

	k_mutex_unlock(&nxp_wifi->mutex);

	return 0;
}

SHELL_CMD_ARG_REGISTER(nxp_wifi, NULL, "NXP Wi-Fi commands (Use help)", nxp_wifi_shell_cmd, 2,
		       CONFIG_SHELL_ARGC_MAX);

#ifdef CONFIG_NXP_WIFI_MANUAL_INIT

#include <zephyr/net/dhcpv4.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(nxp_wifi_manual_init, CONFIG_WIFI_LOG_LEVEL);

static bool s_nxp_wifi_manual_initialized;
static struct net_mgmt_event_callback s_nxp_wifi_connect_cb;

/**
 * @brief NET_EVENT_WIFI_CONNECT_RESULT handler.
 *
 * Starts DHCPv4 after a successful STA association. This is the correct
 * point to call net_dhcpv4_start() because:
 *  - The interface is fully associated and ready to send/receive frames.
 *  - With CONFIG_WIFI_NM_WPA_SUPPLICANT=y, Zephyr forces
 *    NET_DHCPV4_RESTART_ON_IF_UP=n, so net_if_up() alone does NOT
 *    trigger DHCP. The driver must call net_dhcpv4_start() explicitly
 *    after a successful association.
 */
static void nxp_wifi_connect_event_handler(struct net_mgmt_event_callback *cb,
					   uint64_t event,
					   struct net_if *iface)
{
	if (event != NET_EVENT_WIFI_CONNECT_RESULT) {
		return;
	}

	const struct wifi_status *status = (const struct wifi_status *)cb->info;

	if (status->status != 0) {
		LOG_WRN("Wi-Fi connection failed (status %d) - DHCP not started",
			status->status);
		return;
	}

	LOG_INF("Wi-Fi connected - starting DHCPv4");
	net_dhcpv4_start(iface);
}

/**
 * @brief Shell command: wifi_init
 *
 * Loads the NXP Wi-Fi firmware, starts the WLAN subsystem, brings the
 * STA network interface up, and registers a connect-result callback that
 * calls net_dhcpv4_start() after a successful association.
 *
 * Usage:
 *   uart:~$ wifi_init
 *   uart:~$ wifi connect -s <SSID> -k <security> -p <password>
 */
static int cmd_nxp_wifi_init(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	struct net_if *iface;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (s_nxp_wifi_manual_initialized) {
		shell_print(sh, "Wi-Fi already initialized.");
		return 0;
	}

	/* Step 1: load firmware */
	shell_print(sh, "Loading Wi-Fi firmware...");
	ret = nxp_wifi_wlan_init();
	if (ret) {
		shell_error(sh, "nxp_wifi_wlan_init() failed: %d", ret);
		return ret;
	}
	shell_print(sh, "Firmware loaded.");

	/* Step 2: start WLAN subsystem.
	 *
	 * nxp_wifi_wlan_start() blocks until WLAN_REASON_INITIALIZED, which
	 * is posted only after net_wlan_init() has run in the WLCMGR thread.
	 * net_wlan_init() calls net_if_set_link_addr() so the MAC address is
	 * guaranteed to be set when this call returns.
	 */
	shell_print(sh, "Starting WLAN subsystem...");
	ret = nxp_wifi_wlan_start();
	if (ret) {
		shell_error(sh, "nxp_wifi_wlan_start() failed: %d", ret);
		return ret;
	}
	shell_print(sh, "WLAN subsystem started.");

	/* Step 3: get the STA network interface */
	iface = net_if_get_wifi_sta();
	if (!iface) {
		shell_error(sh, "STA network interface not found.");
		return -ENODEV;
	}

	/*
	 * Step 4: register connect-result callback BEFORE bringing the
	 * interface up so we never miss the event.
	 */
	net_mgmt_init_event_callback(&s_nxp_wifi_connect_cb,
				     nxp_wifi_connect_event_handler,
				     NET_EVENT_WIFI_CONNECT_RESULT);
	net_mgmt_add_event_callback(&s_nxp_wifi_connect_cb);

	/*
	 * Step 5: bring the STA interface up.
	 * NET_IF_NO_AUTO_START was set at init time to prevent
	 * net_if_post_init() from calling net_if_up() before the MAC
	 * address was set. Clear it now and bring the interface up manually.
	 */
	net_if_flag_clear(iface, NET_IF_NO_AUTO_START);
	ret = net_if_up(iface);
	if (ret && ret != -EALREADY) {
		shell_error(sh, "net_if_up(STA) failed: %d", ret);
		return ret;
	}
#ifdef CONFIG_NXP_WIFI_SOFTAP_SUPPORT
	/* Step 6: bring the UAP interface up too */
	{
		struct net_if *uap_iface = net_if_get_wifi_sap();

		if (uap_iface) {
			net_if_flag_clear(uap_iface, NET_IF_NO_AUTO_START);
			ret = net_if_up(uap_iface);
			if (ret && ret != -EALREADY) {
				shell_error(sh, "net_if_up(UAP) failed: %d", ret);
				/* non-fatal - STA still works */
			}
		}
	}
#endif

	s_nxp_wifi_manual_initialized = true;

	shell_print(sh, "Wi-Fi initialized. Use 'wifi connect' to join an AP.");
	shell_print(sh, "DHCPv4 will start automatically after association.");
	return 0;
}

SHELL_CMD_REGISTER(wifi_init, NULL,
		   "Initialize NXP Wi-Fi (load FW, start WLAN, bring iface up). "
		   "Requires CONFIG_NXP_WIFI_MANUAL_INIT=y.",
		   cmd_nxp_wifi_init);

#endif /* CONFIG_NXP_WIFI_MANUAL_INIT */
