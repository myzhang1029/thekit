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

#include <time.h>

#include "pico/stdlib.h"

#include "hardware/uart.h"
#include "hardware/rtc.h"

#if ENABLE_GPS

static struct gps_status {
    // `true` if RMC gives 'A'
    bool gps_valid;
    // `true` if GGA, RMC, or ZDA gives a valid time
    bool gps_time_valid;
    // North is positive, South is negative
    float gps_lat;
    // East is positive, West is negative
    float gps_lon;
    // Altitude in meters
    float gps_alt;
    // Number of satellites used in position fix
    uint8_t gps_sat_num;
    uint8_t utc_hour;
    uint8_t utc_min;
    float utc_sec;
    uint16_t utc_year;
    uint8_t utc_month;
    uint8_t utc_day;
    // Maximum length of a sentence that we care about plus some headroom
    // '$GNGGA,000000.000000,00000.000000,N,00000.000000,W,1,99,1.5,00000.000,M,00000.000,M,00.0,0000,*4D'
    // '$' is never included; the first character is the first character of the sentence type
    // The parser is immediately called once a newline is received.
    char buffer[100];
    // Current position in buffer (also length used)
    uint8_t buffer_pos;
    // Whether we are currently in a sentence
    bool in_sentence;
} gps_status = {
    .gps_valid = false,
    .gps_time_valid = false,
    .gps_lat = 0,
    .gps_lon = 0,
    .gps_alt = 0,
    .gps_sat_num = 0,
    .utc_hour = 0,
    .utc_min = 0,
    .utc_sec = 0,
    .utc_year = 0,
    .utc_month = 0,
    .utc_day = 0,
    .buffer = {0},
    .buffer_pos = 0,
    .in_sentence = false,
};

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
    gpio_init(GPS_PPS_PIN);
    gpio_set_dir(GPS_PPS_PIN, GPIO_IN);
    // Enable GPS
    gpio_put(GPS_EN_PIN, 1);
}

/// Get the current time in UTC
bool gps_get_time(datetime_t *dt) {
    if (!gps_status.gps_time_valid) {
        return false;
    }
    struct tm intermediate;
    intermediate.tm_year = gps_status.utc_year + 100;
    intermediate.tm_mon = gps_status.utc_month - 1;
    intermediate.tm_mday = gps_status.utc_day;
    intermediate.tm_hour = gps_status.utc_hour;
    intermediate.tm_min = gps_status.utc_min;
    intermediate.tm_sec = (int)gps_status.utc_sec;
    intermediate.tm_isdst = -1;
    // `mktime` ignores `wday` and `yday` which we don't have
    time_t t = mktime(&intermediate);
    gmtime_r(&t, &intermediate);
    dt->year = intermediate.tm_year + 1900;
    dt->month = intermediate.tm_mon + 1;
    dt->day = intermediate.tm_mday;
    dt->hour = intermediate.tm_hour;
    dt->min = intermediate.tm_min;
    dt->sec = intermediate.tm_sec;
    dt->dotw = intermediate.tm_wday;
    return true;
}

/// Get the current GPS position
bool gps_get_location(float *lat, float *lon, float *alt) {
    if (!gps_status.gps_valid) {
        return false;
    }
    *lat = gps_status.gps_lat;
    *lon = gps_status.gps_lon;
    *alt = gps_status.gps_alt;
    return true;
}

/// Get the number of satellites used in the current position fix
uint8_t gps_get_sat_num(void) {
    return gps_status.gps_sat_num;
}

static bool parse_sentence(char *buffer, uint8_t buffer_len);

/// Read whatever is in the UART, and parse it if it's a sentence
void gps_parse_available(void) {
    while (uart_is_readable(GPS_UART)) {
        char c = uart_getc(GPS_UART);
        if (c == '$') {
            // Start of a sentence
            gps_status.in_sentence = true;
            gps_status.buffer_pos = 0;
            continue;
        }
        if (!gps_status.in_sentence) {
            // Then nothing matters
            continue;
        }
        if (c == '\r' || c == '\n') {
            gps_status.in_sentence = false;
            if (gps_status.buffer_pos > 0) {
                gps_status.buffer[gps_status.buffer_pos] = '\0';
                bool result = parse_sentence(gps_status.buffer, gps_status.buffer_pos);
#ifndef NDEBUG
                if (!result) {
                    printf("Bad sentence: %s\n", gps_status.buffer);
                } else {
                    printf("GPS parsed: %s\n", gps_status.buffer);
                }
#endif
            }
        // Check for buffer overflow
        } else if (gps_status.buffer_pos < sizeof(gps_status.buffer) - 1) {
            gps_status.buffer[gps_status.buffer_pos++] = c;
        } else {
            // Buffer overflow
            gps_status.in_sentence = false;
        }
    }
}

