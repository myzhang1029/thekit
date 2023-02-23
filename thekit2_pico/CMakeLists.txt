add_executable(thekit_pico thekit_pico.c base64.c pcm.c)
add_definitions(-DPICO_MALLOC_PANIC=0)

# pull in common dependencies
target_link_libraries(thekit_pico hardware_pwm pico_stdlib)

# create map/bin/hex file etc.
pico_add_extra_outputs(thekit_pico)
pico_enable_stdio_usb(thekit_pico 1)
pico_enable_stdio_uart(thekit_pico 0)
