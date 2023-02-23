/* UART-controlled switch and speaker on RPi Pico. */
/*
 *  thekit_pico.c
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

#include "hardware/pwm.h"
#include "hardware/uart.h"
#include "malloc.h"
#include "pico/stdlib.h"

#include "base64.h"
#include "pcm.h"

#ifndef NO_EMBEDDED_AUDIO
#include "raw_audio.h"
#endif

#define ECHO_UART 1

// UART for controlling interface
#define UART_ID uart1
#define BAUD_RATE 9600
#define UART_TX_PIN 8
#define UART_RX_PIN 9

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

/// Initialize all interfaces
static inline void init() {
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

    gpio_init(SWITCH_PIN);
    gpio_set_dir(SWITCH_PIN, GPIO_OUT);
    gpio_put(SWITCH_PIN, switch_status);

    pcmaudio_init(&player, SPEAKER_PIN);
}

/// Blink the onboard LED
static inline void blink_led() {
#ifdef PICO_DEFAULT_LED_PIN
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    sleep_ms(50);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
    sleep_ms(50);
#endif
}

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

/// Wait until a char is available on UART and read it
static inline uint8_t uart_getc_blocking(uart_inst_t *uart) {
    // Wait until stuff comes
    while (!uart_is_readable(uart))
        tight_loop_contents();
    uint8_t result = uart_getc(uart);
#ifdef ECHO_UART
    uart_putc(uart, result);
#endif
    return result;
}

/// Receive five ASCII digits and parse it as an integer (max 99999).
static inline uint32_t uart_get_int5(uart_inst_t *uart) {
    uint32_t result = 0;
    result += 10000 * (uart_getc_blocking(uart) - '0');
    result += 1000 * (uart_getc_blocking(uart) - '0');
    result += 100 * (uart_getc_blocking(uart) - '0');
    result += 10 * (uart_getc_blocking(uart) - '0');
    result += (uart_getc_blocking(uart) - '0');
    return result;
}

/// Fill `received_buf` and `received_size` from UART
/// Returns `false` on failure and `received_buf` is reset to NULL
static inline bool fill_receiving_buf() {
    uint32_t size = uart_get_int5(UART_ID);
    struct base64decoder decoder = BASE64_INITIALIZER;
    uint8_t *buf;

    // Make sure the previous stuff is free()d before overwriting
    if (received_buf)
        free(received_buf);
    // Allocate after free() to make OOM less likely
    buf = malloc(size);
    received_buf = buf;
    received_size = size;
    if (buf == NULL) {
        // Send a "cancel" signal
        uart_putc(UART_ID, '-');
        return false;
    }

    while (size) {
        uint8_t nextchar = uart_getc_blocking(UART_ID);
        if (!base64_feed(&decoder, (int)nextchar)) {
            // Cancel the rest if invalid characters are found
            // but keep the received portion
            received_size -= size;
            return false;
        }
        if (decoder.count >= 8) {
            size -= 1;
            *buf++ = base64_read(&decoder);
        }
    }
    return true;
}

/// Dispatch commands
/// (capitalized commands are background tasks, ARG is a ASCII 5-digit uint32_t)
/// Commands:
/// - 'l': Turn off the switch
/// - 'h': Turn on the switch
/// - 'g' ARG DATA: Receive a blob of base64-encoded bytes of a decoded length
///                 of ARG bytes. The padding '=' is not required. Repeated
///                 use overwrites the previous information.
/// - 'c': Clear the received data.
/// - 'P': Play the received data as 8-bit 8kHz PCM audio on the speaker pin.
/// - 'R' (optional): Play embedded audio
/// - 'B' ARG: Toggle the switch every ARG deciseconds
/// - 's': Stop all background tasks
static inline void dispatch_commands(uint8_t cmd) {
    switch (cmd) {
        case 'l':
            gpio_put(SWITCH_PIN, 0);
            switch_status = 0;
            break;
        case 'h':
            gpio_put(SWITCH_PIN, 1);
            switch_status = 1;
            break;
        case 'g':
            fill_receiving_buf();
            break;
        case 'c':
            if (received_buf) {
                free(received_buf);
                received_buf = NULL;
            }
            break;
        case 'P':
            // free() handled by 'c' command
            pcmaudio_fill(&player, received_buf, received_size, false);
            pcmaudio_play(&player);
            break;

        case 'B': {
            uint32_t interval = uart_get_int5(UART_ID);
            int32_t real_interval = interval * 100;
            if (switch_timer_in_use)
                cancel_repeating_timer(&switch_timer);
            if (real_interval == 0)
                /* clear the timer if the interval is 0 */
                break;
            add_repeating_timer_ms(real_interval, toggle_switch, NULL, &switch_timer);
            switch_timer_in_use = true;
            break;
        }
#ifndef NO_EMBEDDED_AUDIO
        case 'R': {
            pcmaudio_fill(&player, raw_audio, raw_audio_len, false);
            pcmaudio_play(&player);
            break;
        }
#endif
        case 's':
            cancel_all();
            break;
        default:
            // Signal an error
            blink_led();
            blink_led();
    }
}

int main() {
    init();
    while (true)
        dispatch_commands(uart_getc_blocking(UART_ID));
}