static void determine_time_validity(void) {
    gps_status.gps_time_valid = (
        gps_status.utc_year > 1000
    );
}


/// Returns whether the current sentence is used
/// Currently, we care about the following sentences:
/// - GGA: type = 0
/// - GLL: type = 1
/// - RMC: type = 2
/// - ZDA: type = 3
static bool parse_sentence(char *buffer, uint8_t buffer_len) {
    // XOR everything until the asterisk
    uint8_t checksum = 0;
    uint8_t cursor = 0;
    // At least six characters
    if (buffer_len < 6) {
        return false;
    }
    // The first two don't matter
    checksum ^= buffer[cursor++];
    checksum ^= buffer[cursor++];
    char type0, type1, type2, type;
    checksum ^= (type0 = buffer[cursor++]);
    checksum ^= (type1 = buffer[cursor++]);
    checksum ^= (type2 = buffer[cursor++]);
    // Check the type
    switch (type0) {
        case 'G':
            if (type1 == 'G' && type2 == 'A') {
                type = 0;
            } else if (type1 == 'L' && type2 == 'L') {
                type = 1;
            } else {
                return false;
            }
            break;
        case 'R':
            if (type1 == 'M' && type2 == 'C') {
                type = 2;
            } else {
                return false;
            }
            break;
        case 'Z':
            if (type1 == 'D' && type2 == 'A') {
                type = 3;
            } else {
                return false;
            }
            break;
        default:
            return false;
    }
    char comma;
    checksum ^= (comma = buffer[cursor++]);
    if (comma != ',') {
        return false;
    }
    switch (type) {
        case 0:
        {
            uint8_t hour, min;
            float sec;
            float lat, lon;
            uint8_t fix_quality;
            uint8_t num_satellites;
            float hdop;
            float altitude;
            float geoid_sep;
            bool result = gpsutil_parse_sentence_gga(
                checksum, cursor, gps_status.buffer, buffer_len,
                &hour, &min, &sec, &lat, &lon, &fix_quality, &num_satellites,
                &hdop, &altitude, &geoid_sep);
            if (result) {
                gps_status.gps_lat = lat;
                gps_status.gps_lon = lon;
                gps_status.gps_valid = fix_quality > 0;
                gps_status.gps_alt = altitude;
                gps_status.gps_sat_num = num_satellites;
                gps_status.utc_hour = hour;
                gps_status.utc_min = min;
                gps_status.utc_sec = sec;
                determine_time_validity();
            }
            return result;
        }
        case 1:
        {
            uint8_t hour, min;
            float sec;
            float lat, lon;
            bool valid, included_time;
            bool result = gpsutil_parse_sentence_gll(
                checksum, cursor, gps_status.buffer, buffer_len,
                &hour, &min, &sec, &lat, &lon, &valid, &included_time);
            if (result) {
                gps_status.gps_lat = lat;
                gps_status.gps_lon = lon;
                gps_status.gps_valid = valid;
                if (included_time) {
                    gps_status.utc_hour = hour;
                    gps_status.utc_min = min;
                    gps_status.utc_sec = sec;
                    determine_time_validity();
                }
            }
        }
        case 2:
        {
            uint8_t hour, min;
            float sec;
            float lat, lon;
            bool valid;
            bool result = gpsutil_parse_sentence_rmc(
                checksum, cursor, gps_status.buffer, buffer_len,
                &hour, &min, &sec, &lat, &lon, &valid);
            if (result) {
                gps_status.gps_valid = valid;
                gps_status.gps_lat = lat;
                gps_status.gps_lon = lon;
                gps_status.utc_hour = hour;
                gps_status.utc_min = min;
                gps_status.utc_sec = sec;
                determine_time_validity();
            }
            return result;
        }
        case 3:
        {
            // XXX: make use of TZ in ZDA
            uint8_t hour, min;
            float sec;
            uint16_t year;
            uint8_t month, day;
            uint8_t zone_hour, zone_min;
            bool result = gpsutil_parse_sentence_zda(
                checksum, cursor, gps_status.buffer, buffer_len,
                &hour, &min, &sec, &year, &month, &day, &zone_hour, &zone_min);
            if (result) {
                gps_status.utc_hour = hour;
                gps_status.utc_min = min;
                gps_status.utc_sec = sec;
                gps_status.utc_year = year;
                gps_status.utc_month = month;
                gps_status.utc_day = day;
                determine_time_validity();
            }
            return result;
        }
        default:
            // unreachable
            return false;
    }
}

#endif
