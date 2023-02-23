/*
 *  http_server.c
 *  Heavily refactored from BSD-3-Clause picow_tcp_server.c
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

/* Lifecycle:
   http_server_open(state)
| -> http_server_accept_cb(state)
   | For each client connect:
   | -> http_conn_recv_cb(conn)
      | -> http_req_check_parse(conn)
        -> http_conn_close(conn)
   | On error: 
   | -> http_conn_err_cb(conn)
   | Or: 
   | -> http_conn_fail(conn)
        -> http_conn_close(conn)
   http_server_close(state)
*/

#include "config.h"
#include "thekit4_pico_w.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#define HTTP_PORT 80

static const char resp_common[] = "\r\nContent-Type: application/json\r\n"
                                  "Content-Length: ";

static const char resp_200_pre[] = "HTTP/1.0 200 OK";
static const char resp_400_pre[] = "HTTP/1.0 400 BAD REQUEST";
static const char resp_400_post[] = "24\r\n\r\n"
                                    "{\"error\": \"bad request\"}";
static const char resp_404_pre[] = "HTTP/1.0 404 NOT FOUND";
static const char resp_404_post[] = "22\r\n\r\n"
                                    "{\"error\": \"not found\"}";
static const char resp_405_pre[] = "HTTP/1.0 405 METHOD NOT ALLOWED";
static const char resp_405_post[] = "31\r\n\r\n"
                                    "{\"error\": \"method not allowed\"}";
static const char resp_500_pre[] = "HTTP/1.0 500 INTERNAL SERVER ERROR";
static const char resp_500_post[] = "24\r\n\r\n"
                                    "{\"error\": \"internal server error\"}";
static const char resp_dashboard[] =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: 4109\r\n\r\n"
#include "dashboard.h"
    ;

static err_t http_conn_close(void *arg) {
    struct http_server_conn *conn = (struct http_server_conn *)arg;
    err_t err = ERR_OK;
    puts("Closing server connection");
    if (conn->client_pcb) {
        cyw43_arch_lwip_begin();
        tcp_arg(conn->client_pcb, NULL);
        tcp_sent(conn->client_pcb, NULL);
        tcp_recv(conn->client_pcb, NULL);
        tcp_err(conn->client_pcb, NULL);
        err = tcp_close(conn->client_pcb);
        if (err != ERR_OK) {
            printf("Close failed (%d), calling abort\n", err);
            tcp_abort(conn->client_pcb);
            err = ERR_ABRT;
        }
        cyw43_arch_lwip_end();
        conn->client_pcb = NULL;
    }
    conn->state = HTTP_OTHER;
    if (conn->received) {
        free(conn->received);
        conn->received = NULL;
    }
    return err;
}

static err_t http_conn_fail(void *arg, int status, const char *function) {
    printf("HTTP server connection failed with status %d at %s\n", status, function);
    return http_conn_close(arg);
}

static void http_conn_err_cb(void *arg, err_t err) {
    if (err != ERR_ABRT)
        http_conn_fail(arg, err, "TCP error callback invoked");
}

static err_t http_conn_write(struct http_server_conn *conn, const char *buf,
                               size_t size, uint8_t copy) {
    struct tcp_pcb *tpcb = conn->client_pcb;
    cyw43_arch_lwip_check();
    assert(size < tcp_sndbuf(tpcb));
    err_t err = tcp_write(tpcb, buf, size, copy);
    if (err != ERR_OK) {
        return http_conn_fail((void *)conn, err, "write");
    }
    return ERR_OK;
}

