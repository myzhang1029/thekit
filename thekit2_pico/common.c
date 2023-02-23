/* Common stuff for both Pico and Pico W */
/*
 *  common.c
 *  Copyright (C) 2021-2022 Zhang Maiyun <me@myzhangll.xyz>
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

#include "common.h"

/// Cancel all background tasks
static inline void cancel_all() {
    if (switch_timer_in_use)
        cancel_repeating_timer(&switch_timer);
    switch_timer_in_use = false;
    pcmaudio_stop(&player);
}

/// Toggle the switch
static inline bool toggle_switch(struct repeating_timer *t) {
    switch_status = !switch_status;
    gpio_put(SWITCH_PIN, switch_status);
    return true;
}