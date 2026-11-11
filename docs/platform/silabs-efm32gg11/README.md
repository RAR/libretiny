# Silicon Labs EFM32GG11

!!! warning "Validation status"
	This port was developed and tested on a single board, the Enel X JuiceBox 40. Since its last rework, the current code has only been compile-tested. It must be re-validated on hardware before a release.

## Introduction

EFM32 Giant Gecko 11 (EFM32GG11) is a family of Cortex-M4F microcontrollers. LibreTiny supports the EFM32GG11B820F2048 inside the **WGM160P** Wi-Fi module, which pairs it with a **WF200** Wi-Fi transceiver on an internal SDIO bus.

Features:

- 32-bit Cortex-M4F CPU (72 MHz, from the 50 MHz crystal through the DPLL)
- 2 MiB flash
- 512 KiB SRAM
- 802.11 b/g/n Wi-Fi, 2.4 GHz (WF200)
- no Bluetooth

Resources:

- [WGM160P datasheet](https://www.silabs.com/documents/public/data-sheets/wgm160p-datasheet.pdf)
- [WGM160P Hardware Design User's Guide (UG384)](https://www.silabs.com/documents/public/user-guides/ug384-wgm160p-hardware-design.pdf)
- [EFM32GG11 datasheet](https://www.silabs.com/documents/public/data-sheets/efm32gg11-datasheet.pdf)
- [Gecko SDK](https://github.com/SiliconLabs/gecko_sdk) : Official Silicon Labs SDK

## SDK

The `framework-silabs-gecko-sdk` package contains Gecko SDK 4.5.0 (emlib, device headers, FreeRTOS, lwIP, mbedTLS) and is built from the Gecko SDK rather than from LibreTiny's shared FreeRTOS and lwIP packages. The WF200 FMAC driver (`library-wfx-fmac-driver`) is a separate package. See `platform.json` for where both are downloaded from.

## Finding your board

{%
	include-markdown "../../inc/find-board.md"
%}

There are two boards:

- `generic-wgm160p` exposes the 31 GPIO pads of the WGM160P module as `D0` to `D30`, in pad order (see the board's page for the pad table). `Serial` is on PE6 (RX) and PE7 (TX).
- `wgm160p-juicebox-40` (Enel X JuiceBox 40) names the pins that product uses, `D0` to `D7`:

Pin | Arduino | Name        | Notes
----|---------|-------------|----------------------------------------
PB5 | `D0`    | `LED_R`     | RGB LED red, active-high, PWM
PB6 | `D1`    | `LED_G`     | RGB LED green, active-high, PWM
PD8 | `D2`    | `LED_B`     | RGB LED blue, active-high, PWM
PC4 | `D3`    | `BTN0`      |
PE6 | `D4`    | `RX0`       | `Serial` RX (USART0)
PE7 | `D5`    | `TX0`       | `Serial` TX (USART0)
PB3 | `D6`    |             |
PF11 | `D7`   |             |

The pins used by the WF200 inside the module (PE4, PE8-PE13, PA8, PA12, PF12) are not module pads and are not in any pin table. On the JuiceBox, PB13 and PB14 are used by its crystal.

## Features

Working:

- GPIO (`pinMode`, `digitalRead`, `digitalWrite`) and pin interrupts
- PWM (`analogWrite`) on PB5, PB6 and PD8 (the JuiceBox RGB LED pins)
- `Serial` on USART0 (PE6/PE7); data bits, parity and stop bits from the `begin()` frame configuration are applied
- Wi-Fi: station, soft AP and both at once, scan, auto-reconnect and events. The link to the WF200 uses Secure Link, which the module enforces. `WiFi.begin()` returns as soon as the join has been issued (its `WL_CONNECTED` return value means "issued", as on the other families); poll `WiFi.status()` or use `waitForConnectResult()`. A join that does not get an address within 20 s is ended and reported as failed. `softAP()` called right after `begin()` may wait up to 4 s for the join, so that the access point can follow the station's channel
- restart of the WF200 after a firmware exception: the radio is reset and brought up again, the station rejoins and the soft AP is started again. `WiFi.status()` is `WL_NO_SHIELD` meanwhile. After 3 failed or short-lived restarts the radio stays off until reboot
- key-value storage (FlashDB) in the `kvs` partition
- watchdog
- dual-bank OTA with rollback (see below)
- mDNS is compiled in (lwIP responder), but has not been run on hardware yet

Not implemented:

- `analogRead` / ADC
- `Wire` (I2C) and `SPI`
- deep sleep
- TLS (`LT_HAS_MBEDTLS` is 0), so there is no `WiFiClientSecure` or HTTPS. mbedTLS is built only for the WF200 Secure Link and MD5.
- Bluetooth (the hardware has none)

Limitations:

- Networks that only allow WPA3 cannot be joined (WPA2/WPA mixed networks can).
- Pin interrupts use the 16 EXTI lines of the GPIO peripheral. A pin may use the line with its own number, or any free line in its group of four (pins 0-3, 4-7, 8-11, 12-15, of any port). So at most four pins from the same group can have an interrupt at the same time; attaching a fifth fails with an error message. While Wi-Fi is running, the WF200 interrupt (PA8) takes one line of the group 8-11.
- The interrupt dispatcher is part of the base core. A `base` framework application must not define `GPIO_EVEN_IRQHandler` or `GPIO_ODD_IRQHandler`; it attaches handlers with `lt_gpio_irq_attach()` (`lt_gpio_irq.h`).
- `lt_get_device_mac()` is not the Wi-Fi MAC. It returns the factory EUI-48 of the chip (or an address made from its unique ID), and it is available before Wi-Fi starts. The WF200 has its own MAC addresses, which `WiFi.macAddress()` returns.
- The watchdog stays off until `lt_wdt_enable()` is called, except while a new OTA image is on trial (see below).

### Settings you may need to change

Setting                      | Default  | Where
-----------------------------|----------|-----------------------------------------
`configTOTAL_HEAP_SIZE`      | 256 KiB  | `base/config/FreeRTOSConfig.h`, override with `-DconfigTOTAL_HEAP_SIZE=...`
`LT_MAIN_TASK_STACK_SIZE`    | 8192 B   | `arduino/src/main.cpp`
`LT_OTA_AUTO_CONFIRM`        | 1        | `base/lt_family.h`
`LT_WIFI_STATUS_LED_DEFAULT` | off      | `arduino/libraries/WiFi/WiFiStatusLed.h`
`LT_WIFI_RC_JOIN_TIMEOUT_MS` | 20000    | `base/wifi/lt_wifi_reconnect.h`
`LT_WIFI_CHIP_RECOVERY`      | 1        | `arduino/libraries/WiFi/WiFiPrivate.h`; 0 leaves the radio off after a WF200 firmware exception
`LT_WIFI_RECOVERY_MAX_ATTEMPTS` | 3     | `base/wifi/lt_wifi_recovery.h`, with `LT_WIFI_RECOVERY_DELAY_MS` (2000) and `LT_WIFI_RECOVERY_STABLE_MS` (600000)

The Wi-Fi status LED shows the radio and connection state on the board's RGB LED. It is off by default, so that the LEDs stay available to the application. Enable it with `-D LT_WIFI_STATUS_LED_DEFAULT=1` or at run time with `ltWifiStatusLedEnable(true)`.

## Flash layout

The chip is programmed over SWD; there is no UART bootloader or download mode.

Partition | Offset     | Size    | Description
----------|------------|---------|-----------------------------------------------------
`boot`    | `0x000000` | 32 KiB  | First-stage bootloader
`ota1`    | `0x008000` | 960 KiB | Application, bank A
`ota2`    | `0x100000` | 960 KiB | Application, bank B
`kvs`     | `0x1F0000` | 32 KiB  | Key-value storage (FlashDB)
`meta`    | `0x1F8000` | 32 KiB  | OTA metadata (two 4 KiB ping-pong slots)

## OTA updates

A custom first-stage bootloader starts one of two application banks. The application is linked once per bank (`ota1` at `0x008000`, `ota2` at `0x100000`), and an update is written into the bank that is not running.

Every application image carries a descriptor at offset `0x400`: the address it was linked for, its length and its CRC-32. The build fills it in after linking, so the ELF files and all images contain it. An image **verifies** if its descriptor names the bank it is in and the length and CRC match the contents.

The bootloader keeps its state (which bank to boot, and for each bank whether it is empty, on trial or known good, together with the CRC of the image that state was recorded for) in two CRC-protected metadata slots in the `meta` partition. Each write goes to the slot that does not hold the newest record, so a power loss during a write does not lose the previous record. A state belongs to an image, not to a bank: an image the metadata does not know is never booted as known good and never offered for rollback.

- When an update has been written, `lt_ota_end()` (and so the `Update` library) verifies the new image. A corrupted or truncated download, or an image built for the other bank, is refused and the running image stays active.
- `lt_ota_switch()` then marks the new bank as a **trial** image. It is given 3 boots.
- On each boot of a trial image, the bootloader consumes one attempt and starts the image with the watchdog armed (about 4.4 minutes at the longest period), so that a hang resets the chip.
- The application commits the update by calling `lt_ota_confirm()`. The Arduino core calls it automatically after `setup()` returns. Applications using the `base` framework must call it themselves. If your application should decide when the update is healthy, build with `-DLT_OTA_AUTO_CONFIRM=0` and call `lt_ota_confirm()` yourself. Confirming disarms the bootloader's watchdog.
- If a trial image is not confirmed before its attempts run out, or it no longer verifies, the bootloader switches back to the previous bank, if that one verifies and is the image the metadata knows. If there is nothing to switch back to, it keeps booting the trial image with the watchdog armed.
- On a device without metadata (freshly flashed over SWD), the bootloader boots the first bank that verifies, bank A first, without the watchdog.
- As a last resort, when no bank qualifies, the bootloader boots the first bank with a sane vector table, unverified and without the watchdog. A device that cannot boot any bank can only be recovered over SWD.

The OTA package is a `.uf2` file containing both bank images; the running bank selects the one to write. Creating it needs an `ltchiptool` with EFM32GG11 support, which is not in a released version yet ([libretiny-eu/ltchiptool#93](https://github.com/libretiny-eu/ltchiptool/pull/93)). Without it, the build prints a warning and skips the UF2 file; the SWD images are still built.

## Flashing

{%
	include-markdown "../../inc/flashing-note.md"
%}

`pio run -t upload` programs the chip over SWD with [OpenOCD](https://openocd.org/), using a J-Link probe by default. It halts the chip, erases the `meta` partition, writes the factory image (bootloader and bank A) and resets the chip. Erasing the metadata makes the bootloader boot the new bank A image.

If you program a bank directly with another tool (for example a debugger `load` of the ELF file), erase the `meta` partition as well. Otherwise the metadata still describes the old image: the bootloader boots the other bank if it holds a known image, and the reprogrammed bank only as a last resort.

To use another debug probe, set `openocd_interface` in `platformio.ini` (it names an OpenOCD `interface/*.cfg` file):

```ini
openocd_interface = stlink
```

To use a specific OpenOCD executable, set the `OPENOCD` environment variable.

## Firmware output files

These files are present in the build directory after successful compilation:

File                            | Description
--------------------------------|----------------------------------------
**image_factory.0x000000.bin**  | **Full image** (bootloader and bank A) - flashable at 0x0
image_boot.0x000000.bin         | Bootloader - flashable at 0x0
image_ota1.0x008000.bin         | Application, bank A - flashable at 0x8000
image_ota2.0x100000.bin         | Application, bank B - flashable at 0x100000
firmware.uf2                    | UF2 package for OTA update (only with a supporting `ltchiptool`)
