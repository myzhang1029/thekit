/// ESP8266 Program for interfacing with TheKit Pico
/*
 *  thekit_receiver.ino
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


#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <uri/UriBraces.h>

// Timeout for the reply of "reliable send"
const unsigned long HS_TIMEOUT = 100;

ESP8266WebServer server(80);

void print_pad5(long n) {
    if (n < 10000)
        Serial.print(F("0"));
    if (n < 1000)
        Serial.print(F("0"));
    if (n < 100)
        Serial.print(F("0"));
    if (n < 10)
        Serial.print(F("0"));
    Serial.print(n);
}

void send_and_check(String data) {
    while (Serial.available()) {
        // Make sure the incoming buffer is already empty
        // otherwise this protocol won't work
        (void)Serial.read();
    }
    for (long index = 0; index < data.length(); ++index) {
        char c = data[index];
        Serial.print(c);
        unsigned long timeout = millis() + HS_TIMEOUT;
        while (Serial.available() == 0) {
            // wait until Pico confirms
            delay(0);
            if (millis() > timeout)
                break;
        }
        (void)(Serial.read() == c);
    }
}

void setup(void) {
    Serial.begin(9600);
    WiFi.mode(WIFI_STA);
    WiFi.begin(F("SSID"), F("PASSWORD"));

    while (WiFi.status() != WL_CONNECTED)
        delay(500);

    MDNS.begin(F("stephlight"));

    // Fake command to abort base64 decode
    server.on(F("/-"), [](){
        Serial.print(F("-"));
        server.send(200, F("text/plain"), F("Nothing is happening."));
    });

    server.on(F("/"), [](){
        server.send(200, F("text/plain"),
                    F("This is an ESP8266 for Stephanie!"));
    });

    server.on(F("/off"), [](){
        Serial.print(F("l"));
        server.send(200, F("text/plain"), F("Light turned off."));
    });

    server.on(F("/on"), [](){
        Serial.print(F("h"));
        server.send(200, F("text/plain"), F("Light turned on."));
    });

    // For partitioned `send`. It is good practice to send an '-' after
    // all data has been sent.
    server.on(UriBraces(F("/send_raw")), [](){
        if (server.method() != HTTP_POST)
            server.send(405, F("text/plain"), F("Method Not Allowed"));
        else {
            send_and_check(server.arg(F("plain")));
            server.send(200, F("text/plain"), F("Sent"));
        }
    });

    server.on(UriBraces(F("/send/{}")), [](){
        String sizestr = server.pathArg(0);
        long size = sizestr.toInt();
        if (server.method() != HTTP_POST)
            server.send(405, F("text/plain"), F("Method Not Allowed"));
        else {
            Serial.print(F("g"));
            print_pad5(size);
            // An opportunistic check for OOM. It may not work.
            delay(0);
            if (Serial.read() == '-') {
                server.send(500, F("text/plain"), F("Out of memory"));
            }
            else {
                send_and_check(server.arg(F("plain")));
                server.send(200, F("text/plain"), F("Sent ") + sizestr + F(" bytes."));
            }
        }
    });

    server.on(F("/clear_buffer"), [](){
        Serial.print(F("c"));
        server.send(200, F("text/plain"), F("Buffer cleared."));
    });

    server.on(F("/clear_tasks"), [](){
        Serial.print(F("s"));
        server.send(200, F("text/plain"), F("Background tasks cleared."));
    });

    server.on(F("/play_sent"), [](){
        Serial.print(F("P"));
        server.send(200, F("text/plain"), F("Playing background audio."));
    });

    server.on(F("/play_rec"), [](){
        Serial.print(F("R"));
        server.send(200, F("text/plain"), F("Playing recorded audio."));
    });

    server.on(UriBraces(F("/blink/{}")), [](){
        String intstr = server.pathArg(0);
        long interval = intstr.toInt();
        Serial.print(F("B"));
        print_pad5(interval);
        server.send(200, F("text/plain"),
                    F("Toggling every ") + String(interval * 100) +
                        F(" milliseconds."));
    });

    server.begin();
    MDNS.addService(F("http"), F("tcp"), 80);
}

void loop(void) {
    MDNS.update();
    server.handleClient();
}
