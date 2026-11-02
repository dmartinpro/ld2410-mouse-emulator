# LD2410 mouse emulator

## What it does

An ESP32-S3 and an LD2410B presence radar act as a USB HID mouse.

While someone is present, the pointer moves 1 px right and 1 px back every 30 s (`JIGGLE_MS`). This keeps the screensaver and the idle lock from starting.

Nothing moves when the room is empty. If no valid radar frame arrives for 5 s (`TIMEOUT_MS`), the sketch treats the room as empty.

Other behaviors:

- **Corner push at boot.** 3 s after boot (time for USB to enumerate), the pointer goes to the top-left corner. A HID mouse is relative only. The sketch sends 60 moves of -127,-127 and the OS clamps at 0,0. Raise the count for very large screens.
- **Hourly corner push.** While someone is present, the push repeats every hour (`CORNER_MS`). It never runs while the room is empty, so the mouse does not wake an unattended screen.
- **Daily restart.** The ESP32 restarts itself every 24 h (`REBOOT_MS`). It then does the boot-time corner push again.
- **Radar auto-restart.** If no valid frame arrives for `TIMEOUT_MS`, the sketch sends "enable configuration" (0xFF), then "restart module" (0xA3), to the radar. It retries every 10 s. The HLKRadarTool phone app can leave the radar in configuration mode. The radar then stops sending data frames. This was reproduced, and the restart fixed it.
- **Status LED.** The board RGB LED (`RGB_BUILTIN`) shows the state at low brightness (16/255). Green: present. Off: absent. Red: radar silent. `RGB_BUILTIN` is GPIO48 on most DevKitC-1 boards. Other revisions may use GPIO38.

Target host: an Ubuntu laptop. The board is flashed from a macOS machine.

## Hardware

### LD2410B radar

- HLK-LD2410B by Hi-Link.
- 24 GHz FMCW mmWave radar.
- Detects moving and stationary people. Static presence works through micro-motion such as breathing.
- Range: up to about 6 m for moving targets, about 4-5 m for static targets.
- Gate resolution: 0.75 m, 9 gates.
- Supply: datasheet says 5-12 V on pad VCC.
- UART: 3.3 V logic, 256000 baud, 8N1 by default.
- Also has an OUT pin (digital presence). This project does not use it.
- Bluetooth is built in. Use the HLKRadarTool phone app to tune it.
- Long, narrow blue board with a 5-pad header on one short edge.

Radar notes:

- The radar itself holds the presence state for its "unmanned duration". The default is about 5 s.
- The author set it to 2 s in HLKRadarTool.
- A lower value makes absence faster. It may flicker when you sit still. A flicker resets the 30 s nudge timer.
- Settings stored in the radar survive restarts.

### ESP32-S3 dev board

- DevKitC-1 style board with two USB-C ports.
- Dual-core Xtensa LX7, up to 240 MHz.
- Wi-Fi and BLE 5.
- 3.3 V GPIO.
- Native USB-OTG on GPIO19/20. This port becomes the HID mouse.
- The second USB-C port goes to a USB-UART bridge. It is used for flashing and the console.

| Port | Use | Plugged into |
|---|---|---|
| USB (native, HID) | Acts as the mouse | Ubuntu laptop (normal use) |
| UART (flash) | Flashing and serial console | macOS machine (when flashing) |

## Wiring

![wiring](wiring.svg)

| LD2410B | ESP32-S3 |
|---|---|
| VCC | 5V pin |
| GND | GND |
| UART Rx | GPIO17 (ESP32 TX) |
| UART Tx | GPIO18 (ESP32 RX, `Serial1`) |
| OUT | not connected |

- TX and RX cross over. Sensor TX goes to the ESP32 RX pin. Sensor RX goes to the ESP32 TX pin.
- Pad order from the vendor datasheet (section 4.2): VCC, GND, UART Rx, UART Tx, OUT, top to bottom with the antenna on the left.

Power:

- 5 V is the datasheet rating. The diagram uses the 5V pin.
- The author's unit has also run from 3V3 without problems. That is out of spec.
- The earlier stalls came from the radar left in configuration mode by the app. They were not a power problem.
- Some ESP32-S3 boards do not feed the 5V header pin from USB. A solder jumper (for example the USB OTG / IN-OUT pad) must be bridged. Check the pin with a multimeter.

## How it is implemented

