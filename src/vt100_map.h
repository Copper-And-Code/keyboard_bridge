#ifndef VT100_MAP_H
#define VT100_MAP_H

#include <stdint.h>
#include <stddef.h>

// HID usage codes for special keys (from USB HID Usage Tables)
#define HID_KEY_A               0x04
#define HID_KEY_Z               0x1D
#define HID_KEY_1               0x1E
#define HID_KEY_9               0x26
#define HID_KEY_0               0x27
#define HID_KEY_ENTER           0x28
#define HID_KEY_ESCAPE          0x29
#define HID_KEY_BACKSPACE       0x2A
#define HID_KEY_TAB             0x2B
#define HID_KEY_SPACE           0x2C
#define HID_KEY_MINUS           0x2D
#define HID_KEY_EQUAL           0x2E
#define HID_KEY_LEFT_BRACKET    0x2F
#define HID_KEY_RIGHT_BRACKET   0x30
#define HID_KEY_BACKSLASH       0x31
#define HID_KEY_SEMICOLON       0x33
#define HID_KEY_APOSTROPHE      0x34
#define HID_KEY_GRAVE_ACCENT    0x35
#define HID_KEY_COMMA           0x36
#define HID_KEY_PERIOD          0x37
#define HID_KEY_SLASH           0x38
#define HID_KEY_CAPS_LOCK       0x39
#define HID_KEY_F1              0x3A
#define HID_KEY_F2              0x3B
#define HID_KEY_F3              0x3C
#define HID_KEY_F4              0x3D
#define HID_KEY_F5              0x3E
#define HID_KEY_F6              0x3F
#define HID_KEY_F7              0x40
#define HID_KEY_F8              0x41
#define HID_KEY_F9              0x42
#define HID_KEY_F10             0x43
#define HID_KEY_F11             0x44
#define HID_KEY_F12             0x45
#define HID_KEY_PRINT_SCREEN    0x46
#define HID_KEY_SCROLL_LOCK     0x47
#define HID_KEY_PAUSE           0x48
#define HID_KEY_INSERT          0x49
#define HID_KEY_HOME            0x4A
#define HID_KEY_PAGE_UP         0x4B
#define HID_KEY_DELETE          0x4C
#define HID_KEY_END             0x4D
#define HID_KEY_PAGE_DOWN       0x4E
#define HID_KEY_RIGHT_ARROW     0x4F
#define HID_KEY_LEFT_ARROW      0x50
#define HID_KEY_DOWN_ARROW      0x51
#define HID_KEY_UP_ARROW        0x52

// HID modifier bit masks
#define HID_MOD_LEFT_CTRL       0x01
#define HID_MOD_LEFT_SHIFT      0x02
#define HID_MOD_LEFT_ALT        0x04
#define HID_MOD_LEFT_GUI        0x08
#define HID_MOD_RIGHT_CTRL      0x10
#define HID_MOD_RIGHT_SHIFT     0x20
#define HID_MOD_RIGHT_ALT       0x40
#define HID_MOD_RIGHT_GUI       0x80

#define HID_MOD_SHIFT           (HID_MOD_LEFT_SHIFT | HID_MOD_RIGHT_SHIFT)
#define HID_MOD_CTRL            (HID_MOD_LEFT_CTRL | HID_MOD_RIGHT_CTRL)
#define HID_MOD_ALT             (HID_MOD_LEFT_ALT | HID_MOD_RIGHT_ALT)

// Maximum length of a VT100 escape sequence (including null terminator)
#define VT100_SEQ_MAX_LEN       8

/**
 * Convert a HID keyboard usage code + modifiers into a VT100 byte sequence.
 *
 * @param usage_code  HID usage code (0x04..0x52 typically)
 * @param modifiers   HID modifier bitmask
 * @param out_buf     Output buffer (at least VT100_SEQ_MAX_LEN bytes)
 * @return            Number of bytes written to out_buf (0 = no mapping)
 */
size_t vt100_map_key(uint8_t usage_code, uint8_t modifiers, uint8_t *out_buf);

#endif // VT100_MAP_H
