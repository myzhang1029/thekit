# The Kit
The Kit is a collection of device/sensor management and remote control
solutions that I made at Pearson College UWC.

## First Gen
The first generation of The Kit is an Arduino program ([ArduinoTheKit](ArduinoTheKit/ArduinoTheKit.ino))
and a Rust daemon ([thekitd](thekitd/src/main.rs)) meant to be run on another Linux machine,
connected to the Arduino via serial.
Basically everything else is based on this interface.

## Second Gen
The second generation consists of a Raspberry Pi daemon ([thekitd2.py](thekitd2.py))
and a Raspberry Pi Pico ([thekit2_pico](thekit2_pico)) connected to WiFi via a ESP8266
([thekit_receiver](thekit_receiver/thekit_receiver.ino)).
There is a slightly more versatile version, [thekitd2.5.py](thekitd2.5.py).

## Third Gen
To minimize power consumption and CPU usage, the third generation is
a Linux Kernel Module ([thekit3_pwm.c](thekit3_pwm.c)) for Raspberry Pi and a corresponding
`inetd`-style HTTP daemon ([thekitd3.c](thekitd3.c)).

## Fourth Gen
The fourth generation is even more power-efficient because it is a Raspberry Pi Pico W
program with all the previous features ([thekit4_pico_w](thekit4_pico_w)). I had to
create it because the SD card on the Pi failed. I coded most of the server routines from
scratch based on LwIP.
