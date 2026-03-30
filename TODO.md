# TODO — Build & Run Procedure

Step-by-step guide to compile the firmware and run it on a Raspberry Pi Pico W.

## 1. Install the toolchain

### On Ubuntu / Debian

```bash
sudo apt update
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi \
    build-essential libstdc++-arm-none-eabi-newlib git
```

### On macOS (Homebrew)

```bash
brew install cmake armmbed/formulae/arm-none-eabi-gcc
```

### On Windows

1. Install [ARM GCC toolchain](https://developer.arm.com/downloads/-/gnu-rm).
2. Install [CMake](https://cmake.org/download/).
3. Install [Git for Windows](https://gitforwindows.org/).
4. Use a MinGW or MSYS2 shell, or build from the Pico SDK's Visual Studio Developer Command Prompt.

## 2. Get the Pico SDK

```bash
cd ~
git clone https://github.com/raspberrypi/pico-sdk.git --branch master
cd pico-sdk
git submodule update --init
```

The BTstack submodule (under `lib/btstack`) is required for Bluetooth support and is pulled automatically by `git submodule update --init`.

Set the environment variable:

```bash
export PICO_SDK_PATH=~/pico-sdk
```

You can add this to your `~/.bashrc` or `~/.zshrc` to make it permanent.

## 3. Clone this repository

```bash
git clone https://github.com/Copper-And-Code/keyboard_bridge.git
cd keyboard_bridge
```

## 4. Build the firmware

```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

On success, this produces:

- `keyboard_bridge.uf2` — the firmware image to flash
- `keyboard_bridge.elf` — ELF binary (useful for debugging with SWD/OpenOCD)

### Troubleshooting build issues

| Problem | Solution |
|---------|----------|
| `PICO_SDK_PATH not set` | Export the variable: `export PICO_SDK_PATH=~/pico-sdk` |
| `arm-none-eabi-gcc not found` | Install the ARM toolchain (see step 1) |
| BTstack headers missing | Run `git submodule update --init` inside the Pico SDK directory |
| CMake version too old | Upgrade to CMake >= 3.13 |

## 5. Flash the Pico W

1. **Unplug** the Pico W from USB.
2. **Hold the BOOTSEL button** (the white button on the board).
3. **While holding BOOTSEL**, plug the Pico W into your computer via USB.
4. Release the button — a USB mass storage drive named **RPI-RP2** appears.
5. Copy the firmware:

```bash
cp build/keyboard_bridge.uf2 /media/$USER/RPI-RP2/
```

On macOS:
```bash
cp build/keyboard_bridge.uf2 /Volumes/RPI-RP2/
```

6. The Pico W reboots automatically and starts running the firmware.

## 6. Wire the UART connection

Connect the Pico W to your target host device:

| Pico W Pin | Wire to           |
|------------|--------------------|
| GP0 (TX)   | Host RX            |
| GP1 (RX)   | Host TX (optional) |
| GND        | Host GND           |

Power the Pico W via VSYS (5V) or VBUS (USB).

## 7. Test with a serial terminal

If using a USB-to-serial adapter for testing, open a terminal:

```bash
# Linux
minicom -b 115200 -D /dev/ttyUSB0

# or with screen
screen /dev/ttyUSB0 115200

# macOS
screen /dev/tty.usbserial-* 115200
```

## 8. Pair a Bluetooth keyboard

1. Power on the Pico W — the **on-board LED blinks** (scanning for keyboards).
2. Put your Bluetooth keyboard into **pairing mode** (usually by holding a pairing button until a LED blinks).
3. The Pico W discovers the keyboard and connects automatically.
4. The **LED turns solid** once connected.
5. Type on the keyboard — characters and VT100 escape sequences appear on the UART.

### If the keyboard requires a PIN

Some older keyboards require a legacy PIN for pairing. The firmware responds with the default PIN `0000`. Type `0000` on the keyboard and press Enter.

## 9. Debug output (optional)

Debug messages (`printf`) are routed to USB CDC by default when `stdio_init_all()` is called. To see them:

1. Connect the Pico W via USB.
2. Open the USB serial port:

```bash
# Linux (usually /dev/ttyACM0)
minicom -b 115200 -D /dev/ttyACM0

# macOS
screen /dev/tty.usbmodem* 115200
```

You will see messages like:
```
=== Keyboard Bridge ===
Bluetooth Keyboard -> VT100 UART

[UART] Initialized at 115200 8N1 on GP0/GP1
[BT] CYW43 initialized.
[BT] Bluetooth stack ready.
[BT] Scanning for Bluetooth keyboards...
[BT] Found keyboard: AA:BB:CC:DD:EE:FF (CoD: 0x002540)
[BT] Connecting to AA:BB:CC:DD:EE:FF...
[BT] Keyboard connected! (cid: 0x0001)
```

## 10. Re-flash / update

To flash new firmware, repeat step 5: hold BOOTSEL, plug USB, copy the new `.uf2` file. No special tools or debugger required.
