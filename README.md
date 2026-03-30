# Keyboard Bridge

Bluetooth keyboard to UART bridge for Raspberry Pi Pico W. Pairs with a standard Bluetooth keyboard and outputs keystrokes as VT100-compatible sequences over UART at 115200 baud — turning a wireless keyboard into a serial terminal input device.

## Features

- **Bluetooth Classic HID Host** — automatically discovers and connects to nearby Bluetooth keyboards
- **VT100 escape sequences** — arrow keys, Home, End, Insert, Delete, Page Up/Down, F1–F12
- **Control characters** — Ctrl+A through Ctrl+Z, Ctrl+[, Ctrl+], Ctrl+\
- **Full ASCII printable set** — letters (with Shift and Caps Lock), numbers, punctuation
- **Auto-reconnect** — rescans automatically when a keyboard disconnects
- **LED status indicator** — blinking = scanning, solid = connected
- **SSP and legacy pairing** — supports Secure Simple Pairing and PIN-based pairing (default PIN: `0000`)

## Hardware Setup

```
Raspberry Pi Pico W
┌─────────────────┐
│              GP0 │──── UART TX ──> to host RX
│              GP1 │──── UART RX <── from host TX
│              GND │──── GND ─────── host GND
│              VSYS│──── 5V (power)
└─────────────────┘
```

| Pico W Pin | Function       | Direction |
|------------|----------------|-----------|
| GP0        | UART0 TX       | Output    |
| GP1        | UART0 RX       | Input     |
| GND        | Ground         | —         |
| VSYS       | 5V Power Input | —         |

**UART settings:** 115200 baud, 8 data bits, no parity, 1 stop bit (8N1).

## VT100 Key Mapping

| Key            | Output              |
|----------------|---------------------|
| Arrow Up       | `ESC [ A`           |
| Arrow Down     | `ESC [ B`           |
| Arrow Right    | `ESC [ C`           |
| Arrow Left     | `ESC [ D`           |
| Home           | `ESC [ H`           |
| End            | `ESC [ F`           |
| Insert         | `ESC [ 2 ~`         |
| Delete         | `ESC [ 3 ~`         |
| Page Up        | `ESC [ 5 ~`         |
| Page Down      | `ESC [ 6 ~`         |
| F1–F4          | `ESC O P` … `ESC O S` |
| F5–F12         | `ESC [ 15~` … `ESC [ 24~` |
| Backspace      | `DEL` (0x7F)        |
| Enter          | `CR` (0x0D)         |
| Tab            | `HT` (0x09)         |
| Escape         | `ESC` (0x1B)        |
| Ctrl+A … Ctrl+Z | 0x01 … 0x1A      |
| Ctrl+[         | `ESC` (0x1B)        |
| Ctrl+]         | `GS` (0x1D)        |
| Ctrl+\         | `FS` (0x1C)        |

## Building

### Prerequisites

- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) (>= 1.5.0)
- CMake (>= 3.13)
- ARM GCC toolchain (`arm-none-eabi-gcc`)

### Build steps

```bash
# Set the SDK path
export PICO_SDK_PATH=/path/to/pico-sdk

# Create build directory and compile
mkdir build && cd build
cmake ..
make -j$(nproc)
```

This produces `keyboard_bridge.uf2` in the build directory.

### Flashing

1. Hold the **BOOTSEL** button on the Pico W and plug it into USB.
2. It appears as a USB mass storage device.
3. Copy `build/keyboard_bridge.uf2` to the drive.
4. The Pico W reboots and starts scanning for keyboards.

## Usage

1. Flash the firmware to the Pico W.
2. Connect the UART pins (GP0 TX, GP1 RX) to your host device (e.g., an STM32, another microcontroller, or a USB-to-serial adapter).
3. Power on the Pico W — the LED blinks while scanning.
4. Put your Bluetooth keyboard in pairing mode.
5. The Pico W connects automatically (LED turns solid).
6. Keystrokes now appear on the UART as VT100 sequences.

## Architecture

```
┌──────────────┐     Bluetooth      ┌──────────────┐    UART 115200    ┌──────────┐
│   BT Keyboard │ ── HID Reports ──>│  Pico W      │ ── VT100 Seqs ──>│   Host   │
│              │                    │  (BTstack)   │                   │ Terminal │
└──────────────┘                    └──────────────┘                   └──────────┘
```

- **`main.c`** — Bluetooth initialization, GAP inquiry, HID Host connection management, LED status
- **`hid_keyboard.c`** — HID boot report parsing, key press detection (with rollover), Caps Lock handling
- **`vt100_map.c`** — HID usage code to VT100/ASCII conversion tables

## About This Project

This firmware was entirely written by [Claude](https://claude.ai), Anthropic's AI assistant, in a vibe coding session. The repository owner needed this accessory to bridge a Bluetooth keyboard to a serial VT100 terminal but did not have enough time to write the code from scratch — so Claude did it. The full implementation, including Bluetooth HID host integration, VT100 escape sequence mapping, and this documentation, was generated in a single conversational session.

## License

See [LICENSE](LICENSE) file.
