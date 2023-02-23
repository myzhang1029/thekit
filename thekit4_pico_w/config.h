#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/ip_addr.h"

#include "thekit4_pico_w.h"

// Define WOLFRAM_DATABIN_ID, DDNS_HOSTNAME, DDNS_KEY, wifi_config, HOSTNAME
#include "private_config.h"

#ifndef ENABLE_WATCHDOG
#define ENABLE_WATCHDOG 1
#endif
#ifndef ENABLE_TEMPERATURE_SENSOR
#define ENABLE_TEMPERATURE_SENSOR 1
#endif
#ifndef ENABLE_LIGHT
#define ENABLE_LIGHT 1
#endif
#ifndef ENABLE_DDNS
#define ENABLE_DDNS 1
#endif
#ifndef ENABLE_NTP
#define ENABLE_NTP 1
#endif

// Light-related
#if ENABLE_LIGHT
// Definitions
static const uint LIGHT_PIN = 3;
static const uint BUTTON1_PIN = 18;
// Magic. TODO: Try to reach 1MHz at PWM?
static const float clockdiv = 1.;
// Max duty
static const uint16_t WRAP = 1000;
// Light-based alarms
// Sort chronologically
static const struct light_sched_entry light_sched[] = {
    {7, 30, true},
    {8, 30, false},
    {21, 30, true},
    {22, 30, false},
};
#endif

// Temperature-related
#if ENABLE_TEMPERATURE_SENSOR
// Zeroing pin
static const uint ADC_ZERO_PIN = 28;
// Temperature pin
static const uint ADC_TEMP_PIN = 26;
// LM2020
static const float VAref = 3.0; // Volts
// NTC base resistance
static const float R0 = 1e4; // Ohm \pm 1%
// Temperature corresponding to R0
static const float T0 = 25.0 + 273.15; // Kelvin
// divider resistance
static const float R = 1.10e4;     // Ohm \pm 1%
static const float BETA = 3977; // Kelvin \pm 0.75%
#endif

// Tasks-related
// 5 minutes
static const int32_t TASKS_INTERVAL_MS = (5 * 60 * 1000);
#if ENABLE_TEMPERATURE_SENSOR
static const char WOLFRAM_HOST[] = "datadrop.wolframcloud.com";
static const char WOLFRAM_URI[] = "/api/v1.0/Add?bin=%s&temperature=%.4f";
static const size_t WOLFRAM_URI_BUFSIZE = sizeof(WOLFRAM_URI) + sizeof(WOLFRAM_HOST) + sizeof(WOLFRAM_DATABIN_ID) - 6 + 8;
/* Access data as:
 * ```mma
 * data := TimeSeries[
 *   MapAt[ToExpression, #, 2] & /@
 *    Normal[TimeSeries[Databin["ID"]]["temperature"]]
 * ]
 * ```
 * because we are uploading the data as strings
 */
#endif
#if ENABLE_DDNS
static const char DDNS_HOST[] = "dyn.dns.he.net";
static const char DDNS_URI[] = "/nic/update?hostname=%s&password=%s&myip=%s";
static const size_t DDNS_URI_BUFSIZE = sizeof(DDNS_URI) + sizeof(DDNS_HOST) + sizeof(DDNS_KEY) + IPADDR_STRLEN_MAX - 6 + 8;
#endif

// Time-related
#if ENABLE_NTP
static const char NTP_SERVER[] = "pool.ntp.org";
static const uint16_t NTP_PORT = 123;
// 10 minutes between syncs
static const uint32_t NTP_INTERVAL_MS = 600 * 1000;
// Crude TZ conversion
static const int TZ_DIFF_SEC = -8 * 3600;
#endif

// Networking-related
static const char DEFAULT_DNS[] = "1.1.1.1";
static const bool FORCE_DEFAULT_DNS = true;