// `true` means that we can stop reading
static bool http_req_check_parse(struct http_server_conn *conn) {
    cyw43_arch_lwip_check();
    uint16_t offset_newline = pbuf_memfind(conn->received, "\r\n", 2, 0);
    if (offset_newline == 0xffff)
        // Have not received a complete request line yet
        return false;
    uint16_t offset_first_space = pbuf_memfind(conn->received, " ", 1, 0);
    if (offset_first_space == 0xffff || offset_first_space > offset_newline)
        // Invalid request
        goto finish;
    uint16_t offset_path = offset_first_space + 1;
    // Extract method (GET, POST, etc.)
    // Only process GET because I discard the entire body.
    // Note the extra space after GET
    if (pbuf_memcmp(conn->received, 0, "GET ", 4) != 0) {
        http_conn_write(conn, resp_405_pre, sizeof(resp_405_pre) - 1, 0);
        http_conn_write(conn, resp_common, sizeof(resp_common) - 1, 0);
        http_conn_write(conn, resp_405_post, sizeof(resp_405_post) - 1, 0);
        goto finish;
    }
    // We don't use or care about HTTP version anymore
    // Note the space at the end of this path
    if (pbuf_memcmp(conn->received, offset_path, "/ ", 2) == 0 
        // unlikely
        || pbuf_memcmp(conn->received, offset_path, "/\r", 2) == 0) {
        http_conn_write(conn, resp_dashboard, 512, 0);
        http_conn_write(conn, resp_dashboard + 512, 512, 0);
        http_conn_write(conn, resp_dashboard + 1024, 512, 0);
        http_conn_write(conn, resp_dashboard + 1536, 512, 0);
        http_conn_write(conn, resp_dashboard + 2048, 512, 0);
        http_conn_write(conn, resp_dashboard + 2560, 512, 0);
        http_conn_write(conn, resp_dashboard + 3072, 512, 0);
        http_conn_write(conn, resp_dashboard + 3584, 512, 0);
        http_conn_write(conn, resp_dashboard + 4096, sizeof(resp_dashboard) - 4096 - 1, 0);
        goto finish;
    }
    // Note the space at the end of this path
    if (pbuf_memcmp(conn->received, offset_path, "/get_info ", 10) == 0 
        // unlikely
        || pbuf_memcmp(conn->received, offset_path, "/get_info\r", 2) == 0) {
        // Max length + nn\r\n\r\n + \0
        char response[48] = {0};
        size_t length;
#if ENABLE_TEMPERATURE_SENSOR
        float temperature = temperature_measure();
#else
        float temperature = nan("unavailable");
#endif
#if ENABLE_LIGHT
        extern uint16_t current_pwm_level;
#else
        uint16_t current_pwm_level = 0;
#endif
        /* Generate response */
        length = snprintf(response, 48,
                     "41\r\n\r\n{\"temperature\": %.3f, \"pwm\": %u}",
                     temperature, (unsigned)current_pwm_level);
        snprintf(response, 48, "%u\r\n\r\n{\"temperature\": %.3f, \"pwm\": %u}",
                 (unsigned)length - 6, temperature, (unsigned)current_pwm_level);
        http_conn_write(conn, resp_200_pre, sizeof(resp_200_pre) - 1, 0);
        http_conn_write(conn, resp_common, sizeof(resp_common) - 1, 0);
        // This one needs to be copied
        http_conn_write(conn, response, length, 1);
        goto finish;
    }
#if ENABLE_LIGHT
    if (pbuf_memcmp(conn->received, offset_path, "/3light_dim", 11) == 0) {
        uint16_t offset_level = pbuf_memfind(conn->received, "level=", 6, offset_path);
        if (offset_level == 0xffff) {
            http_conn_write(conn, resp_400_pre, sizeof(resp_400_pre) - 1, 0);
            http_conn_write(conn, resp_common, sizeof(resp_common) - 1, 0);
            http_conn_write(conn, resp_400_post, sizeof(resp_400_post) - 1, 0);
            goto finish;
        }
        // Max should be 100.000000 and NUL
        char number[12];
        if (pbuf_copy_partial(conn->received, number, 11, offset_level + 6) == 0) {
            puts("Cannot copy pbuf to string");
            http_conn_write(conn, resp_500_pre, sizeof(resp_500_pre) - 1, 0);
            http_conn_write(conn, resp_common, sizeof(resp_common) - 1, 0);
            http_conn_write(conn, resp_500_post, sizeof(resp_500_post) - 1, 0);
            goto finish;
        }
        number[11] = 0;
        float intensity = atof(number);
        // Max length + nn\r\n\r\n + \0
        char response[37] = {0};
        size_t length;
        light_dim(intensity);
        /* Generate response */
        length = snprintf(response, 37,
                     "30\r\n\r\n{\"dim\": true, \"value\": %.2f}", intensity);
        snprintf(response, 37, "%u\r\n\r\n{\"dim\": true, \"value\": %.2f}",
                 (unsigned)length - 6, intensity);
        http_conn_write(conn, resp_200_pre, sizeof(resp_200_pre) - 1, 0);
        http_conn_write(conn, resp_common, sizeof(resp_common) - 1, 0);
        // This one needs to be copied
        http_conn_write(conn, response, length, 1);
        goto finish;
    }
#endif
    http_conn_write(conn, resp_404_pre, sizeof(resp_404_pre) - 1, 0);
    http_conn_write(conn, resp_common, sizeof(resp_common) - 1, 0);
    http_conn_write(conn, resp_404_post, sizeof(resp_404_post) - 1, 0);

finish:
    // We only need one recv/send cycle, so we simply
    // close the connection here.
    tcp_output(conn->client_pcb);
    pbuf_free(conn->received);
    conn->received = NULL;
    http_conn_close(conn);
    return true;
}

