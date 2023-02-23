/// Random sensor management solution based on TinyGPS++ and other sensors
/*
 *  TheKit.ino
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


#include <SoftwareSerial.h>
#include <TinyGPS++.h>

// VISHAY NTC 2381-640-55103
// Datasheet:
// http://connect.iobridge.com/wp-content/uploads/2012/02/thermistor_2381-640-55103.pdf
static const int sensorPin = A0;
// Analog ref
static const float VAref = 5.0; // Volts
// NTC base resistance
static const float R0 = 1e4; // Ohm \pm 1%
// Temperature corresponding to R0
static const float T0 = 25.0 + 273.15; // Kelvin
// divider resistance
static const float R = 1e4;     // Ohm \pm 1%
static const float BETA = 3977; // Kelvin \pm 0.75%
// The port to which the light is attached
static const int LightPin = 8;
bool LightStatus = true;
// GPS Things
static const int RXPin = 12, TXPin = 13;
static const uint32_t GPSBaud = 9600;
static const uint32_t SerBaud = 9600;
TinyGPSPlus gps;
SoftwareSerial ss(RXPin, TXPin);
// Command Things
String command;

void setup()
{
    Serial.begin(SerBaud);
    ss.begin(GPSBaud);
    pinMode(LightPin, OUTPUT);
    setLight(LightStatus);
}

void loop()
{
    if (Serial.available())
    {
        command = Serial.readStringUntil('\n');
        command.trim();
        if (command.equals(""))
        {
        }
        else if (command.equals("light_on"))
            setLight(true);
        else if (command.equals("light_off"))
            setLight(false);
        else if (command.equals("get_info"))
            getInfo();
        else if (command.equals("play"))
            play();
        else
        {
            Serial.print(F("{\"error\":\"bad command: `"));
            Serial.print(command);
            Serial.println(F("`\"}"));
        }
    }
    while (ss.available())
        gps.encode(ss.read());
}

/// Get temperature in degrees Celsius
static float getTemp()
{
    int sensorValue = analogRead(sensorPin); // \pm 0.5/sensorValue
    float VR = (VAref / 1024.00) * sensorValue;
    float NTC = R * (VAref - VR) / VR;
    // R = R0 * exp(beta/T - beta/T0)
    // ln(R/R0) + beta/T0 = beta/T
    float T = BETA / (log(NTC / R0) + BETA / T0);
    return T - 273.15;
}

/// Commands

/// Turn on/off the light
void setLight(bool on)
{
    digitalWrite(LightPin, on ? HIGH : LOW);
    Serial.print(F("{\"light\":"));
    Serial.print(on ? F("true") : F("false"));
    Serial.println(F("}"));
    LightStatus = on;
}

/// Log NTC temperature, GPS data, and other status
void getInfo()
{
    // Start
    Serial.print(F("{\"v\":\"1.5.0\",\"light\":"));
    Serial.print(LightStatus ? F("true") : F("false"));
    Serial.print(F(",\"tempC\":"));
    Serial.print(getTemp());

    // Parse GPS location
    Serial.print(F(",\"loc\":"));
    if (gps.location.isValid())
    {
        Serial.print(F("{\"latN\":"));
        Serial.print(gps.location.lat(), 6);
        Serial.print(F(",\"lonE\":"));
        Serial.print(gps.location.lng(), 6);
        Serial.print(F("}"));
    }
    else
        Serial.print(F("null"));

    // Parse GPS date
    Serial.print(F(",\"date\":\""));
    if (gps.date.isValid())
    {
        Serial.print(gps.date.year());
        Serial.print(F("-"));
        if (gps.date.month() < 10)
            Serial.print(F("0"));
        Serial.print(gps.date.month());
        Serial.print(F("-"));
        if (gps.date.day() < 10)
            Serial.print(F("0"));
        Serial.print(gps.date.day());
    }
    else
        Serial.print(F("null"));

    // Parse GPS time
    Serial.print(F("T"));
    if (gps.time.isValid())
    {
        if (gps.time.hour() < 10)
            Serial.print(F("0"));
        Serial.print(gps.time.hour());
        Serial.print(F(":"));
        if (gps.time.minute() < 10)
            Serial.print(F("0"));
        Serial.print(gps.time.minute());
        Serial.print(F(":"));
        if (gps.time.second() < 10)
            Serial.print(F("0"));
        Serial.print(gps.time.second());
        Serial.print(F("."));
        if (gps.time.centisecond() < 10)
            Serial.print(F("0"));
        Serial.print(gps.time.centisecond());
    }
    else
        Serial.print(F("null"));

    // End
    Serial.println(F("Z\"}"));
}

#ifndef NO_PCM
#include <PCM.h>

const unsigned char sample[] PROGMEM = {
#include "data.h"
};

void play()
{
    Serial.println(F("{\"play\":true}"));
    startPlayback(sample, sizeof(sample));
}
#endif

