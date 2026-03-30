cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico_w)

include(pico_sdk_import.cmake)

project(keyboard_bridge C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(keyboard_bridge
        src/main.c
        src/hid_keyboard.c
        src/vt100_map.c
)

target_include_directories(keyboard_bridge PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(keyboard_bridge
        pico_stdlib
        pico_cyw43_arch_none
        pico_btstack_ble
        pico_btstack_classic
        pico_btstack_cyw43
)

# Disable USB output, enable UART output
pico_enable_stdio_usb(keyboard_bridge 0)
pico_enable_stdio_uart(keyboard_bridge 0)

pico_add_extra_outputs(keyboard_bridge)
