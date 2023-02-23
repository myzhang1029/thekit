/*
 *  wifi.c
 *  Copyright (C) 2022 Zhang Maiyun <me@myzhangll.xyz>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "thekit4_pico_w.h"

#include "pico/cyw43_arch.h"

#if ENABLE_WATCHDOG
#include "hardware/watchdog.h"
#endif

#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/apps/mdns.h"

extern cyw43_t cyw43_state;

static void register_mdns(void) {
    cyw43_arch_lwip_begin();
    mdns_resp_init();
    mdns_resp_add_netif(&WIFI_NETIF, HOSTNAME);
    cyw43_arch_lwip_end();
}

static void print_ip(void) {
    printf("IP Address: %s\n", ipaddr_ntoa(&WIFI_NETIF.ip_addr));
}

static void print_and_check_dns(void) {
    const ip_addr_t *pdns = dns_getserver(0);
    printf("DNS Server: %s\n", ipaddr_ntoa(pdns));
    if (FORCE_DEFAULT_DNS || ip_addr_eq(pdns, &ip_addr_any)) {
        ip_addr_t default_dns;
        printf("Reconfiguing DNS server to %s\n", DEFAULT_DNS);
        ipaddr_aton(DEFAULT_DNS, &default_dns);
        dns_setserver(0, &default_dns);
    }
}

/// Connect to Wi-Fi
bool wifi_connect(void) {
    int n_configs = sizeof(wifi_config) / sizeof(struct wifi_config_entry);
    for (int i = 0; i < n_configs; ++i) {
        printf("Attempting Wi-Fi %s\n", wifi_config[i].ssid);
#if ENABLE_WATCHDOG
        watchdog_update();
#endif
        int result = cyw43_arch_wifi_connect_timeout_ms(
            wifi_config[i].ssid,
            wifi_config[i].password,
            wifi_config[i].auth,
            5000
        );
#if ENABLE_WATCHDOG
        watchdog_update();
#endif
        if (result == 0) {
            print_ip();
            print_and_check_dns();
            register_mdns();
            return true;
        }
        printf("Failed with status %d\n", result);
    }
    puts("WARNING: Cannot connect to Wi-Fi");
    return false;
}
