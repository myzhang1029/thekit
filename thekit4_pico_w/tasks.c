/*
 *  tasks.c
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

#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#define HTTP_DEFAULT_PORT 80

struct http_request_data {
    char *content;
    uint16_t port;
    bool free_path;
    bool sent;
    struct tcp_pcb *conn;
};

absolute_time_t next_task_time;

static const char *REQUEST_FMT = "GET %s HTTP/1.0\r\n"
    "Host: %s\r\n\r\n";


static err_t http_client_close(struct tcp_pcb *conn, struct tcp_pcb *tpcb) {
    if (tpcb == NULL)
        return ERR_OK;
    err_t err = ERR_OK;
    tcp_arg(tpcb, NULL);
    tcp_recv(tpcb, NULL);
    tcp_err(tpcb, NULL);
    err = tcp_close(tpcb);
    if (err != ERR_OK) {
        printf("Close failed (%d), calling abort\n", err);
        tcp_abort(tpcb);
        err = ERR_ABRT;
    }
    tcp_close(conn);
    return err;
}

static err_t tcp_recv_cb(void *_arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (p == NULL) {
        puts("Remote closed connection");
        // In this case client is still closed by connect_cb
        return ERR_OK;
    }
    if (err != ERR_OK) {
      printf("recv error: %d\n", err);
      pbuf_free(p);
      return err;
    }
    cyw43_arch_lwip_check();
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t tcp_connect_cb(void *cb_arg, struct tcp_pcb *tpcb, err_t err) {
    struct http_request_data *req_data = (struct http_request_data *)cb_arg;
    size_t size = strlen(req_data->content);
    if (err != ERR_OK) {
      printf("connect err: %d\n", err);
      goto finally;
    }
    cyw43_arch_lwip_check();
    tcp_recv(tpcb, tcp_recv_cb);
    assert(size < tcp_sndbuf(tpcb));
    err = tcp_write(tpcb, req_data->content, size, 0);
    if (err != ERR_OK) {
      printf("write err: %d\n", err);
      goto finally;
    }
    err = tcp_output(tpcb);
    if (err != ERR_OK) {
      printf("output err: %d\n", err);
      goto finally;
    }
finally:
    http_client_close(req_data->conn, tpcb);
    free(req_data->content);
    free(req_data);
    return err;
}

static void do_send_http(const char *name, const ip_addr_t *ipaddr, void *cb_arg) {
    struct http_request_data *req_data = (struct http_request_data *)cb_arg;
    err_t err;
    assert(req_data != NULL);
    if (ipaddr == NULL) {
        puts("DNS gave no result");
        goto fail;
    }
    // `do_send_http` may be called by non-callback when DNS return cached results
    cyw43_arch_lwip_begin();
    struct tcp_pcb *conn = tcp_new_ip_type(IP_GET_TYPE(ipaddr));
    cyw43_arch_lwip_end();
    // To be closed by connect_cb
    req_data->conn = conn;
    if (conn == NULL) {
        puts("Cannot create TCP PCB");
        goto fail;
    }
    cyw43_arch_lwip_begin();
    tcp_arg(conn, req_data);
    err = tcp_connect(conn, ipaddr, req_data->port, tcp_connect_cb);
    cyw43_arch_lwip_end();
    if (err != ERR_OK) {
        printf("Cannot connect: %d\n", err);
        goto fail;
    }
    // If succeeded, req_data and content should be free()d by connect_cb
    return;
fail:
    free(req_data->content);
    free(req_data);
}

/// Send a GET request. We don't really care about the response
/// since there is no retrying anyways
/// hostname: HTTP hostname
/// path: HTTP path (e.g. /index)
/// port: use 80
/// free_path: whether `path` should be free()d
static bool send_http_request_dns(const char *hostname, const char *path, uint16_t port, bool free_path) {
    struct http_request_data *req_data = malloc(sizeof(struct http_request_data));
    if (req_data == NULL) {
        puts("Cannot allocate request data");
        return false;
    }
    size_t size = snprintf(NULL, 0, REQUEST_FMT, path, hostname) + 1;
    // free()d in connect_cb
    char *content = malloc(size);
    if (content == NULL) {
        puts("Cannot allocate request");
        free(req_data);
        return false;
    }
    snprintf(content, size, REQUEST_FMT, path, hostname);
    req_data->content = content;
    req_data->port = port;
    ip_addr_t cached_result;
    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(hostname, &cached_result, do_send_http, req_data);
    cyw43_arch_lwip_end();
    if (err == ERR_OK) {
        // Has cached DNS result
        do_send_http(hostname, &cached_result, req_data);
    }
    else if (err != ERR_INPROGRESS) {
        printf("Cannot do a DNS request: %d\n", err);
        free(content);
        free(req_data);
    }
    // If succeeded, content and req_data should be free()d in do_send_http
    return true;
}

#if ENABLE_DDNS
static bool send_ddns(void) {
    char uri[DDNS_URI_BUFSIZE];
    char addr[IPADDR_STRLEN_MAX];
    assert(ipaddr_ntoa_r(&WIFI_NETIF.ip_addr, addr, IPADDR_STRLEN_MAX));
    snprintf(uri, DDNS_URI_BUFSIZE, DDNS_URI, DDNS_HOSTNAME, DDNS_KEY, addr);
    puts("Sending DDNS");
    bool result = send_http_request_dns(DDNS_HOST, uri, HTTP_DEFAULT_PORT, true);
    return result;
}
#endif

#if ENABLE_TEMPERATURE_SENSOR
static bool send_temperature(void) {
    float temperature = temperature_measure();
    char uri[WOLFRAM_URI_BUFSIZE];
    snprintf(uri, WOLFRAM_URI_BUFSIZE, WOLFRAM_URI, WOLFRAM_DATABIN_ID, temperature);
    puts("Sending temperature");
    bool result = send_http_request_dns(WOLFRAM_HOST, uri, HTTP_DEFAULT_PORT, true);
    return result;
}
#endif

void tasks_init(void) {
    dns_init();
    next_task_time = get_absolute_time();
}

bool tasks_check_run(void) {
    if (absolute_time_diff_us(get_absolute_time(), next_task_time) < 0) {
        bool result = true;
#if ENABLE_DDNS
        result &= send_ddns();
#endif
#if ENABLE_TEMPERATURE_SENSOR
        result &= send_temperature();
#endif
        if (!result)
            puts("Tasks failed");
        next_task_time = make_timeout_time_ms(TASKS_INTERVAL_MS);
        return result;
    }
    return true;
}
