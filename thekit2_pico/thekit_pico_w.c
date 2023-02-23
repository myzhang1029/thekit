/* UART-controlled switch and speaker on RPi Pico W. */
/*
 *  thekit_pico_w.c
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

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "base64.h"
#include "common.h"
#include "pcm.h"

/// Initialize all interfaces
static inline void w_init() {
    cyw43_arch_init();

    gpio_init(SWITCH_PIN);
    gpio_set_dir(SWITCH_PIN, GPIO_OUT);
    gpio_put(SWITCH_PIN, switch_status);

    pcmaudio_init(&player, SPEAKER_PIN);
}

/// Blink the onboard LED
static inline void w_blink_led() {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    sleep_ms(50);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
    sleep_ms(50);
}

int main() {
    w_init();
}
