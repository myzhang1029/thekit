#!/usr/bin/env python3
#
#  thekitd2.py
#
#  Copyright (C) 2019-2020 Zhang Maiyun <me@myzhangll.xyz>
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <https://www.gnu.org/licenses/>.
#

"""A moe versatile Python thekitd."""

import json
import sys
from datetime import datetime
from math import log
from pathlib import Path
from typing import Any, Dict, Tuple, Union

import serial
import smbus
from flask import Flask, request
from RPi import GPIO

DASHBOARD = """<!DOCTYPE html>
<html lang="en">
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <meta charset="UTF-8" />
    <title> The Kit Controller </title>
    <style>
h1, button {
    width: 100%;
    text-align: center;
    font-size: 2em;
}

#resblk {
    width: 100%;
}

button {
    background: black;
    color: white;
}

@media screen and (min-width: 660px) {
    #buttons {
        column-count: 2;
    }
}
    </style>
</head>
<body>
    <h1> The Kit Controller </h1>

    <div id="buttons">
        <button id="turnon" type="button" onclick="lightSwitch(1, true)">
            Turn Light 1 On
        </button>
        <button id="turnoff" type="button" onclick="lightSwitch(1, false)">
            Turn Light 1 Off
        </button>
        <button id="turnon" type="button" onclick="lightSwitch(2, true)">
            Turn Light 2 On
        </button>
        <button id="turnoff" type="button" onclick="lightSwitch(2, false)">
            Turn Light 2 Off
        </button>
        <button id="playrecorded" type="button" onclick="playRecorded()">
            Play Recorded Audio
        </button>
        <button id="playsent" type="button" onclick="playSent()">
            Play Sent Audio
        </button>
        <button id="getinfo" type="button" onclick="getInfo()">
            Get System Information
        </button>
    </div>

    <pre id="resblk"></pre>

    <script>
        function xhrGlue(endpoint) {
            let xhttp = new XMLHttpRequest();
            xhttp.onreadystatechange = function() {
                if (this.readyState == 4) {
                    let text;
                    try {
                        const obj = JSON.parse(this.responseText);
                        text = JSON.stringify(obj, null, 2);
                    } catch (err) {
                        text = err.name + ": " + err.message;
                    }
                    document.getElementById("resblk").innerHTML = text;
                }
            };
            xhttp.open("GET", endpoint, true);
            xhttp.send();
        }
        function lightSwitch(n, on) {
            xhrGlue("/" + n + "light_" + (on ? "on" : "off"));
        }
        function playRecorded() {
            xhrGlue("/playR");
        }
        function playSent() {
            xhrGlue("/playP");
        }
        function getInfo() {
            xhrGlue("/get_info");
        }
</script>

</body>
</html>"""


class TheKit:
    """TheKit unified interface."""

    LIGHT_PIN = 16
    ADC_ADDR = 0x48
    MAX_TRIES = 20
    T_R0 = 1e4
    T_T0 = 25.0 + 273.15
    T_RDIV = 1e4
    T_BETA = 3977
    BLE_SERIAL = "/dev/rfcomm0"

    def __init__(self) -> None:
        GPIO.setmode(GPIO.BCM)
        GPIO.setup(self.LIGHT_PIN, GPIO.OUT)
        self._light_state = bool(GPIO.input(self.LIGHT_PIN))

    def _get_raw_temp(self) -> int:
        i2c = smbus.SMBus(1)
        for _ in range(self.MAX_TRIES):
            try:
                data = i2c.read_word_data(self.ADC_ADDR, 0x00)
                break
            except OSError:
                pass
        i2c.close()
        return data

    def get_temp(self) -> float:
        """Get temperature from Arduino-as-ADC."""
        sensor = self._get_raw_temp()
        # VRDIV = (VAref / 1024.00) * sensorValue
        # NTC = RDIV * (VAref - VRDIV) / VRDIV;
        NTC = self.T_RDIV * (1024.0 / sensor - 1)
        # R = R0 * exp(beta/T - beta/T0)
        # ln(R/R0) + beta/T0 = beta/T
        T = self.T_BETA / (log(NTC / self.T_R0) + self.T_BETA / self.T_T0)
        return T - 273.15

    def set_light(self, new: bool) -> None:
        """Set the status of the light."""
        self._light_state = new
        GPIO.output(self.LIGHT_PIN, new)

    def get_light(self) -> bool:
        """Get lighting status."""
        return self._light_state

    def send_with_ack(self, data: Union[str, bytes]) -> None:
        """Send data to Bluetooth and receive echo as ACK."""
        with serial.Serial(self.BLE_SERIAL, 9600) as bluetooth:
            bluetooth.reset_input_buffer()
            if isinstance(data, bytes):
                for code in data:
                    byte = code.to_bytes(1, sys.byteorder)
                    bluetooth.write(byte)
                    assert byte == bluetooth.read()
            else:
                for char in data:
                    byte = char.encode()
                    bluetooth.write(byte)
                    assert byte == bluetooth.read()


def main() -> None:
    """Daemon entrypoint."""
    thekit = TheKit()
    app = Flask(__name__)

    def _send_or(
        data: Union[str, bytes], if_succeed: Dict[str, Any]
    ) -> Tuple[str, int]:
        """Send this data through bluetooth or fail."""
        try:
            thekit.send_with_ack(data)
            return json.dumps(if_succeed), 200
        except serial.serialutil.SerialException as exc:
            message = {
                "success": False,
                "type": type(exc).__name__,
                "error": str(exc)
            }
            return json.dumps(message), 500

    if len(sys.argv) == 2 and sys.argv[1] == "record":
        temp = thekit.get_temp()
        date = datetime.utcnow().replace(microsecond=0).isoformat() + ".00Z"
        data = json.dumps({"date": date, "tempC": temp})
        (Path.home() / "temperatures.ldjson").open("at").write(data + "\n")
        sys.exit(0)

    @app.route("/")
    def _dashboard() -> Tuple[str, int]:
        return DASHBOARD, 80

    @app.route("/get_info")
    def _get_info() -> Tuple[str, int]:
        temp = thekit.get_temp()
        light = thekit.get_light()
        return json.dumps({"light": light, "tempC": temp}), 200

    @app.route("/1light_on")
    def _light_on() -> Tuple[str, int]:
        thekit.set_light(True)
        return json.dumps({"light": True}), 200

    @app.route("/1light_off")
    def _light_off() -> Tuple[str, int]:
        thekit.set_light(False)
        return json.dumps({"light": False}), 200

    @app.route("/2light_on")
    def _ble_light_on() -> Tuple[str, int]:
        return _send_or("h", {"light": True})

    @app.route("/2light_off")
    def _ble_light_off() -> Tuple[str, int]:
        return _send_or("l", {"light": False})

    @app.route("/playR")
    def _play_recorded() -> Tuple[str, int]:
        return _send_or("R", {"play": True})

    @app.route("/playP")
    def _play_sent() -> Tuple[str, int]:
        return _send_or("P", {"play": True})

    @app.route("/send_cmd")
    def _send_cmd() -> Tuple[str, int]:
        data = request.data
        if not data:
            return json.dumps({"error": "Bad Request"}), 400
        return _send_or(data, {"sent": True})

    app.run(host="0.0.0.0")


if __name__ == "__main__":
    main()
