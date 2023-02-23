//! Serve on a web interface to control The Kit
//! Set `stty -F /dev/ttyACM0 -hupcl` in a udev rule first
//
// Copyright 2021 Zhang Maiyun <me@myzhangll.xyz
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <https://www.gnu.org/licenses/>.

use std::fs::OpenOptions;
use std::io::{Read, Write};
use tiny_http::{Header, Response, Server};

/// Read one line from a `Read`, discarding the trailing newline
fn unbuffered_readline<R: Read>(mut file: R) -> String {
    let mut buffer = [0; 1];
    let mut resp = String::new();
    while let Ok(n) = file.read(&mut buffer) {
        // Stop reading when line ends
        if buffer[0] as char == '\n' {
            break;
        }
        // Stop if the stream is empty
        if n == 0 {
            break;
        }
        resp.push(buffer[0] as char);
    }
    resp
}

fn main() {
    let server = Server::http("0.0.0.0:8780").unwrap();

    for request in server.incoming_requests() {
        let url = request.url();
        println!("Request at {}", url);
        if url == "/" {
            let header = Header::from_bytes(&b"Location"[..], &b"/dashboard"[..]).unwrap();
            let mut response = Response::empty(302);
            response.add_header(header);
            request.respond(response).ok();
            continue;
        }
        if url.starts_with("/dashboard") {
            let header = Header::from_bytes(&b"Content-Type"[..], &b"text/html"[..]).unwrap();
            let mut response = Response::from_string(DASHBOARD);
            response.add_header(header);
            request.respond(response).ok();
            continue;
        }

        // Run the command
        let command = &url[1..];
        let response_string = match OpenOptions::new()
            .read(true)
            .write(true)
            .open("/dev/ttyACM0")
        {
            Ok(mut tty) => {
                if let Err(error) = writeln!(&mut tty, "{}", command) {
                    format!("{{\"error\": \"{:?}\"}}", error)
                } else {
                    // Get the response
                    unbuffered_readline(&tty)
                }
            }
            Err(error) => {
                format!("{{\"error\": \"{:?}\"}}", error)
            }
        };
        println!("Response is: {}", response_string);
        let header = Header::from_bytes(&b"Content-Type"[..], &b"application/json"[..]).unwrap();
        let mut response = Response::from_string(&response_string);
        response.add_header(header);
        request.respond(response).ok();
    }
}

const DASHBOARD: &str = r#"
<!DOCTYPE html>
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
    </style>
</head>
<body>
    <h1> The Kit Controller </h1>

    <button id="turnon" type="button" onclick="lightSwitch(true)"> Turn Light On </button>
    <button id="turnoff" type="button" onclick="lightSwitch(false)"> Turn Light Off </button>
    <button id="playaudio" type="button" onclick="playAudio()"> Play Audio </button>
    <button id="getinfo" type="button" onclick="getInfo()"> Get System Information </button>

    <pre id="resblk"></pre>

    <script>
        function xhrGlue(endpoint) {
            var xhttp = new XMLHttpRequest();
            xhttp.onreadystatechange = function() {
                if (this.readyState == 4 && this.status == 200) {
                    const obj = JSON.parse(this.responseText);
                    document.getElementById("resblk").innerHTML = JSON.stringify(obj, null, 2);
                }
            };
            xhttp.open("GET", endpoint, true);
            xhttp.send();
        }
        function lightSwitch(on) {
            xhrGlue("/light_" + (on ? "on" : "off"));
        }
        function playAudio() {
            xhrGlue("/play");
        }
        function getInfo() {
            xhrGlue("/get_info");
        }
</script>

</body>
</html>
"#;
