/*
 *  light.c
 *  Copyright (C) 2022-2023 Zhang Maiyun <me@myzhangll.xyz>
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

#include "pico/stdlib.h"

#include "hardware/pwm.h"
#include "hardware/rtc.h"

#if ENABLE_LIGHT
static volatile uint32_t last_button1_irq_timestamp = 0;
// defined in light.c
extern volatile uint16_t current_pwm_level;

// For gpio irq
static void light_toggle(void) {
    // Debounce
    uint32_t irq_timestamp = time_us_32();
    if (irq_timestamp - last_button1_irq_timestamp < 8000)
        return;
    last_button1_irq_timestamp = irq_timestamp;
    current_pwm_level = current_pwm_level ? 0 : WRAP;
    pwm_set_gpio_level(LIGHT_PIN, current_pwm_level);
    puts("Toggling");
}
#endif

#if ENABLE_GPS
// defined in gps.c
extern struct gps_status gps_status;

static void gps_update_rtc(void) {
    time_t t;
    timestamp_t age;
    // This hopefully doesn't interrupt an update in progress
    // But even if it does, it's not a big deal because
    // the age comparison will reject it
    if (!gps_get_time(&t, &age)) {
        // Reject invalid time
        return;
    }
    if (age > 1000000) {
        // Reject if the previous time update happened more than 1 second ago
        return;
    }
    // This is stratum 0
    update_rtc(t, 0);
}
#endif

static void gpio_irq_handler(uint gpio, uint32_t event_mask) {
#if ENABLE_LIGHT
    if (gpio == BUTTON1_PIN && (event_mask & BUTTON1_EDGE_TYPE))
        light_toggle();
#endif
#if ENABLE_GPS
    if (gpio == GPS_PPS_PIN || (event_mask & PPS_EDGE_TYPE))
        gps_update_rtc();
#endif
}

void irq_init(void) {
    gpio_set_irq_enabled_with_callback(BUTTON1_PIN, BUTTON1_EDGE_TYPE, true, gpio_irq_handler);
    gpio_pull_up(BUTTON1_PIN);
    gpio_set_irq_enabled_with_callback(GPS_PPS_PIN, PPS_EDGE_TYPE, true, gpio_irq_handler);
}
