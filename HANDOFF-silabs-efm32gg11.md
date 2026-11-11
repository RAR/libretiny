# silabs-efm32gg11: where this stands (2026-11-11)

**Delete this file (drop its commit) before opening a PR.**

## State

- Upstream PR libretiny-eu/libretiny#387 was closed on 2026-11-10 as a draft that never got review. The old code is still on `feature/silabs-efm32gg11-wf200`.
- This branch, `feature/silabs-efm32gg11-v2`, is that work rebased onto upstream v1.14.0 and reworked in two rounds. Round 1 (2026-11-10): OTA/bootloader/builder, the Wi-Fi stack and the Arduino core were fixed after a full review. Round 2 (2026-11-11): non-blocking `WiFi.begin()`, WF200 restart after a firmware exception, a build-time image descriptor checked by the bootloader, the GPIO interrupt dispatcher moved into the base core, a generic WGM160P board with a real IC pin table, and three fixes to shared code. Each round was independently reviewed and the findings fixed; the fixes to the round 2 review findings have not had a further review.
- **Nothing on this branch has run on hardware.** It builds cleanly for `framework = arduino` and `framework = base` on both boards (PlatformIO 6.1.19 on macOS), and the host tests in `cores/silabs-efm32gg11/test/{ota,wifi,pwm,gpio}` pass. A `firmware.uf2` built from this branch was fed through the on-device `uf2ota` library on the host for both schemes and reproduced both bank images byte for byte. Everything else below is from reading code against the Gecko SDK and WF200 driver sources.
- ltchiptool: the EFM32GG11 SoC module is reworked on `RAR/ltchiptool` branch `feat/silabs-efm32gg11-soc-v2` (rebased on v4.15.0; it now tells the GUI and `ltchiptool flash` that the chip cannot be flashed over serial). The open PR libretiny-eu/ltchiptool#93 still has the old commit; with #387 closed it cannot be merged meaningfully, so close it or update it when this is resubmitted. Until a release has the module, the build skips `firmware.uf2` with a warning. To get the UF2 now: `PYTHONPATH=/path/to/ltchiptool-checkout pio run`.
- Changes to shared code are also on their own branches from upstream master, ready to send as small PRs (none opened): `fix/realloc-zero-size` (two commits: zero size/NULL, and the over-read when growing), `feature/fdb-write-gran`, `fix/update-md5-order` (two commits: LN882H `lt_ota_switch(revert)`, and `Update` checking the MD5 before activating), `fix/ringbuffer-count-race`. They compile for a Beken board; each has a host reproducer that fails before and passes after, but none has run on Beken/Realtek/LN882H hardware. The same commits are in this branch.

## Before resubmitting: bench test, in this order

The first three are where an untested change is most likely to stop the board working at all.

