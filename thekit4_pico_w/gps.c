/*
 *  gps.c
 *  Copyright (C) 2023 Zhang Maiyun <me@myzhangll.xyz>
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
#include "gps_util.h"

#include <time.h>

#include "pico/stdlib.h"

#include "hardware/uart.h"
#include "hardware/rtc.h"

#if ENABLE_GPS

static struct gps_status gps_status = GPS_STATUS_INIT;

volatile timestamp_t last_gps_rtc_update = 0;

static void gps_update_rtc(uint gpio, uint32_t event_mask) {
    if (gpio != GPS_PPS_PIN || !(event_mask & PPS_EDGE_TYPE))
        return;
    struct tm intermediate;
    time_t t;
    timestamp_t timestamp, now;
    datetime_t dt;
    now = timestamp_micros();
    // This hopefully doesn't interrupt an update in progress
    // But even if it does, it's not a big deal because
    // the age comparison will reject it
    if (!gpsutil_get_time(&gps_status, &t, &timestamp)) {
        // Reject invalid time
        return;
    }
    if (now - timestamp > 1000000) {
        // Reject if the previous time update happened more than 1 second ago
        return;
    }
    // Timezone conversion
    t += TZ_DIFF_SEC;
    // Re-entrant gmtime is important
    gmtime_r(&t, &intermediate);
    dt.year = intermediate.tm_year + 1900;
    dt.month = intermediate.tm_mon + 1;
    dt.day = intermediate.tm_mday;
    dt.hour = intermediate.tm_hour;
    dt.min = intermediate.tm_min;
    dt.sec = intermediate.tm_sec;
    dt.dotw = intermediate.tm_wday;
    // The docs didn't say this function's re-entry safety
    if (light_update_rtc_and_register_next_alarm(&dt)) {
        last_gps_rtc_update = now;
    }
    // Updating the stratum is taken care of by ntp.c
}

void gps_init(void) {
    uart_init(GPS_UART, GPS_BAUD);
    gpio_set_function(GPS_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(GPS_RX_PIN, GPIO_FUNC_UART);
    // We don't need TX
    // Turn off flow control CTS/RTS
    uart_set_hw_flow(GPS_UART, false, false);
    // We cannot use the interrupt because we will be accessing shared data.
    // Set up EN and PPS
    gpio_init(GPS_EN_PIN);
    gpio_set_dir(GPS_EN_PIN, GPIO_OUT);
    gpio_set_irq_enabled_with_callback(GPS_PPS_PIN, PPS_EDGE_TYPE, true, gps_update_rtc);
    // Enable GPS
    gpio_put(GPS_EN_PIN, 1);
}

bool gps_get_location(float *lat, float *lon, float *alt, timestamp_t *age) {
    timestamp_t timestamp, now;
    now = timestamp_micros();
    if (!gpsutil_get_location(&gps_status, lat, lon, alt, &timestamp)) {
        return false;
    }
    *age = now - timestamp;
    return true;
}

uint8_t gps_get_sat_num(void) {
    return gps_status.gps_sat_num;
}

/// Read whatever is in the UART, and parse it if it's a sentence
void gps_parse_available(void) {
    while (uart_is_readable(GPS_UART)) {
        char c = uart_getc(GPS_UART);
        gpsutil_feed(&gps_status, c);
    }
}


#endif
