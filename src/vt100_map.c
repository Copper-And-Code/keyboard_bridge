#include "vt100_map.h"
#include <string.h>

// ASCII characters for unshifted keys a-z mapped from HID usage 0x04-0x1D
static const char lower_alpha[] = "abcdefghijklmnopqrstuvwxyz";

// ASCII characters for unshifted number row: 1-9, 0
static const char num_row_unshifted[] = "1234567890";

// ASCII characters for shifted number row: !@#$%^&*()
static const char num_row_shifted[] = "!@#$%^&*()";

// Punctuation keys mapped from HID usage 0x2D-0x38 (unshifted)
static const char punct_unshifted[] = {
    '-',    // 0x2D MINUS
    '=',    // 0x2E EQUAL
    '[',    // 0x2F LEFT_BRACKET
    ']',    // 0x30 RIGHT_BRACKET
    '\\',   // 0x31 BACKSLASH
    0,      // 0x32 (non-US #, skip)
    ';',    // 0x33 SEMICOLON
    '\'',   // 0x34 APOSTROPHE
    '`',    // 0x35 GRAVE_ACCENT
    ',',    // 0x36 COMMA
    '.',    // 0x37 PERIOD
    '/',    // 0x38 SLASH
};

// Punctuation keys (shifted)
static const char punct_shifted[] = {
    '_',    // 0x2D
    '+',    // 0x2E
    '{',    // 0x2F
    '}',    // 0x30
    '|',    // 0x31
    0,      // 0x32
    ':',    // 0x33
    '"',    // 0x34
    '~',    // 0x35
    '<',    // 0x36
    '>',    // 0x37
    '?',    // 0x38
};

// VT100 escape sequences for special keys
typedef struct {
    uint8_t usage_code;
    const char *sequence;
} vt100_special_key_t;

static const vt100_special_key_t special_keys[] = {
    { HID_KEY_UP_ARROW,     "\x1B[A"  },   // Cursor Up
    { HID_KEY_DOWN_ARROW,   "\x1B[B"  },   // Cursor Down
    { HID_KEY_RIGHT_ARROW,  "\x1B[C"  },   // Cursor Right
    { HID_KEY_LEFT_ARROW,   "\x1B[D"  },   // Cursor Left
    { HID_KEY_HOME,         "\x1B[H"  },   // Home
    { HID_KEY_END,          "\x1B[F"  },   // End
    { HID_KEY_INSERT,       "\x1B[2~" },   // Insert
    { HID_KEY_DELETE,       "\x1B[3~" },   // Delete
    { HID_KEY_PAGE_UP,      "\x1B[5~" },   // Page Up
    { HID_KEY_PAGE_DOWN,    "\x1B[6~" },   // Page Down
    { HID_KEY_F1,           "\x1BOP"  },   // F1
    { HID_KEY_F2,           "\x1BOQ"  },   // F2
    { HID_KEY_F3,           "\x1BOR"  },   // F3
    { HID_KEY_F4,           "\x1BOS"  },   // F4
    { HID_KEY_F5,           "\x1B[15~"},   // F5
    { HID_KEY_F6,           "\x1B[17~"},   // F6
    { HID_KEY_F7,           "\x1B[18~"},   // F7
    { HID_KEY_F8,           "\x1B[19~"},   // F8
    { HID_KEY_F9,           "\x1B[20~"},   // F9
    { HID_KEY_F10,          "\x1B[21~"},   // F10
    { HID_KEY_F11,          "\x1B[23~"},   // F11
    { HID_KEY_F12,          "\x1B[24~"},   // F12
};

#define NUM_SPECIAL_KEYS (sizeof(special_keys) / sizeof(special_keys[0]))

size_t vt100_map_key(uint8_t usage_code, uint8_t modifiers, uint8_t *out_buf) {
    bool shift = (modifiers & HID_MOD_SHIFT) != 0;
    bool ctrl  = (modifiers & HID_MOD_CTRL)  != 0;

    // Alphabetic keys (A-Z): HID 0x04 - 0x1D
    if (usage_code >= HID_KEY_A && usage_code <= HID_KEY_Z) {
        uint8_t idx = usage_code - HID_KEY_A;
        if (ctrl) {
            // Ctrl+A = 0x01, Ctrl+Z = 0x1A
            out_buf[0] = (uint8_t)(idx + 1);
            return 1;
        }
        char ch = lower_alpha[idx];
        if (shift) {
            ch = ch - 'a' + 'A';
        }
        out_buf[0] = (uint8_t)ch;
        return 1;
    }

    // Number row: HID 0x1E - 0x27
    if (usage_code >= HID_KEY_1 && usage_code <= HID_KEY_0) {
        uint8_t idx = usage_code - HID_KEY_1;
        if (ctrl) {
            return 0;  // No Ctrl+number mapping in VT100
        }
        out_buf[0] = (uint8_t)(shift ? num_row_shifted[idx] : num_row_unshifted[idx]);
        return 1;
    }

    // Enter
    if (usage_code == HID_KEY_ENTER) {
        out_buf[0] = '\r';
        return 1;
    }

    // Escape
    if (usage_code == HID_KEY_ESCAPE) {
        out_buf[0] = 0x1B;
        return 1;
    }

    // Backspace -> DEL (0x7F) as VT100 expects
    if (usage_code == HID_KEY_BACKSPACE) {
        out_buf[0] = 0x7F;
        return 1;
    }

    // Tab
    if (usage_code == HID_KEY_TAB) {
        out_buf[0] = '\t';
        return 1;
    }

    // Space
    if (usage_code == HID_KEY_SPACE) {
        if (ctrl) {
            out_buf[0] = 0x00;  // Ctrl+Space = NUL
            return 1;
        }
        out_buf[0] = ' ';
        return 1;
    }

    // Punctuation keys: HID 0x2D - 0x38
    if (usage_code >= HID_KEY_MINUS && usage_code <= HID_KEY_SLASH) {
        uint8_t idx = usage_code - HID_KEY_MINUS;
        if (idx < sizeof(punct_unshifted)) {
            char ch = shift ? punct_shifted[idx] : punct_unshifted[idx];
            if (ch == 0) {
                return 0;
            }
            // Ctrl+Backslash = 0x1C (File Separator)
            if (ctrl && usage_code == HID_KEY_BACKSLASH) {
                out_buf[0] = 0x1C;
                return 1;
            }
            // Ctrl+[ = ESC (0x1B)
            if (ctrl && usage_code == HID_KEY_LEFT_BRACKET) {
                out_buf[0] = 0x1B;
                return 1;
            }
            // Ctrl+] = GS (0x1D)
            if (ctrl && usage_code == HID_KEY_RIGHT_BRACKET) {
                out_buf[0] = 0x1D;
                return 1;
            }
            out_buf[0] = (uint8_t)ch;
            return 1;
        }
    }

    // Special keys (arrows, function keys, etc.) -> VT100 escape sequences
    for (size_t i = 0; i < NUM_SPECIAL_KEYS; i++) {
        if (special_keys[i].usage_code == usage_code) {
            size_t len = strlen(special_keys[i].sequence);
            memcpy(out_buf, special_keys[i].sequence, len);
            return len;
        }
    }

    return 0;
}