The sketch is `ld2410-mouse-emulator.ino`. It uses `USB.h` and `USBHIDMouse.h` from Arduino-ESP32 (TinyUSB). It needs no external library.

`loop()` works in this order:

1. `Serial1` reads bytes from the radar.
2. A small state machine matches the frame header `F4 F3 F2 F1`.
3. It reads a 2-byte little-endian length.
4. It reads the data and the footer `F8 F7 F6 F5`.
5. It accepts data frames of type `0x02` (basic) or `0x01` (engineering) with marker `0xAA`.
6. The target-state byte decides presence: 0 none, 1 moving, 2 static, 3 both. Anything other than 0 means present.
7. Each accepted frame records `lastFrame`.
8. After `REBOOT_MS` since boot, the board restarts.
9. Once, after 3 s, it does the boot-time corner push.
10. If `TIMEOUT_MS` passes with no accepted frame, the state becomes absent. The radar restart commands go out, every 10 s.
11. The RGB LED is updated.
12. If present and `CORNER_MS` has elapsed, it pushes the pointer to the corner. Otherwise, if `JIGGLE_MS` has elapsed, it calls `Mouse.move(1, 0)`, waits `delay(20)`, then calls `Mouse.move(-1, 0)`.

`lastMove` is reset on every pass while absent. The first nudge therefore comes 30 s after someone appears.

| Constant | Default | Meaning | Tuning |
|---|---|---|---|
| `JIGGLE_MS` | 30000 | Interval between nudges while present | Lower it if your idle timeout is under 30 s. |
| `TIMEOUT_MS` | 5000 | No valid frame for this long means absent, and triggers radar restart | Raise it if the state flickers. Lower it to react faster. |
| `CORNER_MS` | 3600000 | Interval between corner pushes while present (1 h) | Lower it if the pointer drifts from the corner sooner. |
| `REBOOT_MS` | 86400000 | Self-restart interval (24 h) | Raise it, or use a huge value to effectively disable it. |

## Debug sketch

`debug/ld2410-debug/ld2410-debug.ino` prints one line per second on the USB serial port. It has no HID code. Example:

```
PRESENCE | 0.98 m | mov 0.98m e52 | sta 0.00m e0 | 10 fps
```

- Status: `PRESENCE`, `--------` (absent), or `NO FRAME` (no valid frame for 3 s).
- First distance: detection distance. It only means something while `PRESENCE`.
- Then moving and static distance, each with its energy (`e`).
- Last: frames per second. A healthy radar gives 10.
- It has the same radar auto-restart and prints `restart sent` when it fires.

Build and upload with this FQBN (USB-JTAG serial). Uploads reset the board by themselves. No BOOT/RESET is needed:

```
arduino-cli compile -b esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc debug/ld2410-debug
```

Watch the output:

```
screen /dev/cu.usbmodemXXXX 115200
```

Quit with Ctrl-A, K, Y. A leftover `screen` session blocks uploads. Terminal lines end with CRLF.

## Build and flash

Host: macOS, `arduino-cli`, esp32 core 3.3.x installed.

Compile:

```
arduino-cli compile -b esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc .
```

`USBMode=default` selects USB-OTG (TinyUSB). HID needs it.

Find the port with `arduino-cli board list`. Plug the cable into the UART USB-C port. Then upload:

```
arduino-cli upload -b esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc -p <PORT> .
```

If uploads drop, add `UploadSpeed=115200` to the FQBN. Higher speeds were unreliable on the author's board.

Flashing from a running TinyUSB firmware needs boot mode. Hold BOOT, tap RESET, release BOOT. After the upload, tap RESET once to start the new program.

If the board already holds another program, back up the flash first with `esptool read-flash`. The Arduino core ships esptool.

Then move the cable to the native USB port. Plug it into the Ubuntu machine.

Ubuntu note: flashing from Linux needs `sudo usermod -aG dialout $USER`. Log out and in again afterwards.

## Limitations

- Tested on macOS: boot-time corner push, nudge, absence handling, radar auto-restart.
- Untested on hardware: hourly corner push, 24 h restart, red LED state.
- One HID unit moves the pointer about 0.2 px on the author's Mac.
- Some Wayland compositors may ignore tiny moves. If the screensaver still starts, raise the move to 2-3 units.
- No data persistence.
- No configuration over UART besides the restart.
