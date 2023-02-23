#!/usr/bin/env python3

"""A more versatile Python thekitd."""

import os
import subprocess
from math import e, log
from typing import Any, Dict, Tuple

import requests
from flask import Flask, redirect, request
from werkzeug.utils import secure_filename

UPLOAD_FOLDER = "/tmp"

DASHBOARD = eval(open("dashboard.h").read(), {}, {})

RH2 = "stephlight.local"
JSONResponse = Tuple[Dict[str, Any], int]


def _send_or(data: str, if_succeed: Dict[str, Any], remote_host: str) -> JSONResponse:
    """Send this data through or fail."""
    try:
        resp = requests.get("http://" + remote_host + "/" + data, timeout=5)
    except requests.exceptions.ConnectionError as exc:
        return {
            "success": False,
            "exception": str(exc),
        }, 500
    if resp.status_code == 200:
        return {**if_succeed, "message": resp.text}, 200
    return {
        "success": False,
        "error": resp.text,
        "error_code": resp.status_code,
    }, 500


def intensity_to_dcycle(intensity: float) -> int:
    real_intensity = e ** (intensity * log(101, e) / 100) - 1
    voltage = real_intensity * (19.2 - 7.845)/100 + 7.845
    if 7.845 < voltage <= 9.275:
        return int(281970 * (-7.664 + voltage))
    if 9.275 < voltage <= 13.75:
        return int(26520 * (6.959 + voltage))
    if 13.75 < voltage <= 16.88:
        return int(49485 * (-2.529 + voltage))
    if 16.88 < voltage <= 19.2:
        return min(int(21692 * (26.90 + voltage)), 1000000)
    return 0


app = Flask(__name__)
app.config["UPLOAD_FOLDER"] = UPLOAD_FOLDER


@app.route("/")
def _dashboard() -> Tuple[str, int]:
    return DASHBOARD, 200


@app.route("/2light_on")
def _2light_on() -> JSONResponse:
    return _send_or("on", {"light": True}, RH2)


@app.route("/2light_off")
def _2light_off() -> JSONResponse:
    return _send_or("off", {"light": False}, RH2)


@app.route("/3light_dim")
def _3light_dim() -> JSONResponse:
    value = request.args.get("level")
    light3_state = intensity_to_dcycle(float(value))
    open("/dev/thekit_pwm", "w").write(f"{light3_state}\n")
    return {"dim": True, "value": value}, 200


@app.route("/playR")
def _play_recorded() -> JSONResponse:
    return _send_or("play_rec", {"play": True}, RH2)


@app.route("/playP")
def _play_sent() -> JSONResponse:
    return _send_or("play_sent", {"play": True}, RH2)


@app.route("/player", methods=["POST"])
def play_music():
    if "audio" not in request.files:
        return {"error": "bad request", "reason": "no audio"}, 400
    audio = request.files["audio"]
    if not audio or not audio.filename:
        return {"error": "bad request", "reason": "audio is undefined"}, 400
    if audio.filename[-4:] in (".wav", ".m4a", ".mp4", ".aac"):
        filename = secure_filename(audio.filename)
        filename = os.path.join(app.config["UPLOAD_FOLDER"], filename)
        audio.save(filename)
        subprocess.run(
            ["ffplay", "-nodisp", "-autoexit", filename], check=True)
        os.remove(filename)
    return redirect("/")


app.run(host="0.0.0.0")