static err_t http_conn_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p,
                           err_t err) {
    struct http_server_conn *conn = (struct http_server_conn *)arg;
    if (!p)
        return http_conn_fail(arg, ERR_OK, "remote disconnected");
    else if (err != ERR_OK) {
        /* cleanup, for unknown reason */
        pbuf_free(p);
        return err;
    }
    // this method is callback from lwIP, so cyw43_arch_lwip_begin is not
    // required, however you can use this method to cause an assertion in debug
    // mode, if this method is called when cyw43_arch_lwip_begin IS needed
    cyw43_arch_lwip_check();
    tcp_recved(tpcb, p->tot_len);
    if (conn->state == HTTP_ACCEPTED) {
        // First chunk
        assert(!conn->received);
        conn->received = p;
    }
    else if (conn->state == HTTP_RECEIVING) {
        // Not first chunk
        pbuf_cat(conn->received, p);
    }
    else {
        // Might be DONE or something else
    }
    if (http_req_check_parse(conn))
        conn->state = HTTP_OTHER;
    return ERR_OK;
}

static err_t http_server_accept_cb(void *arg, struct tcp_pcb *client_pcb,
                                err_t err) {
    struct http_server *state = (struct http_server *)arg;

    cyw43_arch_lwip_check();
    if (err != ERR_OK || client_pcb == NULL) {
        http_conn_fail(arg, err, "accept");
        return ERR_VAL;
    }
    puts("Client connected");
    state->conn.state = HTTP_ACCEPTED;

    state->conn.client_pcb = client_pcb;
    tcp_arg(client_pcb, &state->conn);
    tcp_recv(client_pcb, http_conn_recv_cb);
    tcp_err(client_pcb, http_conn_err_cb);

    return ERR_OK;
}

bool http_server_open(struct http_server *state) {
    if (!state)
        return false;

    // NULL-init `conn`
    state->conn.client_pcb = NULL;
    state->conn.state = HTTP_OTHER;
    state->conn.received = NULL;

    printf("Starting server on port %u\n", HTTP_PORT);

    cyw43_arch_lwip_begin();
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        cyw43_arch_lwip_end();
        puts("Failed to create pcb");
        return false;
    }

    err_t err = tcp_bind(pcb, NULL, HTTP_PORT);
    if (err) {
        cyw43_arch_lwip_end();
        puts("Failed to bind to port");
        return false;
    }

    state->server_pcb = tcp_listen_with_backlog(pcb, 1);
    if (!state->server_pcb) {
        cyw43_arch_lwip_end();
        puts("Failed to listen");
        http_server_close(state);
        return false;
    }

    // Specify the payload for the callbacks
    tcp_arg(state->server_pcb, state);
    tcp_accept(state->server_pcb, http_server_accept_cb);
    cyw43_arch_lwip_end();

    return true;
}

void http_server_close(struct http_server *state) {
    if (state == NULL)
        return;
    http_conn_close(&state->conn);
    if (state->server_pcb) {
        cyw43_arch_lwip_begin();
        tcp_arg(state->server_pcb, NULL);
        tcp_close(state->server_pcb);
        cyw43_arch_lwip_end();
        state->server_pcb = NULL;
    }
}
