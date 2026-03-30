#ifndef HID_KEYBOARD_H
#define HID_KEYBOARD_H

#include <stdint.h>

// Maximum number of simultaneous keys in a HID boot report
#define HID_BOOT_REPORT_MAX_KEYS    6

// Boot protocol keyboard report structure
typedef struct {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[HID_BOOT_REPORT_MAX_KEYS];
} hid_boot_report_t;

/**
 * Initialize the HID keyboard handler.
 * Must be called after UART is configured.
 */
void hid_keyboard_init(void);

/**
 * Process a HID boot protocol keyboard report.
 * Detects newly pressed keys (comparing with previous report),
 * maps them to VT100 sequences, and sends them over UART.
 *
 * @param report  Pointer to the 8-byte HID boot keyboard report
 * @param len     Length of the report (expected: 8)
 */
void hid_keyboard_process_report(const uint8_t *report, uint16_t len);

#endif // HID_KEYBOARD_H
