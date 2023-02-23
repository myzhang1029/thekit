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

// Pin that the switch controls
#define SWITCH_PIN 22
// Pin for audio output
#define SPEAKER_PIN 26

bool switch_timer_in_use = false;
// 1 means high
bool switch_status = 1;
struct repeating_timer switch_timer;
// Speaker stuff
struct pcmaudio_player player;
// Buffer for received data
uint8_t *received_buf = NULL;
uint32_t received_size;