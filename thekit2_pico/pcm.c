/* PCM Audio Player for Raspberry Pi Pico */
/*
 *  pcm.c
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

/* Basic usage:
 * - pcmaudio_init(struct, PIN);
 * - pcmaudio_play(initialized struct, length)
 * - pcmaudio_stop() to interrupt (safe to call multiple times on one player)
 *
 * if playback is finished, pcmaudio_stop() is automatically called,
 * and the buffer can be free()d.
*/

#include "hardware/pwm.h"
#include "malloc.h"
#include "pico/stdlib.h"

#include "pcm.h"

// 8kHz: 125us per sample
#define SAMPLE_TIME 125

static bool update_pcm_callback(struct repeating_timer *t) {
    struct pcmaudio_player *player = (struct pcmaudio_player *) t->user_data;

    if (player->audio_length > player->index) {
        uint8_t next = player->audio_buf[player->index++];
        pwm_set_gpio_level(player->pin, next);
        return true;
    }
    // the audio has been drained
    pcmaudio_stop(player);
    return false;
}

/// Initialize player on the pin
void pcmaudio_init(struct pcmaudio_player *player, uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    /* for BJT amps */
    gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_12MA);
    player->pin = pin;
    player->started = false;
}

/// Put data into the player
/// `audio_buffer` is an array of `uint8_t`s representing PCM samples
/// `length` is the total length in bytes
void pcmaudio_fill(struct pcmaudio_player *player, uint8_t *buffer, uint32_t length, bool free_buf) {
    player->audio_buf = buffer;
    player->audio_length = length;
    player->free_buf = free_buf;
    player->index = 0;
}

/// Start playing
/// Returns `false` if playback has already started or a timer cannot be initiated.
bool pcmaudio_play(struct pcmaudio_player *player) {
    uint slice_num;
    if (player->started)
        return false;
    if (player->audio_buf == NULL)
        return false;
    slice_num = pwm_gpio_to_slice_num(player->pin);
    // 8-bit wraps
    pwm_set_wrap(slice_num, 255);
    pwm_set_gpio_level(player->pin, 0);
    pwm_set_enabled(slice_num, true);
    // Must be before adding timer in case the audio is too short
    player->started = true;
    return add_repeating_timer_us(-(SAMPLE_TIME), update_pcm_callback, player, &player->pcm_timer);
}

/// Make sure playback is stopped
void pcmaudio_stop(struct pcmaudio_player *player) {
    uint slice_num;
    if (!player->started)
        return;
    player->started = false;
    // First cancel timer then free buffer
    // `cancel_repeating_timer` should work on already-cancelled timers
    cancel_repeating_timer(&player->pcm_timer);
    if (player->free_buf)
        free(player->audio_buf);
    slice_num = pwm_gpio_to_slice_num(player->pin);
    // Disable PWM
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(player->pin), 0);
    pwm_set_enabled(slice_num, false);
    // Advance the counter by pulsing the PH_ADV bit in its CSR (4.1.19.3.24)
    // If we make sure the counter passes 0 (the newly-set `level` once,
    // we can keep the pin low when halten
    while (pwm_hw->slice[slice_num].ctr != 1) {
        // Called at most 254 times
        hw_set_bits(&pwm_hw->slice[slice_num].csr, PWM_CH0_CSR_PH_ADV_BITS);
        while (pwm_hw->slice[slice_num].csr & PWM_CH0_CSR_PH_ADV_BITS) {
            tight_loop_contents();
        }
    }
}

