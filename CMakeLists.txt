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
pico_enable_stdio_usb(keyboard_bridge 1)
pico_enable_stdio_uart(keyboard_bridge 0)

pico_add_extra_outputs(keyboard_bridge)

# --- cyw43_test: minimal CYW43 test without BTstack ---
add_executable(cyw43_test
        src/cyw43_test.c
)

target_include_directories(cyw43_test PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(cyw43_test
        pico_stdlib
        pico_cyw43_arch_none
)

pico_enable_stdio_usb(cyw43_test 1)
pico_enable_stdio_uart(cyw43_test 0)

pico_add_extra_outputs(cyw43_test)

# --- board_test: minimal board test without CYW43 ---
add_executable(board_test
        src/board_test.c
)

target_include_directories(board_test PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(board_test
        pico_stdlib
)

pico_enable_stdio_usb(board_test 1)
pico_enable_stdio_uart(board_test 0)

pico_add_extra_outputs(board_test)