1. **Flash and boot.** `pio run -t upload` (OpenOCD + J-Link; set `OPENOCD=/usr/bin/openocd` if PlatformIO's bundled OpenOCD does not see the probe). It erases `meta` and writes `image_factory.0x000000.bin`. Check the banner, `Serial` (its RX interrupt now runs at the FreeRTOS syscall priority, not 0), `millis()`/`micros()`, and that the interrupt stack move (MSP is 8 KiB at the top of RAM) did not break anything. Without `meta` the bootloader now checks bank A's CRC on every boot until the first OTA: measure the boot delay (estimated 0.35 s).
2. **Wi-Fi bring-up and Secure Link handshake.** Changed underneath it: a bus mutex around every SDIO transaction, RX/TX buffer allocation (TX frames come from the driver's command-buffer allocator, RX buffers are rounded to 512-byte blocks), TRNG alarm handling, pins taken from the variant, and the WF200 interrupt, which is now attached in `sl_wfx_host_enable_platform_interrupt()` through the base GPIO dispatcher (check PA8 gets EXTI line 8 and that interrupts wake the bus task). Note the "WF200 FW x.y.z up in N ms" figure: bring-up still runs on the caller of `mode()`/`begin()`.
3. **Join, DHCP, traffic both ways under load.** `begin()` now returns at once; `status()` goes IDLE → CONNECTED. If every received frame is dropped, look first at the length check in `wf200_netif.c`. Log `wfx_context.encryption_bitmap[0]` once after init: it decides whether the old TX path was corrupting the heap and whether the RX free-address guard matters.
4. **Join failures.** Wrong password → `WL_CONNECT_FAILED`; missing SSID → `WL_NO_SSID_AVAIL`; an AP that gives no DHCP lease fails after 20 s. `begin()` twice within milliseconds, `begin()` right after a join completed, `disconnect(); begin();`: the result must be the second join's (watch for "Ignoring CONNECT_IND" in the debug log; whether the chip reports a cancelled join as ABORTED was not knowable from source). `begin()` then `softAP()` at once: the AP must end up on the station's channel.
5. **OTA.** First OTA after an SWD flash (trial boots, auto-confirm commits it, no watchdog reset ~4.4 min later); an image that hangs in `setup()` (watchdog resets it, three attempts, then back to the old bank); `Update.rollBack()` after a reboot; an aborted download followed by `OTA.switchImage()` (must return false); a corrupted or truncated UF2 and a bank image packed for the wrong bank (refused with "Update rejected: …", `Update.end()` false); power cut after the last block but before the switch (the device keeps its bank, `rollBack()` is refused); an `Update` with a wrong `setMD5()` (not activated); the two CRC passes at the end of an update with the flash lock held (0.1-0.4 s: effect on Wi-Fi and USART); real watchdog period on the uncalibrated ULFRCO; reset by a short `lt_wdt_enable()` period (the bootloader disarms the watchdog first thing).
6. **WF200 restart.** Simulate a firmware exception from the application: `lt_wfx_host.fault_reason = 1; lt_wfx_host.chip_fault = true;` (`sl_wfx_host_conf.h`). Check that `lt_sdio_init()` and `sl_wfx_init()` succeed a second time in the same boot, the "up again in N ms" figure, that the station rejoins and the softAP comes back (in AP+STA on the station's channel), TCP traffic under load during the fault, `softAP()`/`softAPdisconnect()` from the application during the restore, and that the 6 KiB event task stack is enough. If it misbehaves, build with `-DLT_WIFI_CHIP_RECOVERY=0`.
7. **Station state.** Static IP (connect, AP power loss, automatic rejoin); `begin()` while connected; open network; `begin()` with channel/BSSID.
8. **softAP.** Start (waits for the chip's start indication), hidden SSID, `maxClients` (default 4 is enforced), restart with new settings, more than 8 clients over time (leases are recycled), AP + STA together.
9. **Core.** Reboot reason after power-on, software reset, watchdog, brown-out; PWM 0 %/100 %/frequency change; `digitalWrite(PF11, HIGH); pinMode(PF11, OUTPUT);` gives no low pulse; two pin interrupts in the same group of four on different ports, `detachInterrupt()` from inside a handler; `Serial.begin(9600, SERIAL_8E1)` followed by logging; a deliberate fault prints a dump and resets; is the chip's EUI-48 programmed (the device name changes if it is). A `framework = base` image boots.
10. **Generic board.** `generic-wgm160p`: console on PE6/PE7, `analogWrite` on D10/D11/D17 (PB5/PB6/PD8).
11. **Never exercised at all, old or new:** Secure Link key renegotiation (long uptime or heavy traffic), a real WF200 firmware exception, mDNS, FlashDB writes concurrent with OTA staging.

## Decisions to raise with the maintainer first

- Does upstream want an EFM32GG11/WF200 family at all?
- Package hosting: `platform.json` points at `RAR/framework-silabs-gecko-sdk` and `RAR/framework-wfx-fmac-driver`; every other package is under `libretiny-eu`. The Gecko SDK repo also references git-lfs objects; a machine with the lfs filter configured but no `git-lfs` binary cannot install it.
- FreeRTOS, lwIP and mbedTLS are built from the Gecko SDK tree instead of LibreTiny's shared packages.
- ltchiptool's SoC module only packs the UF2; linking and ELF conversion are done by the family builder in SCons (two bank programs, the bootloader, the descriptor fill), where other families use `ltchiptool link2bin`.
- `families.json`: the family is a single entry with no parent; upstream uses parent/child pairs.
- Uploads work by switching `UPLOAD_PROTOCOL` to `custom` inside the family builder, because `builder/main.py` only runs `$UPLOADCMD` for that protocol. A small change to `main.py` would be cleaner.
- `lt_ota_confirm()` and `lt_ota_staged()` are new weak hooks in common `lt_ota.{c,h}`; a family without TLS is new (the `LT_HAS_MBEDTLS` guards in `HTTPClient.cpp`).
- Board variants are boardgen output plus a hand-kept tail (`PIN_WF200_*`; on the JuiceBox also `LED_*`/`BTN0`) marked "Not boardgen output". boardgen cannot emit that tail, so CI regeneration would drop it: the WF200 pins could move into the core instead. The IC pin table lists only what the core implements (GPIO, IRQ, three PWM routes, USART0); upstream tables usually list all hardware functions.
- boardgen 0.12.0 prints empty pin flags on Python 3.11+ (it relies on the old `str(IntFlag)`), for every board including upstream's. The variants here were generated with a shim that restores the old formatting; with it, upstream's `generic-ln882h` regenerates byte-identically.
- ltchiptool's diff32 writer raises OverflowError when a word differs by 2^31 or more between the two bank images. The image CRC is stored as two 16-bit halves to stay inside that; fixing the writer (difference mod 2^32, which the on-device applier already handles) would be the better cure.
- The `realloc` fix reads the FreeRTOS heap_4/heap_5 block header from common code; a size helper exported by `library-freertos-port` would be cleaner. LN882H could instead use its native `pvPortRealloc` (its `LT_REALLOC_FUNC` is misspelled `pvPortReAlloc`).
- Seen in passing, not touched: `builder/family/lightning-ln882h.py` around line 104, `"+<mcu/ln882h/system_ln882h.c>"` lacks its trailing comma and concatenates with the next filter.

## Known gaps left on purpose

- No `analogRead`, Wire, SPI, deep sleep, TLS. WPA3-only networks cannot be joined.
- The first radio bring-up (firmware download, Secure Link key exchange; estimated under 2 s, not measured) runs on the caller of `mode()`/`begin()`.
- After the WF200 restarts are used up (3 per 10 minutes), the radio stays off until reboot. A chip that stops answering without sending an exception indication is not detected.
- Flash page erases stall the CPU with interrupts enabled, so `millis()` falls behind and USART RX can overrun during OTA or FlashDB writes.
- If the running image itself does not verify (flash bit error, or loaded by a debugger without a filled descriptor) and an update is then installed, there is nothing to revert to: an unconfirmed trial keeps booting with the watchdog armed.
- The meta record format changed (magic "OTA2", with the CRC of the image each state belongs to). A device with a bootloader from before 2026-11-11 cannot move to this firmware by OTA; flash the factory image over SWD.
- Flashing a bank directly with another tool while `meta` is intact: the other bank boots if it holds an image the metadata knows, the reprogrammed one only as a last resort. Erase `meta` too.
- `framework = base`: nothing reads the WF200 host event queue (the Arduino Wi-Fi library's event task does), so a base application must drain it and run its own station handling; it must also call `lt_ota_confirm()` itself.
- A fault resets through a software reset, so a crash reports as a software reset, not a crash.
- Upstream, not fixed: SoftwareSerial `write()` checks `tx.state == SS_IDLE` without masking the timer interrupt, so a byte can sit unsent until the next `write()`; the BDK's own `os_realloc` over-reads like the fallback did.

## The application (openevse-native-juicebox-firmware) needs these changes to build against this branch

- `scripts/lite_flash.sh` flashes `bootloader.bin`/`firmware.bin`, which no longer exist as raw images. Use `upload_protocol = openocd`, or flash `image_factory.0x000000.bin` at 0x0 and erase `meta`.
- `scripts/gen_ota_uf2.py` packs `firmware_a.bin`/`firmware_b.bin`. Use the build's `firmware.uf2`, or pack `image_ota1.0x008000.bin` and `image_ota2.0x100000.bin`. Bank images without a filled descriptor are rejected on the device.
- Remove `-D interrupts=__enable_irq -D noInterrupts=__disable_irq` (the core defines them now), `-D configTOTAL_HEAP_SIZE=262144` (now the default) and `-D LT_WIFI_STATUS_LED_DEFAULT=0` (now the default). Keep `-D LT_MAIN_TASK_STACK_SIZE=16384` (default is 8 KiB).
- `lite_sntp_set_system_time` is no longer called by lwIP; SNTP sets the clock through `settimeofday`. Read time with `time()`.
- `WiFi.begin()` returns immediately. The application already polls `WiFi.status()` and ignores the return value, so nothing to change; a join now ends on its own after 20 s. While the WF200 restarts, `status()` is `WL_NO_SHIELD`.
- Wi-Fi event callbacks run on the Wi-Fi event task, not the lwIP thread.
- `softAP()` enforces `maxClients` (default 4).
- Its own `lt_ota_confirm()` call is redundant now (the core confirms after `setup()`); to keep deciding itself, build with `-DLT_OTA_AUTO_CONFIRM=0`.
- Pin semantics follow upstream: `digitalWrite` on a pin that is not an output sets a pull; `digitalRead` on an output switches it to input (careful with PF11).

## Building here

```ini
[env:wgm160p]
platform = symlink:///path/to/this/checkout
board = wgm160p-juicebox-40   ; or generic-wgm160p
framework = arduino
```

Host tests: `make -C cores/silabs-efm32gg11/test/ota` (also `wifi`, `pwm`, `gpio`). CI lint is clang-format 19, black, isort.
