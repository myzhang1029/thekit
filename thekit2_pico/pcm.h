/* PCM Audio Player for Raspberry Pi Pico */
/*
 *  pcm.h
 *  Copyright (C) 2021 Zhang Maiyun <me@myzhangll.xyz>
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

#ifndef PCM_H
#define PCM_H

#include "pico/stdlib.h"

struct pcmaudio_player {
    // Speaker/amplifier pin
    uint pin;
    // PCM buffer
    uint8_t *audio_buf;
    // Whether `free(audio_buf)` should be done when finished
    bool free_buf;
    // Total length in bytes
    uint32_t audio_length;
    // Current index
    uint32_t index;
    // PCM timer
    struct repeating_timer pcm_timer;
    // Whether timer has started
    bool started;
};

void pcmaudio_init(struct pcmaudio_player *player, uint pin);
void pcmaudio_fill(struct pcmaudio_player *player, uint8_t *buffer, uint32_t length, bool free_buf);
bool pcmaudio_play(struct pcmaudio_player *player);
void pcmaudio_stop(struct pcmaudio_player *player);

#endif
