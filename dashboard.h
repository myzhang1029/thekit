"<!DOCTYPE html>" \
"<html lang=\"en\">" \
"<head>" \
"    <meta name=\"viewport\" content=\"width=device-width, " \
"initial-scale=1\" />" \
"    <meta charset=\"UTF-8\" />" \
"    <title> The Kit Controller </title>" \
"    <style>" \
".color1 {background-color: #fffba9;}" \
".color2 {background-color: #eb80fd;}" \
".color3 {background-color: #76ff9a;}" \
".color4 {background-color: #fffbf4;}" \
".color5 {background-color: #ff817b;}" \
"" \
"html {" \
"    background-image: " \
"url(\"https://myzhangll.xyz/assets/img/scenery/image1.jpg\");" \
"    background-size: 100vw 100vh;" \
"    text-align: center;" \
"}" \
"" \
"h1 {" \
"    color: #411f07;" \
"    font-family: fantasy;" \
"    font-weight: bold;" \
"    font-size: 1.5em;" \
"}" \
"" \
"#resblk {" \
"    max-width: 100vw;" \
"    text-align: start;" \
"    word-wrap: break-word;" \
"    overflow-x: auto;" \
"}" \
"" \
"button, .slider {" \
"    color: #00152c;" \
"    opacity: 0.7;" \
"    margin: 0.5vh 1vw;" \
"    border: 2px solid #bfe3ae;" \
"    border-radius: 40px;" \
"    font-family: sans-serif;" \
"    font-size: 1.5em;" \
"    height: 8vh;" \
"}" \
"" \
".slider {" \
"    -webkit-appearance: none;" \
"    outline: none;" \
"    -webkit-transition: .2s;" \
"    transition: opacity .2s;" \
"}" \
"" \
".slider:hover, button:hover, form:hover {" \
"    opacity: 1;" \
"}" \
"" \
".slider::-webkit-slider-thumb {" \
"    -webkit-appearance: none;" \
"    appearance: none;" \
"    width: 8vh;" \
"    height: 8vh;" \
"    border-radius: 40px;" \
"    background: #00152c;" \
"    cursor: pointer;" \
"}" \
"" \
".slider::-moz-range-thumb {" \
"    width: 8vh;" \
"    height: 8vh;" \
"    border-radius: 40px;" \
"    background: #00152c;" \
"    cursor: pointer;" \
"}" \
"" \
".reorg {" \
"    display: grid;" \
"    grid-template-columns: 50% 50%;" \
"    width: 100%;" \
"    vertical-align: middle;" \
"}" \
"" \
"form {" \
"    margin: auto;" \
"    font-size: 3em;" \
"    opacity: 0.7;" \
"}" \
"    </style>" \
"</head>" \
"<body>" \
"    <h1> The Kit Controller </h1>" \
"" \
"<div id=\"buttons\">" \
"    <!--div class=reorg>" \
"        <button class=\"turnon color1\" type=\"button\" " \
"onclick=\"lightSwitch(1, true)\">" \
"            Big Mikey I" \
"        </button>" \
"        <button class=\"turnoff color1\" type=\"button\" " \
"onclick=\"lightSwitch(1, false)\">" \
"            Big Mikey O" \
"        </button>" \
"    </div-->" \
"    <div class=reorg>" \
"        <button class=\"turnon color2\" type=\"button\" " \
"onclick=\"lightSwitch(2, true)\">" \
"            Stephanie I" \
"        </button>" \
"        <button class=\"turnoff color2\" type=\"button\" " \
"onclick=\"lightSwitch(2, false)\">" \
"            Stephanie O" \
"        </button>" \
"    </div>" \
"    <div class=\"reorg\">" \
"        <input type=\"range\" min=\"0\" max=\"100\" value=\"50\" " \
"class=\"slider color3\" id=\"light3_dimmer\" onchange=\"lightDim(3)\">" \
"        <button id=\"getinfo\" class=\"color5\" type=\"button\" " \
"onclick=\"getInfo()\">" \
"            Mikey Info" \
"        </button>" \
"    </div>" \
"    <div class=\"reorg\">" \
"        <button id=\"playrecorded\" class=\"color1\" type=\"button\" " \
"onclick=\"playRecorded()\">" \
"            Good Morning" \
"        </button>" \
"        <button id=\"playsent\" class=\"color1\" type=\"button\" " \
"onclick=\"playSent()\">" \
"            Play Audio" \
"        </button>" \
"    </div>" \
"    <form method=\"POST\" enctype=\"multipart/form-data\" " \
"action=\"/player\">" \
"        <input type=\"file\" name=\"audio\">" \
"        <br />" \
"        <input type=\"submit\" value=\"Play at Mikey's\">" \
"    </form>" \
"</div>" \
"" \
"    <pre id=\"resblk\"></pre>" \
"" \
"    <script>" \
"        function xhrGlue(endpoint) {" \
"            let xhttp = new XMLHttpRequest();" \
"            xhttp.onreadystatechange = function() {" \
"                if (this.readyState == 4) {" \
"                    let text;" \
"                    try {" \
"                        const obj = JSON.parse(this.responseText);" \
"                        text = JSON.stringify(obj, null, 2);" \
"                    } catch (err) {" \
"                        text = err.name + \": \" + err.message;" \
"                    }" \
"                    document.getElementById(\"resblk\").innerHTML = " \
"text;" \
"                }" \
"            };" \
"            xhttp.open(\"GET\", endpoint, true);" \
"            xhttp.send();" \
"        }" \
"        function lightDim(n) {" \
"            xhrGlue(\"/\" + n + \"light_dim?level=\" + " \
"document.getElementById(\"light\" + n + \"_dimmer\").value);" \
"        }" \
"        function lightSwitch(n, on) {" \
"            xhrGlue(\"/\" + n + \"light_\" + (on ? \"on\" : \"off\"));" \
"        }" \
"        function playRecorded() {" \
"            xhrGlue(\"/playR\");" \
"        }" \
"        function playSent() {" \
"            xhrGlue(\"/playP\");" \
"        }" \
"        function getInfo() {" \
"            xhrGlue(\"/get_info\");" \
"        }" \
"</script>" \
"" \
"</body>" \
"</html>"
