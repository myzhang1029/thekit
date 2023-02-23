add_executable(thekit4_pico_w thekit4_pico_w.c temperature.c light.c ntp.c tasks.c http_server.c wifi.c)

target_link_libraries(thekit4_pico_w
    pico_stdlib
    hardware_adc hardware_pwm hardware_rtc
    pico_lwip_mdns
    pico_cyw43_arch_lwip_threadsafe_background
)

target_include_directories(thekit4_pico_w PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/..
)

# create map/bin/hex file etc.
pico_add_extra_outputs(thekit4_pico_w)
pico_enable_stdio_usb(thekit4_pico_w 1)
pico_enable_stdio_uart(thekit4_pico_w 0)
