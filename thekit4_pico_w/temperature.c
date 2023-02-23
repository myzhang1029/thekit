/*
 *  temperature.c
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

#include "config.h"
#include "thekit4_pico_w.h"

#include <math.h>

#include "pico/stdlib.h"
#include "hardware/adc.h"

#if ENABLE_TEMPERATURE_SENSOR

void temperature_init(void) {
    // ADC
    adc_init();
    adc_gpio_init(ADC_TEMP_PIN);
    adc_gpio_init(ADC_ZERO_PIN);
}

/// Take a single temperature measurement
float temperature_measure(void) {
    adc_select_input(ADC_ZERO_PIN - 26);
    uint16_t bias = adc_read();
    adc_select_input(ADC_TEMP_PIN - 26);
    uint16_t place = adc_read();
    uint16_t sensed = place - bias;
    float VR = (VAref / 4096.00) * sensed;
    float NTC = R * (VAref - VR) / VR;
    // R = R0 * exp(beta/T - beta/T0)
    // ln(R/R0) + beta/T0 = beta/T
    float T = BETA / (log(NTC / R0) + BETA / T0);
    return T - 273.15;
}

#endif
