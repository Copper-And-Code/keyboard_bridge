#include "hid_keyboard.h"
#include "vt100_map.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include <string.h>
#include <stdio.h>

// UART instance used for VT100 output
#define BRIDGE_UART     uart0
#define UART_BAUD_RATE  115200
#define UART_TX_PIN     0
#define UART_RX_PIN     1

// Previous HID report for detecting key press/release transitions
static hid_boot_report_t prev_report;

// Flag to track if caps lock is active (toggled by caps lock key)
static bool caps_lock_active = false;

/**
 * Send a byte buffer over UART.
 */
static void uart_send(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uart_putc_raw(BRIDGE_UART, data[i]);
    }
}

/**
 * Check if a key code was present in the previous report.
 */
static bool key_was_pressed(uint8_t keycode) {
    for (int i = 0; i < HID_BOOT_REPORT_MAX_KEYS; i++) {
        if (prev_report.keys[i] == keycode) {
            return true;
        }
    }
    return false;
}

void hid_keyboard_init(void) {
    memset(&prev_report, 0, sizeof(prev_report));
    caps_lock_active = false;

    // Initialize UART
    uart_init(BRIDGE_UART, UART_BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    // 8N1 format
    uart_set_format(BRIDGE_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(BRIDGE_UART, false, false);
}

void hid_keyboard_process_report(const uint8_t *report, uint16_t len) {
    if (len < 8) {
        return;
    }

    hid_boot_report_t current;
    current.modifiers = report[0];
    current.reserved  = report[1];
    memcpy(current.keys, &report[2], HID_BOOT_REPORT_MAX_KEYS);

    // Process each key in the current report
    for (int i = 0; i < HID_BOOT_REPORT_MAX_KEYS; i++) {
        uint8_t keycode = current.keys[i];
        if (keycode == 0x00 || keycode == 0x01) {
            // 0x00 = no key, 0x01 = ErrorRollOver
            continue;
        }

        // Only process newly pressed keys (not held)
        if (key_was_pressed(keycode)) {
            continue;
        }

        // Handle Caps Lock toggle
        if (keycode == HID_KEY_CAPS_LOCK) {
            caps_lock_active = !caps_lock_active;
            continue;
        }

        // Build effective modifiers (apply caps lock to shift for alpha keys)
        uint8_t effective_mods = current.modifiers;
        if (caps_lock_active && keycode >= HID_KEY_A && keycode <= HID_KEY_Z) {
            effective_mods ^= HID_MOD_LEFT_SHIFT;
        }

        // Map to VT100 sequence and send over UART
        uint8_t vt100_buf[VT100_SEQ_MAX_LEN];
        size_t vt100_len = vt100_map_key(keycode, effective_mods, vt100_buf);
        if (vt100_len > 0) {
            uart_send(vt100_buf, vt100_len);
        }
    }

    // Save current report for next comparison
    memcpy(&prev_report, &current, sizeof(hid_boot_report_t));
}
