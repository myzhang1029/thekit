/*
 *  gps_util.c
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

//! Yet another ad-hoc GPS NMEA-0183 parser.

#include "gps_util.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#ifdef GPS_UTIL_TEST
#include <assert.h>
#include <string.h>
#define assert_eq(a, b) assert((a) == (b))
#define assert_float_eq(a, b) assert(fabs((a) - (b)) < 1e-5)
#endif

// Everything returns false for invalid input

/// Parse an unsigned integer
static inline bool parse_integer(uint8_t *checksum, uint32_t *cursor, const char *buffer, uint32_t buffer_len, uint32_t *result) {
    uint32_t value = 0;
    char c;

    while (*cursor < buffer_len && isdigit((unsigned char) buffer[*cursor])) {
        *checksum ^= (c = buffer[(*cursor)++]);
        value = value * 10 + (c - '0');
    }

    *result = value;
    return true;
}

#ifdef GPS_UTIL_TEST
static void test_parse_integer(void) {
    uint8_t checksum = 0;
    uint32_t cursor = 0;
    uint32_t result;
    char buffer[] = "12345,";
    uint32_t buffer_len = sizeof(buffer) - 1;
    assert(parse_integer(&checksum, &cursor, buffer, buffer_len, &result));
    assert_eq(result, 12345);
    assert_eq(checksum, 49);
    assert_eq(cursor, 5);
    checksum = 0;
    cursor = 0;
    char buffer2[] = "123456";
    uint32_t buffer2_len = sizeof(buffer2) - 1;
    assert(parse_integer(&checksum, &cursor, buffer2, buffer2_len, &result));
    assert_eq(result, 123456);
    assert_eq(checksum, 7);
    assert_eq(cursor, 6);
}
#endif

/// Parse a floating point number
static inline bool parse_float(uint8_t *checksum, uint32_t *cursor, const char *buffer, uint32_t buffer_len, float *result) {
    uint32_t integer_part = 0;
    uint32_t decimal_part = 0;
    uint32_t decimal_part_len = 0;
    char c;
    bool negative = false;

    if (buffer_len - *cursor >= 1) {
        if ((c = buffer[*cursor]) == '-') {
            *checksum ^= c;
            negative = true;
            (*cursor)++;
        }
    }
    if (!parse_integer(checksum, cursor, buffer, buffer_len, &integer_part)) {
        return false;
    }

    if (*cursor < buffer_len && (c = buffer[*cursor]) == '.') {
        *checksum ^= c;
        (*cursor)++;
        uint32_t decimal_part_cursor = *cursor;
        if (!parse_integer(checksum, cursor, buffer, buffer_len, &decimal_part)) {
            return false;
        }
        decimal_part_len = *cursor - decimal_part_cursor;
    }

    *result = integer_part + decimal_part * powf(10, -(float) decimal_part_len);
    if (negative) {
        *result = -*result;
    }
    return true;
}

#ifdef GPS_UTIL_TEST
static void test_parse_float(void) {
    uint8_t checksum = 0;
    uint32_t cursor = 0;
    float result;
    char buffer[] = "123.456789,";
    uint32_t buffer_len = sizeof(buffer) - 1;
    assert(parse_float(&checksum, &cursor, buffer, buffer_len, &result));
    assert_float_eq(result, 123.456789);
    assert_eq(checksum, 31);
    assert_eq(cursor, 10);
    checksum = 0;
    cursor = 0;
    char buffer2[] = "123456";
    uint32_t buffer2_len = sizeof(buffer2) - 1;
    assert(parse_float(&checksum, &cursor, buffer2, buffer2_len, &result));
    assert_float_eq(result, 123456);
    assert_eq(checksum, 7);
    assert_eq(cursor, 6);
    checksum = 0;
    cursor = 0;
    char buffer3[] = "-123456";
    uint32_t buffer3_len = sizeof(buffer3) - 1;
    assert(parse_float(&checksum, &cursor, buffer3, buffer3_len, &result));
    assert_float_eq(result, -123456);
    assert_eq(checksum, 42);
    assert_eq(cursor, 7);
}
#endif

/// Take a single character.
/// If the following character is a comma or an asterisk, return EOF and the cursor and the checksum are not modified.
static inline int16_t parse_single_char(uint8_t *checksum, uint32_t *cursor, const char *buffer, uint32_t buffer_len) {
    if (*cursor >= buffer_len) {
        return EOF;
    }
    char c = buffer[*cursor];
    if (c == ',' || c == '*') {
        return EOF;
    }
    *checksum ^= c;
    (*cursor)++;
    return c;
}

#ifdef GPS_UTIL_TEST
static void test_parse_single_char(void) {
    uint8_t checksum = 0;
    uint32_t cursor = 0;
    char buffer[] = "12345,";
    uint32_t buffer_len = sizeof(buffer) - 1;
    assert_eq(parse_single_char(&checksum, &cursor, buffer, buffer_len), '1');
    assert_eq(checksum, 49);
    assert_eq(cursor, 1);
    assert_eq(parse_single_char(&checksum, &cursor, buffer, buffer_len), '2');
    assert_eq(checksum, 3);
    assert_eq(cursor, 2);
    assert_eq(parse_single_char(&checksum, &cursor, buffer, buffer_len), '3');
    assert_eq(checksum, 48);
    assert_eq(cursor, 3);
    assert_eq(parse_single_char(&checksum, &cursor, buffer, buffer_len), '4');
    assert_eq(checksum, 4);
    assert_eq(cursor, 4);
    assert_eq(parse_single_char(&checksum, &cursor, buffer, buffer_len), '5');
    assert_eq(checksum, 49);
    assert_eq(cursor, 5);
    assert_eq(parse_single_char(&checksum, &cursor, buffer, buffer_len), EOF);
    assert_eq(checksum, 49);
    assert_eq(cursor, 5);
}
#endif

/// Parse a h?hmmss.?s* string.
static bool parse_hms(uint8_t *checksum, uint32_t *cursor, const char *buffer, uint32_t buffer_len, uint8_t *hour, uint8_t *min, float *sec) {
    uint32_t hms;
    if (!parse_integer(checksum, cursor, buffer, buffer_len, &hms)) {
        return false;
    }
    uint8_t sec_int = hms % 100;
    hms /= 100;
    *min = hms % 100;
    hms /= 100;
    *hour = hms % 100;
    uint16_t sec_frac = 0;
    uint8_t sec_mag = 0;
    char c;
    // Look-ahead for fractional seconds
    c = buffer[(*cursor)++];
    if (c == '.') {
        *checksum ^= c;
        while (*cursor < buffer_len && isdigit((unsigned char) buffer[*cursor])) {
            *checksum ^= (c = buffer[(*cursor)++]);
            sec_frac = sec_frac * 10 + (c - '0');
            sec_mag++;
        }
    } else {
        (*cursor)--;
    }
    *sec = sec_int + sec_frac * powf(10, -(float) sec_mag);
    return true;
}

#ifdef GPS_UTIL_TEST
static void test_parse_hms(void) {
    uint8_t checksum = 0;
    uint32_t cursor = 0;
    uint8_t hour, min;
    float sec;
    char buffer[] = "123456.789";
    uint32_t buffer_len = sizeof(buffer) - 1;
    // Just to confirm my understanding of the length
    assert_eq(buffer_len, 10);
    assert(parse_hms(&checksum, &cursor, buffer, buffer_len, &hour, &min, &sec));
    assert_eq(hour, 12);
    assert_eq(min, 34);
    assert_float_eq(sec, 56.789);
    assert_eq(checksum, 31);
    // Index of the char after the last one (length)
    assert_eq(cursor, buffer_len);
    checksum = 0;
    cursor = 0;
    char buffer2[] = "32432.";
    uint8_t buffer2_len = sizeof(buffer2) - 1;
    assert(parse_hms(&checksum, &cursor, buffer2, buffer2_len, &hour, &min, &sec));
    assert_eq(hour, 3);
    assert_eq(min, 24);
    assert_float_eq(sec, 32.0);
    assert_eq(checksum, 26);
    assert_eq(cursor, buffer2_len);
    checksum = 0;
    cursor = 0;
    char buffer3[] = "132432";
    uint8_t buffer3_len = sizeof(buffer3) - 1;
    assert(parse_hms(&checksum, &cursor, buffer3, buffer3_len, &hour, &min, &sec));
    assert_eq(hour, 13);
    assert_eq(min, 24);
    assert_float_eq(sec, 32.0);
    assert_eq(checksum, 5);
    assert_eq(cursor, buffer3_len);
}
#endif

/// Parse a d?d?dmm.?m* string.
static bool parse_dm(uint8_t *checksum, uint32_t *cursor, const char *buffer, uint32_t buffer_len, uint16_t *deg, float *min) {
    uint32_t dms;
    if (!parse_integer(checksum, cursor, buffer, buffer_len, &dms)) {
        return false;
    }
    uint8_t min_int = dms % 100;
    dms /= 100;
    *deg = dms;
    uint16_t min_frac = 0;
    uint8_t min_mag = 0;
    char c;
    // Look-ahead for the decimal point
    c = buffer[(*cursor)++];
    if (c == '.') {
        // Consume the decimal point
        *checksum ^= c;
        while (*cursor < buffer_len && isdigit((unsigned char) buffer[*cursor])) {
            *checksum ^= (c = buffer[(*cursor)++]);
            min_frac = min_frac * 10 + (c - '0');
            min_mag++;
        }
    } else {
        (*cursor)--;
    }
    *min = min_int + min_frac * powf(10, -(float) min_mag);
    return true;
}

#ifdef GPS_UTIL_TEST
static void test_parse_dm(void) {
    uint8_t checksum = 0;
    uint32_t cursor = 0;
    uint16_t deg;
    float min;
    char buffer[] = "23456.789";
    uint32_t buffer_len = sizeof(buffer) - 1;
    assert_eq(buffer_len, 9);
    assert(parse_dm(&checksum, &cursor, buffer, buffer_len, &deg, &min));
    assert_eq(deg, 234);
    assert_float_eq(min, 56.789);
    assert_eq(checksum, 46);
    assert_eq(cursor, buffer_len);
    checksum = 0;
    cursor = 0;
    char buffer2[] = "32432.";
    uint8_t buffer2_len = sizeof(buffer2) - 1;
    assert(parse_dm(&checksum, &cursor, buffer2, buffer2_len, &deg, &min));
    assert_eq(deg, 324);
    assert_float_eq(min, 32.0);
    assert_eq(checksum, 26);
    assert_eq(cursor, buffer2_len);
}
#endif

/// Parse the trailing '*hh' part of a NMEA sentence.
static inline bool check_checksum(uint8_t checksum, uint32_t cursor, const char *buffer, uint32_t buffer_len) {
    if (cursor + 3 > buffer_len) {
        return false;
    }
    if (buffer[cursor++] != '*') {
        return false;
    }
    static const char hex[] = "0123456789ABCDEF";
    char first = buffer[cursor++];
    char second = buffer[cursor++];
    char real_first = hex[checksum >> 4];
    char real_second = hex[checksum & 0x0F];
    if (first != real_first || second != real_second) {
        return false;
    }
    return true;
}

#ifdef GPS_UTIL_TEST
static void test_check_checksum(void) {
    char buffer[] = "*12";
    uint32_t buffer_len = sizeof(buffer) - 1;
    assert_eq(buffer_len, 3);
    assert(check_checksum(18, 0, buffer, buffer_len));
    assert(!check_checksum(20, 0, buffer, buffer_len));
    char buffer2[] = "*7A";
    uint8_t buffer2_len = sizeof(buffer2) - 1;
    assert(check_checksum(122, 0, buffer2, buffer2_len));
    assert(!check_checksum(123, 0, buffer2, buffer2_len));
}
#endif

static inline void consume_until_checksum(uint8_t *checksum, uint32_t *cursor, const char *buffer, uint32_t buffer_len) {
    while (*cursor < buffer_len) {
        char c = buffer[(*cursor)++];
        if (c == '*') {
            (*cursor)--;
            return;
        }
        *checksum ^= c;
    }
}

#define COMMA_OR_FAIL(cursor) do { \
    checksum ^= (c = buffer[(cursor)++]); \
    if (c != ',') { \
        return false; \
    } \
} while (0)

bool gpsutil_parse_sentence_gga(
    uint8_t checksum, uint32_t cursor, const char *buffer, uint32_t buffer_len,
    uint8_t *hour, uint8_t *min, float *sec,
    float *lat, float *lon,
    uint8_t *fix_quality, uint8_t *num_satellites,
    float *hdop, float *altitude, float *geoid_sep
) {
    // hhmmss.sss,dddmm.mmmmm,[NS],dddmm.mmmmm,[EW],FIX,NSAT,HDOP,ALT,M,MSL,M,AGE,STID
    if (!parse_hms(&checksum, &cursor, buffer, buffer_len, hour, min, sec)) {
        return false;
    }
    char c;
    COMMA_OR_FAIL(cursor);
    uint16_t deg;
    float min_parser;
    if (!parse_dm(&checksum, &cursor, buffer, buffer_len, &deg, &min_parser)) {
        return false;
    }
    *lat = (float)deg + min_parser / 60.0;
    COMMA_OR_FAIL(cursor);
    int16_t next = parse_single_char(&checksum, &cursor, buffer, buffer_len);
    if (next == 'S') {
        *lat = -*lat;
    } else if (next == 'N') {
        // Nothing to do
    } else if (next == EOF) {
        // Empty field
    } else {
        // Invalid value
        return false;
    }
    COMMA_OR_FAIL(cursor);
    if (!parse_dm(&checksum, &cursor, buffer, buffer_len, &deg, &min_parser)) {
        return false;
    }
    *lon = (float)deg + min_parser / 60.0;
    COMMA_OR_FAIL(cursor);
    next = parse_single_char(&checksum, &cursor, buffer, buffer_len);
    if (next == 'W') {
        *lon = -*lon;
    } else if (next == 'E') {
        // Nothing to do
    } else if (next == EOF) {
        // Empty field
    } else {
        // Invalid value
        return false;
    }
    COMMA_OR_FAIL(cursor);
    uint32_t fix_quality_int;
    if (!parse_integer(&checksum, &cursor, buffer, buffer_len, &fix_quality_int)) {
        return false;
    }
    *fix_quality = fix_quality_int;
    COMMA_OR_FAIL(cursor);
    uint32_t num_satellites_int;
    if (!parse_integer(&checksum, &cursor, buffer, buffer_len, &num_satellites_int)) {
        return false;
    }
    *num_satellites = num_satellites_int;
    COMMA_OR_FAIL(cursor);
    if (!parse_float(&checksum, &cursor, buffer, buffer_len, hdop)) {
        return false;
    }
    COMMA_OR_FAIL(cursor);
    if (!parse_float(&checksum, &cursor, buffer, buffer_len, altitude)) {
        return false;
    }
    COMMA_OR_FAIL(cursor);
    next = parse_single_char(&checksum, &cursor, buffer, buffer_len);
    if (next == 'M') {
        // Nothing to do
    } else if (next == EOF) {
        // Empty field
    } else {
        // Invalid value
        return false;
    }
    COMMA_OR_FAIL(cursor);
    if (!parse_float(&checksum, &cursor, buffer, buffer_len, geoid_sep)) {
        return false;
    }
    // The rest we don't care about
    consume_until_checksum(&checksum, &cursor, buffer, buffer_len);
    return check_checksum(checksum, cursor, buffer, buffer_len);
}

#ifdef GPS_UTIL_TEST
static void test_parse_sentence_gga(void) {
    uint8_t hour;
    uint8_t min;
    float sec;
    float lat;
    float lon;
    uint8_t fix_quality;
    uint8_t num_satellites;
    float hdop;
    float altitude;
    float geoid_sep;
    uint8_t checksum = 0;
    char buffer[] = "GPGGA,161229.487,3723.2475,N,12158.3416,W,1,07,1.0,9.0,M,1.0,M,1,0000*4B";
    uint32_t buffer_len = sizeof(buffer) - 1;
    assert_eq(buffer_len, 72);
    uint32_t cursor = 6;
    for (uint32_t i = 0; i < cursor; i++) {
        checksum ^= buffer[i];
    }
    assert(gpsutil_parse_sentence_gga(
        checksum, cursor, buffer, buffer_len,
        &hour, &min, &sec, &lat, &lon, &fix_quality, &num_satellites,
        &hdop, &altitude, &geoid_sep
    ));
    assert_eq(hour, 16);
    assert_eq(min, 12);
    assert_float_eq(sec, 29.487);
    assert_float_eq(lat, 37.387458);
    assert_float_eq(lon, -121.97236);
    assert_eq(fix_quality, 1);
    assert_eq(num_satellites, 7);
    assert_float_eq(hdop, 1.0);
    assert_float_eq(altitude, 9.0);
    checksum = 0;
    char buffer2[] = "GNGGA,121613.000,2455.2122,N,6532.8547,E,1,05,3.3,-1.0,M,0.0,M,,*64";
    uint32_t buffer2_len = sizeof(buffer2) - 1;
    assert_eq(buffer2_len, 67);
    uint32_t cursor2 = 6;
    for (uint32_t i = 0; i < cursor2; i++) {
        checksum ^= buffer2[i];
    }
    assert(gpsutil_parse_sentence_gga(
        checksum, cursor2, buffer2, buffer2_len,
        &hour, &min, &sec, &lat, &lon, &fix_quality, &num_satellites,
        &hdop, &altitude, &geoid_sep
    ));
    assert_eq(hour, 12);
    assert_eq(min, 16);
    assert_float_eq(sec, 13.0);
    assert_float_eq(lat, 24.920203);
    assert_float_eq(lon, 65.547578);
    assert_eq(fix_quality, 1);
    assert_eq(num_satellites, 5);
    assert_float_eq(hdop, 3.3);
    assert_float_eq(altitude, -1.0);
    checksum = 0;
    // Minimum example
    char buffer3[] = "GNGGA,,,,,,0,00,25.5,,,,,,*64";
    uint32_t buffer3_len = sizeof(buffer3) - 1;
    assert_eq(buffer3_len, 29);
    uint32_t cursor3 = 6;
    for (uint32_t i = 0; i < cursor3; i++) {
        checksum ^= buffer3[i];
    }
    assert(gpsutil_parse_sentence_gga(
        checksum, cursor3, buffer3, buffer3_len,
        &hour, &min, &sec, &lat, &lon, &fix_quality, &num_satellites,
        &hdop, &altitude, &geoid_sep
    ));
    assert_eq(hour, 0);
    assert_eq(min, 0);
    assert_float_eq(sec, 0.0);
    assert_float_eq(lat, 0.0);
    assert_float_eq(lon, 0.0);
    assert_eq(fix_quality, 0);
    assert_eq(num_satellites, 0);
    assert_float_eq(hdop, 25.5);
    assert_float_eq(altitude, 0.0);
}
#endif

bool gpsutil_parse_sentence_gll(
    uint8_t checksum, uint32_t cursor, const char *buffer, uint32_t buffer_len,
    uint8_t *hour, uint8_t *min, float *sec,
    float *lat, float *lon, bool *valid
) {
    // dddmm.mmmmm, [NS], dddmm.mmmmm, [EW], hhmmss.ss, [AV], ...
    uint16_t deg;
    float min_parser;
    if (!parse_dm(&checksum, &cursor, buffer, buffer_len, &deg, &min_parser)) {
        return false;
    }
    *lat = (float)deg + min_parser / 60.0;
    char c;
    COMMA_OR_FAIL(cursor);
    int16_t next = parse_single_char(&checksum, &cursor, buffer, buffer_len);
    if (next == 'S') {
        *lat = -*lat;
    } else if (next == 'N') {
        // Nothing to do
    } else if (next == EOF) {
        // Empty field
    } else {
        // Invalid value
        return false;
    }
    COMMA_OR_FAIL(cursor);
    if (!parse_dm(&checksum, &cursor, buffer, buffer_len, &deg, &min_parser)) {
        return false;
    }
    *lon = (float)deg + min_parser / 60.0;
    COMMA_OR_FAIL(cursor);
    next = parse_single_char(&checksum, &cursor, buffer, buffer_len);
    if (next == 'W') {
        *lon = -*lon;
    } else if (next == 'E') {
        // Nothing to do
    } else if (next == EOF) {
        // Empty field
    } else {
        // Invalid value
        return false;
    }
    COMMA_OR_FAIL(cursor);
    if (!parse_hms(&checksum, &cursor, buffer, buffer_len, hour, min, sec)) {
        return false;
    }
    COMMA_OR_FAIL(cursor);
    next = parse_single_char(&checksum, &cursor, buffer, buffer_len);
    if (next == 'A') {
        *valid = true;
    } else if (next == 'V') {
        *valid = false;
    } else if (next == EOF) {
        // Empty field
        *valid = false;
    } else {
        // Invalid value
        return false;
    }
    // There is also an optional mode, which is unused
    consume_until_checksum(&checksum, &cursor, buffer, buffer_len);
    return check_checksum(checksum, cursor, buffer, buffer_len);
}

#ifdef GPS_UTIL_TEST
static void test_parse_sentence_gll(void) {
    uint8_t checksum = 0;
    uint8_t hour;
    uint8_t min;
    float sec;
    float lat;
    float lon;
    bool valid;
    char buffer2[] = "GNGLL,4922.1031,N,10022.1234,W,002434.000,A,A*5F";
    uint32_t buffer2_len = sizeof(buffer2) - 1;
    assert_eq(buffer2_len, 48);
    uint32_t cursor2 = 6;
    for (uint32_t i = 0; i < cursor2; i++) {
        checksum ^= buffer2[i];
    }
    assert(gpsutil_parse_sentence_gll(
        checksum, cursor2, buffer2, buffer2_len,
        &hour, &min, &sec, &lat, &lon, &valid
    ));
    assert_float_eq(lat, 49.368385);
    assert_float_eq(lon, -100.368723);
    assert_eq(hour, 0);
    assert_eq(min, 24);
    assert_float_eq(sec, 34.0);
    assert(valid);
    // Minimum example
    checksum = 0;
    char buffer3[] = "GNGLL,,,,,,V,N*7A";
    uint32_t buffer3_len = sizeof(buffer3) - 1;
    assert_eq(buffer3_len, 17);
    uint32_t cursor3 = 6;
    for (uint32_t i = 0; i < cursor3; i++) {
        checksum ^= buffer3[i];
    }
    assert(gpsutil_parse_sentence_gll(
        checksum, cursor3, buffer3, buffer3_len,
        &hour, &min, &sec, &lat, &lon, &valid
    ));
    assert_float_eq(lat, 0.0);
    assert_float_eq(lon, 0.0);
    assert_eq(hour, 0);
    assert_eq(min, 0);
    assert_float_eq(sec, 0.0);
    assert(!valid);
}

#endif
bool gpsutil_parse_sentence_rmc(
    uint8_t checksum, uint32_t cursor, const char *buffer, uint32_t buffer_len,
    uint8_t *hour, uint8_t *min, float *sec,
    float *lat, float *lon, bool *valid
) {
    // XXX: Currently only used to retrieve lat, lon, and time
    // hhmmss.ss, [AV], ddmm.mmmmm, [NS], dddmm.mmmmm, [EW], sss.s, ddd.d, ddMMyy, [E/W]
    if (!parse_hms(&checksum, &cursor, buffer, buffer_len, hour, min, sec)) {
        return false;
    }
    char c;
    COMMA_OR_FAIL(cursor);
    // Valid
    int16_t next = parse_single_char(&checksum, &cursor, buffer, buffer_len);
    if (next == 'A') {
        *valid = true;
    } else if (next == 'V') {
        *valid = false;
    } else if (next == EOF) {
        // Empty field
        *valid = false;
    } else {
        // Invalid value
        return false;
    }
    COMMA_OR_FAIL(cursor);
    // Latitude
    uint16_t deg;
    float min_parser;
    if (!parse_dm(&checksum, &cursor, buffer, buffer_len, &deg, &min_parser)) {
        return false;
    }
    *lat = (float)deg + min_parser / 60.0;
    COMMA_OR_FAIL(cursor);
    next = parse_single_char(&checksum, &cursor, buffer, buffer_len);
    if (next == 'S') {
        *lat = -*lat;
    } else if (next == 'N') {
        // Nothing to do
    } else if (next == EOF) {
        // Empty field
    } else {
        // Invalid value
        return false;
    }
    COMMA_OR_FAIL(cursor);
    // Longitude
    if (!parse_dm(&checksum, &cursor, buffer, buffer_len, &deg, &min_parser)) {
        return false;
    }
    *lon = (float)deg + min_parser / 60.0;
    COMMA_OR_FAIL(cursor);
    next = parse_single_char(&checksum, &cursor, buffer, buffer_len);
    if (next == 'W') {
        *lon = -*lon;
    } else if (next == 'E') {
        // Nothing to do
    } else if (next == EOF) {
        // Empty field
    } else {
        // Invalid value
        return false;
    }
    // The rest is unused
    consume_until_checksum(&checksum, &cursor, buffer, buffer_len);
    return check_checksum(checksum, cursor, buffer, buffer_len);
}

#ifdef GPS_UTIL_TEST
static void test_parse_sentence_rmc(void) {
    uint8_t checksum = 0;
    uint8_t hour;
    uint8_t min;
    float sec;
    float lat;
    float lon;
    bool valid;
    char buffer[] = "GPRMC,081836,A,3751.65,S,14507.36,E,000.0,360.0,130998,011.3,E*62";
    uint32_t buffer_len = sizeof(buffer) - 1;
    assert_eq(buffer_len, 65);
    uint32_t cursor = 6;
    for (uint32_t i = 0; i < cursor; i++) {
        checksum ^= buffer[i];
    }
    assert(gpsutil_parse_sentence_rmc(
        checksum, cursor, buffer, buffer_len,
        &hour, &min, &sec, &lat, &lon, &valid
    ));
    assert_float_eq(lat, -37.860833);
    assert_float_eq(lon, 145.122667);
    assert_eq(hour, 8);
    assert_eq(min, 18);
    assert_float_eq(sec, 36.0);
    assert(valid);
    checksum = 0;
    char buffer2[] = "GNRMC,001313.000,A,3740.0000,N,12223.0000,W,0.00,0.00,290123,,,A*69";
    uint32_t buffer2_len = sizeof(buffer2) - 1;
    assert_eq(buffer2_len, 67);
    uint32_t cursor2 = 6;
    for (uint32_t i = 0; i < cursor2; i++) {
        checksum ^= buffer2[i];
    }
    assert(gpsutil_parse_sentence_rmc(
        checksum, cursor2, buffer2, buffer2_len,
        &hour, &min, &sec, &lat, &lon, &valid
    ));
    assert_float_eq(lat, 37.666667);
    assert_float_eq(lon, -122.383333);
    assert_eq(hour, 0);
    assert_eq(min, 13);
    assert_float_eq(sec, 13.0);
    assert(valid);
    // Minimum example
    checksum = 0;
    char buffer3[] = "GNRMC,,V,,,,,,,,,,M*4E";
    uint32_t buffer3_len = sizeof(buffer3) - 1;
    assert_eq(buffer3_len, 22);
    uint32_t cursor3 = 6;
    for (uint32_t i = 0; i < cursor3; i++) {
        checksum ^= buffer3[i];
    }
    assert(gpsutil_parse_sentence_rmc(
        checksum, cursor3, buffer3, buffer3_len,
        &hour, &min, &sec, &lat, &lon, &valid
    ));
    assert_float_eq(lat, 0.0);
    assert_float_eq(lon, 0.0);
    assert_eq(hour, 0);
    assert_eq(min, 0);
    assert_float_eq(sec, 0.0);
    assert(!valid);
}
#endif

bool gpsutil_parse_sentence_zda(
    uint8_t checksum, uint32_t cursor, const char *buffer, uint32_t buffer_len,
    uint8_t *hour, uint8_t *min, float *sec,
    uint16_t *year, uint8_t *month, uint8_t *day,
    uint8_t *zone_hour, uint8_t *zone_min
) {
    // hhmmss.sss,dd,mm,yyyy,zh,zm
    if (!parse_hms(&checksum, &cursor, buffer, buffer_len, hour, min, sec)) {
        return false;
    }
    char c;
    COMMA_OR_FAIL(cursor);
    uint32_t day_int;
    if (!parse_integer(&checksum, &cursor, buffer, buffer_len, &day_int)) {
        return false;
    }
    *day = day_int;
    COMMA_OR_FAIL(cursor);
    uint32_t month_int;
    if (!parse_integer(&checksum, &cursor, buffer, buffer_len, &month_int)) {
        return false;
    }
    *month = month_int;
    COMMA_OR_FAIL(cursor);
    uint32_t year_int;
    if (!parse_integer(&checksum, &cursor, buffer, buffer_len, &year_int)) {
        return false;
    }
    *year = year_int;
    COMMA_OR_FAIL(cursor);
    uint32_t zone_hour_int;
    if (!parse_integer(&checksum, &cursor, buffer, buffer_len, &zone_hour_int)) {
        return false;
    }
    *zone_hour = zone_hour_int;
    COMMA_OR_FAIL(cursor);
    uint32_t zone_min_int;
    if (!parse_integer(&checksum, &cursor, buffer, buffer_len, &zone_min_int)) {
        return false;
    }
    *zone_min = zone_min_int;
    return check_checksum(checksum, cursor, buffer, buffer_len);
}

#ifdef GPS_UTIL_TEST
static void test_parse_sentence_zda(void) {
    uint8_t checksum = 0;
    uint8_t hour;
    uint8_t min;
    float sec;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t zone_hour;
    uint8_t zone_min;
    // A real example
    char buffer[] = "GNZDA,001313.000,29,01,2023,00,00*41";
    uint32_t buffer_len = sizeof(buffer) - 1;
    assert_eq(buffer_len, 36);
    uint32_t cursor = 6;
    for (uint32_t i = 0; i < cursor; i++) {
        checksum ^= buffer[i];
    }
    assert(gpsutil_parse_sentence_zda(
        checksum, cursor, buffer, buffer_len,
        &hour, &min, &sec, &year, &month, &day, &zone_hour, &zone_min
    ));
    assert_eq(hour, 0);
    assert_eq(min, 13);
    assert_float_eq(sec, 13.0);
    assert_eq(day, 29);
    assert_eq(month, 1);
    assert_eq(year, 2023);
    assert_eq(zone_hour, 0);
    assert_eq(zone_min, 0);
    checksum = 0;
    char buffer2[] = "GNZDA,060618.133,23,02,2023,00,00*40";
    uint32_t buffer2_len = sizeof(buffer2) - 1;
    assert_eq(buffer2_len, 36);
    uint32_t cursor2 = 6;
    for (uint32_t i = 0; i < cursor2; i++) {
        checksum ^= buffer2[i];
    }
    assert(gpsutil_parse_sentence_zda(
        checksum, cursor2, buffer2, buffer2_len,
        &hour, &min, &sec, &year, &month, &day, &zone_hour, &zone_min
    ));
    assert_eq(hour, 6);
    assert_eq(min, 6);
    assert_float_eq(sec, 18.133);
    assert_eq(day, 23);
    assert_eq(month, 2);
    assert_eq(year, 2023);
    assert_eq(zone_hour, 0);
    assert_eq(zone_min, 0);
    // Minimum example
    checksum = 0;
    char buffer3[] = "GNZDA,,,,,,*56";
    uint32_t buffer3_len = sizeof(buffer3) - 1;
    assert_eq(buffer3_len, 14);
    uint32_t cursor3 = 6;
    for (uint32_t i = 0; i < cursor3; i++) {
        checksum ^= buffer3[i];
    }
    assert(gpsutil_parse_sentence_zda(
        checksum, cursor3, buffer3, buffer3_len,
        &hour, &min, &sec, &year, &month, &day, &zone_hour, &zone_min
    ));
    assert_eq(hour, 0);
    assert_eq(min, 0);
    assert_float_eq(sec, 0.0);
    assert_eq(day, 0);
    assert_eq(month, 0);
    assert_eq(year, 0);
    assert_eq(zone_hour, 0);
    assert_eq(zone_min, 0);
}
#endif

/// Consume a sentence and check its checksum.
bool gpsutil_parse_sentence_unused(
    uint8_t checksum, uint32_t cursor, const char *buffer, uint32_t buffer_len
) {
    consume_until_checksum(&checksum, &cursor, buffer, buffer_len);
    return check_checksum(checksum, cursor, buffer, buffer_len);
}

static void determine_time_validity(struct  gps_status *gps_status) {
    // XXX: This is a bit of a hack, but it works for now
    gps_status->gps_time_valid = (
        gps_status->utc_year > 1000
    );
}

/// Returns whether the current sentence is used
/// Currently, we care about the following sentences:
/// - GGA: type = 0
/// - GLL: type = 1
/// - RMC: type = 2
/// - ZDA: type = 3
static bool parse_sentence(struct gps_status *gps_status) {
    // XOR everything until the asterisk
    // Always check the validity before committing to the `gps_status` struct
    uint8_t checksum = 0;
    uint8_t cursor = 0;
    const char *buffer = gps_status->buffer;
    const uint32_t buffer_len = gps_status->buffer_pos;
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
    if (type0 == 'G' && type1 == 'G' && type2 == 'A') {
        type = 0;
    } else if (type0 == 'G' && type1 == 'L' && type2 == 'L') {
        type = 1;
    } else if (type0 == 'R' && type1 == 'M' && type2 == 'C') {
        type = 2;
    } else if (type0 == 'Z' && type1 == 'D' && type2 == 'A') {
        type = 3;
    } else {
        // Return true as long as the checksum is correct
        return gpsutil_parse_sentence_unused(checksum, cursor, buffer, buffer_len);
    }
    char c;
    COMMA_OR_FAIL(cursor);
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
                checksum, cursor, buffer, buffer_len,
                &hour, &min, &sec, &lat, &lon, &fix_quality, &num_satellites,
                &hdop, &altitude, &geoid_sep);
            if (result) {
                gps_status->gps_lat = lat;
                gps_status->gps_lon = lon;
                gps_status->gps_valid = fix_quality > 0;
                gps_status->gps_alt = altitude;
                gps_status->gps_sat_num = num_satellites;
                gps_status->utc_hour = hour;
                gps_status->utc_min = min;
                gps_status->utc_sec = sec;
                determine_time_validity(gps_status);
            }
            return result;
        }
        case 1:
        {
            uint8_t hour, min;
            float sec;
            float lat, lon;
            bool valid;
            bool result = gpsutil_parse_sentence_gll(
                checksum, cursor, buffer, buffer_len,
                &hour, &min, &sec, &lat, &lon, &valid);
            if (result) {
                gps_status->gps_lat = lat;
                gps_status->gps_lon = lon;
                gps_status->gps_valid = valid;
                gps_status->utc_hour = hour;
                gps_status->utc_min = min;
                gps_status->utc_sec = sec;
                determine_time_validity(gps_status);
            }
            return result;
        }
        case 2:
        {
            uint8_t hour, min;
            float sec;
            float lat, lon;
            bool valid;
            bool result = gpsutil_parse_sentence_rmc(
                checksum, cursor, buffer, buffer_len,
                &hour, &min, &sec, &lat, &lon, &valid);
            if (result) {
                gps_status->gps_valid = valid;
                gps_status->gps_lat = lat;
                gps_status->gps_lon = lon;
                gps_status->utc_hour = hour;
                gps_status->utc_min = min;
                gps_status->utc_sec = sec;
                determine_time_validity(gps_status);
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
                checksum, cursor, buffer, buffer_len,
                &hour, &min, &sec, &year, &month, &day, &zone_hour, &zone_min);
            if (result) {
                gps_status->utc_hour = hour;
                gps_status->utc_min = min;
                gps_status->utc_sec = sec;
                gps_status->utc_year = year;
                gps_status->utc_month = month;
                gps_status->utc_day = day;
                determine_time_validity(gps_status);
            }
            return result;
        }
        default:
            // unreachable
            return false;
    }
}

#ifdef GPS_UTIL_TEST
// Test that the sentences are dispatched correctly
void test_parse_sentence(void) {
    struct gps_status gps_status = GPS_STATUS_INIT;
    // Test GGA
    char sentence[] = "GNGGA,121613.000,2455.2122,N,6532.8547,E,1,05,3.3,-1.0,M,0.0,M,,*64";
    strcpy(gps_status.buffer, sentence);
    gps_status.buffer_pos = strlen(sentence);
    assert(parse_sentence(&gps_status));
    assert_eq(gps_status.utc_hour, 12);
    assert_eq(gps_status.utc_min, 16);
    assert_float_eq(gps_status.utc_sec, 13.0);
    assert_float_eq(gps_status.gps_lat, 24.920203);
    assert_float_eq(gps_status.gps_lon, 65.547578);
    assert_float_eq(gps_status.gps_alt, -1.0);
    // GGA does not carry validity
    // Test GLL
    char sentence2[] = "GNGLL,4922.1031,N,10022.1234,W,002434.000,A,A*5F";
    strcpy(gps_status.buffer, sentence2);
    gps_status.buffer_pos = strlen(sentence2);
    assert(parse_sentence(&gps_status));
    assert_eq(gps_status.utc_hour, 0);
    assert_eq(gps_status.utc_min, 24);
    assert_float_eq(gps_status.utc_sec, 34.0);
    assert_float_eq(gps_status.gps_lat, 49.368385);
    assert_float_eq(gps_status.gps_lon, -100.368723);
    assert(gps_status.gps_valid);
    // Test RMC
    char sentence3[] = "GNRMC,001313.000,A,3740.0000,N,12223.0000,W,0.00,0.00,290123,,,A*69";
    strcpy(gps_status.buffer, sentence3);
    gps_status.buffer_pos = strlen(sentence3);
    assert(parse_sentence(&gps_status));
    assert_eq(gps_status.utc_hour, 0);
    assert_eq(gps_status.utc_min, 13);
    assert_float_eq(gps_status.utc_sec, 13.0);
    assert_float_eq(gps_status.gps_lat, 37.666667);
    assert_float_eq(gps_status.gps_lon, -122.383333);   
    // Test ZDA
    char sentence4[] = "GNZDA,060618.133,23,02,2023,00,00*40";
    strcpy(gps_status.buffer, sentence4);
    gps_status.buffer_pos = strlen(sentence4);
    assert(parse_sentence(&gps_status));
    assert_eq(gps_status.utc_hour, 6);
    assert_eq(gps_status.utc_min, 6);
    assert_float_eq(gps_status.utc_sec, 18.133);
    assert_eq(gps_status.utc_year, 2023);
    assert_eq(gps_status.utc_month, 2);
    assert_eq(gps_status.utc_day, 23);
    assert(gps_status.gps_time_valid);
}
#endif

/// Feed a character to the parser, returns true if a sentence is parsed successfully
bool gpsutil_feed(struct gps_status *gps_status, int c) {
    if (c == '$') {
        // Start of a sentence
        gps_status->in_sentence = true;
        gps_status->buffer_pos = 0;
        return false;
    }
    if (!gps_status->in_sentence) {
        // Then nothing matters
        return false;
    }
    if (c == '\r' || c == '\n') {
        gps_status->in_sentence = false;
        if (gps_status->buffer_pos > 0) {
            gps_status->buffer[gps_status->buffer_pos] = '\0';
            bool result = parse_sentence(gps_status);
#ifndef NDEBUG
            if (!result) {
                printf("Bad sentence: %s\n", gps_status->buffer);
            } else {
                printf("GPS parsed: %s\n", gps_status->buffer);
            }
#endif
            return result;
        }
    // Check for buffer overflow
    } else if (gps_status->buffer_pos < sizeof(gps_status->buffer) - 1) {
        gps_status->buffer[gps_status->buffer_pos++] = c;
    } else {
        // Buffer overflow
        printf("GPS buffer overflow\n");
        gps_status->in_sentence = false;
    }
    return false;
}

#ifdef GPS_UTIL_TEST
void test_gpsutil_feed(void) {
    struct gps_status gps_status = GPS_STATUS_INIT;
    // Just test six short sentences
    char source[] = "$GNZDA,,,,,,*56\r\n"
    "$GPRMC,081836,A,3751.65,S,14507.36,E,000.0,360.0,130998,011.3,E*62\r\n"
    "$GNZDA,,,,,,*56\r\n"
    "$GPRMC,081836,A,3751.65,S,14507.36,E,000.0,360.0,130998,011.3,E*62\r\n"
    "$GNZDA,,,,,,*56\r\n"
    "$GPRMC,081836,A,3751.65,S,14507.36,E,000.0,360.0,130998,011.3,E*62\r\n";

    for (size_t i = 0; i < strlen(source); ++i) {
        gpsutil_feed(&gps_status, source[i]);
    }

    assert_float_eq(gps_status.gps_lat, -37.860833);
    assert_float_eq(gps_status.gps_lon, 145.122667);
}
#endif

/// Get the current time in UTC
bool gpsutil_get_time(struct gps_status *gps_status, time_t *t) {
    if (!gps_status->gps_time_valid) {
        return false;
    }
    struct tm intermediate;
    intermediate.tm_year = gps_status->utc_year + 100;
    intermediate.tm_mon = gps_status->utc_month - 1;
    intermediate.tm_mday = gps_status->utc_day;
    intermediate.tm_hour = gps_status->utc_hour;
    intermediate.tm_min = gps_status->utc_min;
    intermediate.tm_sec = (int)gps_status->utc_sec;
    intermediate.tm_isdst = -1;
    // `mktime` ignores `wday` and `yday` which we don't have
    *t = mktime(&intermediate);
    return true;
}

/// Get the current GPS position
bool gpsutil_get_location(struct gps_status *gps_status, float *lat, float *lon, float *alt) {
    if (!gps_status->gps_valid) {
        return false;
    }
    *lat = gps_status->gps_lat;
    *lon = gps_status->gps_lon;
    *alt = gps_status->gps_alt;
    return true;
}

#ifdef GPS_UTIL_TEST
int main(void) {
    test_parse_integer();
    test_parse_float();
    test_parse_single_char();
    test_parse_hms();
    test_parse_dm();
    test_check_checksum();
    test_parse_sentence_gga();
    test_parse_sentence_gll();
    test_parse_sentence_rmc();
    test_parse_sentence_zda();
    test_parse_sentence();
    test_gpsutil_feed();
    printf("All tests passed\n");
    return 0;
}

#endif
