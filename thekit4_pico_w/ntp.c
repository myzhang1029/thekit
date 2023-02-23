/*
 *  ntp.c
 *  Heavily refactored from BSD-3-Clause picow_ntp_client.c
 *  Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
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

#include <string.h>
#include <time.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "hardware/rtc.h"

#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#if ENABLE_NTP

static const uint16_t NTP_MSG_LEN = 48;
// Seconds between 1 Jan 1900 and 1 Jan 1970
static const uint32_t NTP_DELTA = 2208988800;
// Time to wait in case UDP requests are lost
static const uint32_t UDP_TIMEOUT_TIME_MS = 10 * 1000;

/// Close this request
static void ntp_req_close(struct ntp_client_current_request *req) {
    if (!req)
        return;
    if (req->pcb) {
        cyw43_arch_lwip_begin();
        udp_remove(req->pcb);
        cyw43_arch_lwip_end();
        req->pcb = NULL;
    }
    if (req->resend_alarm > 0) {
        // We finished, so cancel it
        cancel_alarm(req->resend_alarm);
        req->resend_alarm = 0;
    }
    req->in_progress = false;
}

static void ntp_update_rtc(time_t *result) {
    if (result) {
        time_t lresult = *result + TZ_DIFF_SEC;
        struct tm *lt = gmtime(&lresult);
        datetime_t dt = {
            .year  = lt->tm_year + 1900,
            .month = lt->tm_mon + 1,
            .day = lt->tm_mday,
            .dotw = lt->tm_wday,
            .hour = lt->tm_hour,
            .min = lt->tm_min,
            .sec = lt->tm_sec
        };
        printf("Got NTP response: %04d-%02d-%02d %02d:%02d:%02d\n",
               dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec);
        if (rtc_set_datetime(&dt)) {
            time_in_sync = true;
            puts("RTC set");
            // Note that `light_register_next_alarm` modifies `dt` so make sure we don't need it anymore
            light_register_next_alarm(&dt);
        }
    }
}

static int64_t ntp_timeout_alarm_cb(alarm_id_t id, void *user_data)
{
    struct ntp_client_current_request *req = (struct ntp_client_current_request *)user_data;
    puts("NTP request timed out");
    ntp_req_close(req);
    return 0;
}

// NTP data received
static void ntp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    struct ntp_client_current_request *req = (struct ntp_client_current_request *)arg;
    cyw43_arch_lwip_check();
    uint8_t mode = pbuf_get_at(p, 0) & 0x7;
    uint8_t stratum = pbuf_get_at(p, 1);

    // Check the result
    if (ip_addr_cmp(addr, &req->server_address) && port == NTP_PORT && p->tot_len == NTP_MSG_LEN &&
        mode == 0x4 && stratum != 0) {
        uint8_t seconds_buf[4] = {0};
        pbuf_copy_partial(p, seconds_buf, sizeof(seconds_buf), 40);
        uint32_t seconds_since_1900 = seconds_buf[0] << 24 | seconds_buf[1] << 16 | seconds_buf[2] << 8 | seconds_buf[3];
        uint32_t seconds_since_1970 = seconds_since_1900 - NTP_DELTA;
        time_t epoch = seconds_since_1970;
        ntp_update_rtc(&epoch);
    } else {
        puts("Invalid NTP response");
    }
    ntp_req_close(req);
    pbuf_free(p);
}

// Make an NTP request
static void do_send_ntp_request(const char *_hostname, const ip_addr_t *ipaddr, void *arg) {
    struct ntp_client_current_request *req = (struct ntp_client_current_request *)arg;
    if (ipaddr) {
        req->server_address = *ipaddr;
        printf("NTP address %s\n", ipaddr_ntoa(ipaddr));
    }
    else {
        puts("NTP DNS request failed");
        ntp_req_close(req);
    }
    cyw43_arch_lwip_begin();
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, NTP_MSG_LEN, PBUF_RAM);
    uint8_t *payload = (uint8_t *) p->payload;
    memset(payload, 0, NTP_MSG_LEN);
    payload[0] = 0x1b;
    udp_sendto(req->pcb, p, &req->server_address, NTP_PORT);
    pbuf_free(p);
    cyw43_arch_lwip_end();
}

/// Perform initialisation
bool ntp_client_init(struct ntp_client *state) {
    if (!state)
        return false;
    // Meaningful init values
    state->req.in_progress = false;
    state->req.pcb = NULL;
    state->req.resend_alarm = 0;
    // So that next check_run is triggered
    state->next_sync_time = get_absolute_time();
    return true;
}

/// Check and see if the time should be synchronized
void ntp_client_check_run(struct ntp_client *state) {
    if (!state)
        return;
    struct ntp_client_current_request *req = &state->req;
    // `state` is zero-inited so it will always fire on the first time
    if (absolute_time_diff_us(get_absolute_time(), state->next_sync_time) < 0 && !req->in_progress) {
        // Initialize a NTP sync
        req->in_progress = true;
        cyw43_arch_lwip_begin();
        req->pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
        if (!req->pcb) {
            cyw43_arch_lwip_end();
            puts("Failed to create pcb");
            req->in_progress = false;
            return;
        }
        udp_recv(req->pcb, ntp_recv_cb, req);

        // Set alarm to close the connection in case UDP requests are lost
        req->resend_alarm = add_alarm_in_ms(UDP_TIMEOUT_TIME_MS, ntp_timeout_alarm_cb, req, true);

        int err = dns_gethostbyname(NTP_SERVER, &req->server_address, do_send_ntp_request, req);
        cyw43_arch_lwip_end();

        if (err == ERR_OK) {
            // Cached result
            do_send_ntp_request(NTP_SERVER, &req->server_address, req);
        } else if (err != ERR_INPROGRESS) { // ERR_INPROGRESS means expect a callback
            puts("DNS request failed");
            req->pcb = NULL;
            // Now calling `req_close` is safe because it does not call `udp_remove` on NULL
            ntp_req_close(req);
        }
        state->next_sync_time = make_timeout_time_ms(NTP_INTERVAL_MS);
    }
}

#endif
