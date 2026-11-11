# Copyright (c) Andrew Rankin 2026-06-12.

import importlib.util
import os
import shutil
import subprocess
import sys
from glob import glob
from os.path import isdir, isfile, join, splitext

from ltchiptool import SocInterface
from platformio.platform.board import PlatformBoardConfig
from SCons.Errors import UserError
from SCons.Script import COMMAND_LINE_TARGETS, DefaultEnvironment, Environment

env: Environment = DefaultEnvironment()
board: PlatformBoardConfig = env.BoardConfig()
queue = env.AddLibraryQueue("silabs-efm32gg11")
env.ConfigureFamily()

GSDK = join(env.subst("$SDK_DIR"), "gecko_sdk")
if not isdir(GSDK):
    raise UserError(f"Gecko SDK not found at {GSDK}")
# Device header directory and part define, e.g. EFM32GG11B / EFM32GG11B820F2048GM64
DEVICE_SERIES = board.get("build.gsdk_device")
DEVICE_PART = board.get("build.ic").upper()
DEVICE_DIR = join(GSDK, "platform", "Device", "SiliconLabs", DEVICE_SERIES)
EMLIB_DIR = join(GSDK, "platform", "emlib")
CMSIS_DIR = join(GSDK, "platform", "CMSIS", "Core")
COMMON_DIR = join(GSDK, "platform", "common")
FREERTOS_DIR = join(GSDK, "util", "third_party", "freertos", "kernel")
LWIP_DIR = join(GSDK, "util", "third_party", "lwip")
MBEDTLS_DIR = join(GSDK, "util", "third_party", "mbedtls")
OTA_DIR = join("$FAMILY_DIR", "base", "ota")
BOOTLOADER_DIR = join("$FAMILY_DIR", "bootloader")

CPU_FLAGS = [
    "-mcpu=cortex-m4",
    "-mthumb",
    "-mfloat-abi=hard",
    "-mfpu=fpv4-sp-d16",
]
# Flash layout from the board JSON (FLASH_<PART>_OFFSET/_LENGTH), for code built
# outside the base library queue, which is where frameworks/base.py adds them.
FLASH_LAYOUT = [
    (k, v) for k, v in env["FLASH_DEFINES"].items() if k.startswith("FLASH_")
]

# Flags
queue.AppendPublic(
    CCFLAGS=[
        *CPU_FLAGS,
        "--specs=nano.specs",
    ],
    CPPDEFINES=[
        (DEVICE_PART, "1"),
        "ARM_MATH_CM4",
        # emlib places the MSC write/erase routines in flash on Series 1 parts
        # (only EFM32G and Series 2 default to RAM, see em_msc.h). Put them in
        # the .ram section instead: without this, staging an 85 KB OTA image was
        # seen to stall inside MSC_LoadWriteData on this hardware.
        ("EM_MSC_RUN_FROM_RAM", "1"),
        # Internal flash is programmed in 32-bit words; FlashDB's default 8-bit
        # status granularity rewrites bytes within a word and corrupts the KVs.
        ("FDB_WRITE_GRAN", "32"),
        # WF200 FMAC driver over SDIO, with Secure Link (the module's WF200 is
        # OTP-provisioned to reject plaintext host messages). The catalog token
        # enables SL_WFX_USE_SECURE_LINK and selects curve25519 in
        # sl_wfx_configuration_defaults.h.
        ("SL_WFX_USE_SDIO", "1"),
        ("SL_CATALOG_WFX_SECURE_LINK_PRESENT", "1"),
        # lt_sdio uses 512-byte blocks; host and card must agree.
        ("SL_WFX_SDIO_BLOCK_SIZE", "512"),
        # Round the one reachable length in 509..511 (a 510-byte queue read) up
        # to a block in the bus layer, which then relocates the piggy-backed
        # control word; otherwise sl_wfx_receive_frame reads it from offset 508.
        ("SL_WFX_SDIO_BLOCK_MODE_THRESHOLD", "509"),
    ],
    CPPPATH=[
        join(DEVICE_DIR, "Include"),
        join(EMLIB_DIR, "inc"),
        join(CMSIS_DIR, "Include"),
        join(COMMON_DIR, "inc"),
        join("$FAMILY_DIR", "base"),
        OTA_DIR,
        join(FREERTOS_DIR, "include"),
        join(FREERTOS_DIR, "portable", "GCC", "ARM_CM4F"),
        # WF200 RF front-end parameters (sl_wfx_pds.h) of BRD4321A, the WGM160P board
        join(GSDK, "platform", "radio", "wifi", "wfx_fmac_driver", "pds", "brd4321a"),
        join(MBEDTLS_DIR, "include"),
        join(MBEDTLS_DIR, "library"),
    ],
    LINKFLAGS=[
        *CPU_FLAGS,
        "--specs=nano.specs",
        "--specs=nosys.specs",
        # Keep the ELF headers out of the loadable segments. Otherwise the first
        # LOAD segment of a bank-A image starts at address 0 and contains the
        # headers, and programming the ELF overwrites the bootloader.
        "-Wl,--nmagic",
        # The family core (base/api, base/port, ...) mostly overrides weak
        # defaults from cores/common and the vendor startup. The linker does not
        # search an archive for a symbol that a weak definition already
        # satisfies, so link the whole library; --gc-sections drops what is unused.
        "-Wl,--whole-archive",
        "-lcore_efm32gg11",
        "-Wl,--no-whole-archive",
    ],
)
queue.AppendPrivate(
    CPPDEFINES=[
        # the vendor startup's `bl __START` enters LibreTiny's lt_main()
        ("__START", "lt_main"),
    ],
)

# Sources - OTA metadata and image descriptor (shared with the bootloader)
queue.AddLibrary(
    name="silabs_ota",
    base_dir=OTA_DIR,
    srcs=[
        "+<lt_crc32.c>",
        "+<lt_ota_meta.c>",
        "+<lt_ota_image.c>",
        "+<lt_ota_app.c>",
    ],
    options=dict(
        CPPDEFINES=FLASH_LAYOUT,
    ),
)

# Sources - device startup and emlib
queue.AddLibrary(
    name="gsdk_device",
    base_dir=DEVICE_DIR,
    srcs=[
        f"+<Source/system_{DEVICE_SERIES.lower()}.c>",
        f"+<Source/GCC/startup_{DEVICE_SERIES.lower()}.S>",
    ],
)
queue.AddLibrary(
    name="gsdk_emlib",
    base_dir=EMLIB_DIR,
    srcs=[
        "+<src/em_assert.c>",
        "+<src/em_cmu.c>",
        "+<src/em_core.c>",
        "+<src/em_emu.c>",
        "+<src/em_gpio.c>",
        "+<src/em_msc.c>",
        "+<src/em_rmu.c>",
        "+<src/em_system.c>",
        "+<src/em_timer.c>",
        "+<src/em_usart.c>",
        "+<src/em_wdog.c>",
    ],
)

# FreeRTOS, lwIP and mbedTLS are built from the Gecko SDK copies (FreeRTOS
# 10.4.3, lwIP 2.1.2, mbedTLS 3.5.0) rather than LibreTiny's library-freertos /
# library-lwip packages; the port was brought up and validated against these.
# Config headers come from base/config, which ConfigureFamily() puts ahead of
# the SDK paths (FreeRTOSConfig.h, lwipopts.h, arch/cc.h, mbedtls/mbedtls_config.h).

# Sources - FreeRTOS
queue.AddLibrary(
    name="gsdk_freertos",
    base_dir=FREERTOS_DIR,
    srcs=[
        "+<event_groups.c>",
        "+<list.c>",
        "+<queue.c>",
        "+<stream_buffer.c>",
        "+<tasks.c>",
        "+<timers.c>",
        "+<portable/MemMang/heap_4.c>",
        "+<portable/GCC/ARM_CM4F/port.c>",
    ],
)

# Sources - WF200 FMAC driver (source list in external-libs.json)
queue.AddExternalLibrary("wfx-fmac-driver")

# Sources - lwIP
queue.AddLibrary(
    name="gsdk_lwip",
    base_dir=LWIP_DIR,
    srcs=[
        "+<lwip/src/api/*.c>",
        "+<lwip/src/core/*.c>",
        "+<lwip/src/core/ipv4/*.c>",
        "+<lwip/src/netif/ethernet.c>",
        # apps, compiled out unless enabled in lwipopts.h
        "+<lwip/src/apps/mdns/mdns.c>",  # LWIP_MDNS_RESPONDER (Arduino mDNS)
        "+<lwip/src/apps/sntp/sntp.c>",  # SNTP_SUPPORT
        "+<lwip-contrib/ports/freertos/sys_arch.c>",
    ],
    includes=[
        "+<lwip/src/include>",
        "+<lwip-contrib/ports/freertos/include>",
    ],
)

# Sources - mbedTLS, software only, for the WF200 Secure Link key exchange and
# session crypto, and MD5 for the Arduino Update library
queue.AddLibrary(
    name="gsdk_mbedtls",
    base_dir=MBEDTLS_DIR,
    srcs=[
        # curve25519 ECDH
        "+<library/bignum.c>",
        "+<library/bignum_core.c>",
        "+<library/ecdh.c>",
        "+<library/ecp.c>",
        "+<library/ecp_curves.c>",
        # hashes and HMAC
        "+<library/md.c>",
        "+<library/md5.c>",
        "+<library/sha256.c>",
        "+<library/sha512.c>",
        # AES-CCM (needs the cipher layer)
        "+<library/aes.c>",
        "+<library/ccm.c>",
        "+<library/cipher.c>",
        "+<library/cipher_wrap.c>",
        # RNG
        "+<library/ctr_drbg.c>",
        "+<library/entropy.c>",
        "+<library/entropy_poll.c>",
        # platform
        "+<library/constant_time.c>",
        "+<library/platform.c>",
        "+<library/platform_util.c>",
    ],
)

# Sources - WF200 host port (base/wifi is not one of the directories
# AddCoreSources() collects)
queue.AddLibrary(
    name="silabs_wifi_port",
    base_dir=join("$FAMILY_DIR", "base"),
    srcs=[
        "+<wifi/*.c*>",
    ],
    includes=[
        # compat/ holds the app_webpage.h that dhcp_server.c (below) includes
        "+<wifi/compat>",
        "+<wifi>",
        # lt_sdio.h; the core library include paths are only added after this queue
        "+<port>",
    ],
)

# Sources - AP DHCP server, the Gecko SDK example's dhcp_server.c used as-is.
# Only its apps/ directory is on the include path, not the example's own
# app_webpage.h (two levels up), so `#include "app_webpage.h"` resolves to
# base/wifi/compat/app_webpage.h, which declares the AP address octets and
# ap_netif it uses; base/wifi/lt_dhcpserver.c defines them.
queue.AddLibrary(
    name="gsdk_dhcp_server",
    base_dir=join(
        GSDK,
        "app",
        "common",
        "example",
        "wifi_commissioning_micriumos",
        "lwip_host",
        "apps",
    ),
    srcs=[
        "+<dhcp_server.c>",
    ],
    includes=[
        "+<.>",
    ],
)

# Libs & linker config
queue.AppendPublic(
    LIBS=["nosys"],
)
# efm32gg11b820_ota1.ld (bank A, the main program) and _ota2.ld (bank B)
LDSCRIPT_OTA1 = board.get("build.ldscript")
LDSCRIPT_OTA2 = LDSCRIPT_OTA1.replace("_ota1", "_ota2")
LDSCRIPT_BOOT = LDSCRIPT_OTA1.replace("_ota1", "_boot")
LDSCRIPT_SECTIONS = join("$MISC_DIR", LDSCRIPT_OTA1.replace("_ota1", "_sections"))
for script in (LDSCRIPT_OTA1, LDSCRIPT_OTA2, LDSCRIPT_BOOT):
    env.GenerateLinkerScript(board, script)
# -T takes the script by name (resolved through LIBPATH), which SCons does not track
env.Depends(
    "${BUILD_DIR}/${PROGNAME}.elf",
    [join("$BUILD_DIR", LDSCRIPT_OTA1), LDSCRIPT_SECTIONS],
)

# Uploader
# SWD programming with OpenOCD (J-Link by default; the debug probe interface
# follows the `openocd_interface` project option, as for debugging). Writes the
# factory image (bootloader + bank A) and erases the OTA metadata so that the
# bootloader boots the new bank A image. The executable is $OPENOCD if set,
# else the platform's tool-openocd package if installed, else `openocd` on PATH.
IMAGE_BOOT = join("$BUILD_DIR", "image_boot.${FLASH_BOOT_OFFSET}.bin")
IMAGE_OTA1 = join("$BUILD_DIR", "image_ota1.${FLASH_OTA1_OFFSET}.bin")
IMAGE_OTA2 = join("$BUILD_DIR", "image_ota2.${FLASH_OTA2_OFFSET}.bin")
IMAGE_FACTORY = join("$BUILD_DIR", "image_factory.${FLASH_BOOT_OFFSET}.bin")


def find_openocd() -> tuple:
    """Return (executable, scripts directory or None), or (None, None)."""
    if os.environ.get("OPENOCD"):
        return os.environ["OPENOCD"], None
    package_dir = env.PioPlatform().get_package_dir("tool-openocd")
    exe = "openocd.exe" if os.name == "nt" else "openocd"
    if package_dir and isfile(join(package_dir, "bin", exe)):
        return join(package_dir, "bin", exe), join(package_dir, "scripts")
    return shutil.which("openocd"), None


if env.subst("$UPLOAD_PROTOCOL") == "openocd":
    openocd, openocd_scripts = find_openocd()
    if not openocd and "upload" in COMMAND_LINE_TARGETS:
        sys.stderr.write(
            "Error: OpenOCD not found. Install it, or set the OPENOCD environment "
            "variable to its path.\n"
        )
        env.Exit(1)
    interface = env.GetProjectOption("openocd_interface", "jlink")
    env.Replace(
        UPLOADER=openocd or "openocd",
        UPLOADCMD=" ".join(
            [
                '"$UPLOADER"',
                "$UPLOADERFLAGS",
                f'-s "{openocd_scripts}"' if openocd_scripts else "",
                f"-f interface/{interface}.cfg",
                '-c "transport select swd"',
                "-f target/efm32.cfg",
                "-c init",
                '-c "reset halt"',
                '-c "flash erase_address unlock ${FLASH_META_OFFSET} ${FLASH_META_LENGTH}"',
                # braces keep OpenOCD's Tcl parser from splitting the path
                f'-c "program {{{IMAGE_FACTORY}}} ${{FLASH_BOOT_OFFSET}} verify"',
                '-c "reset run"',
                "-c exit",
            ]
        ),
        # builder/main.py runs $UPLOADCMD for the "custom" protocol only
        UPLOAD_PROTOCOL="custom",
    )

# Bootloader
# A separate program with its own environment: no LibreTiny core, RTOS or
# user build flags.
boot_env = env.Clone()
boot_env.Replace(
    CCFLAGS=[
        *CPU_FLAGS,
        "--specs=nano.specs",
        "-Os",
        "-g2",
        "-ffunction-sections",
        "-fdata-sections",
        "-fno-common",
        "-Wall",
    ],
    CFLAGS=["-std=gnu99"],
    CPPDEFINES=[
        (DEVICE_PART, "1"),
        ("EM_MSC_RUN_FROM_RAM", "1"),  # it writes the metadata, see above
        ("__START", "main"),
        *FLASH_LAYOUT,
    ],
    CPPPATH=[
        join(DEVICE_DIR, "Include"),
        join(EMLIB_DIR, "inc"),
        join(CMSIS_DIR, "Include"),
        join(COMMON_DIR, "inc"),
        OTA_DIR,
    ],
    LINKFLAGS=[
        *CPU_FLAGS,
        "--specs=nano.specs",
        "--specs=nosys.specs",
        "-Wl,--gc-sections",
        "-Wl,--nmagic",
        '"-Wl,-Map=' + join("$BUILD_DIR", "bootloader.map") + '"',
        "-T",
        LDSCRIPT_BOOT,
    ],
    LIBPATH=["$BUILD_DIR", "$MISC_DIR"],
    LIBS=[],
)
boot_objs = (
    boot_env.CollectBuildFiles(
        join("$BUILD_DIR", "bootloader", "main"),
        BOOTLOADER_DIR,
        ["+<main.c>"],
    )
    + boot_env.CollectBuildFiles(
        join("$BUILD_DIR", "bootloader", "ota"),
        OTA_DIR,
        ["+<lt_crc32.c>", "+<lt_ota_meta.c>", "+<lt_ota_image.c>", "+<lt_ota_boot.c>"],
    )
    + boot_env.CollectBuildFiles(
        join("$BUILD_DIR", "bootloader", "device"),
        DEVICE_DIR,
        [
            f"+<Source/system_{DEVICE_SERIES.lower()}.c>",
            f"+<Source/GCC/startup_{DEVICE_SERIES.lower()}.S>",
        ],
    )
    + boot_env.CollectBuildFiles(
        join("$BUILD_DIR", "bootloader", "emlib"),
        EMLIB_DIR,
        [
            "+<src/em_cmu.c>",
            "+<src/em_core.c>",
            "+<src/em_msc.c>",
            "+<src/em_system.c>",
            "+<src/em_wdog.c>",
        ],
    )
)
boot_elf = boot_env.Program(join("$BUILD_DIR", "bootloader"), boot_objs)
boot_env.Depends(boot_elf, [join("$BUILD_DIR", LDSCRIPT_BOOT), LDSCRIPT_SECTIONS])

# Build all libraries
queue.BuildLibraries()

# Main firmware outputs and actions
# builder/main.py links bank A as the main program and then calls
# env.BuildUF2OTA(); at that point the object list and link flags are final,
# so the bank-B link and the flash images are declared there.
OBJCOPY_ACTION = env.VerboseAction(
    "$OBJCOPY -O binary $SOURCE $TARGET", "Generating ${TARGET.file}"
)
image_boot = env.Command(IMAGE_BOOT, boot_elf, OBJCOPY_ACTION)


# Application image descriptor tool (base/ota/lt_ota_image.h). Plain Python,
# shared with the host tests in test/ota, so loaded by path.
IMAGE_TOOL = join("$LT_DIR", "builder", "utils", "efm32gg11_image.py")


def load_image_tool():
    path = env.subst(IMAGE_TOOL)
    spec = importlib.util.spec_from_file_location("efm32gg11_image", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


image_tool = load_image_tool()


def fill_image_desc(link_offset: str):
    # Post-link action for a bank ELF: check the descriptor's link-time fields
    # against the ELF's binary image and fill in its CRC, in the ELF itself, so
    # that the bank image, factory image, UF2 and a debugger load all carry it.
    def action(target, source, env):
        elf_path = target[0].get_abspath()
        bin_path = splitext(elf_path)[0] + ".desc.bin"
        link_addr = int(env.subst(link_offset), 0)
        objcopy = env.subst("$OBJCOPY")
        objcopy = env.WhereIs(objcopy) or objcopy  # on the toolchain's PATH
        try:
            if subprocess.run([objcopy, "-O", "binary", elf_path, bin_path]).returncode:
                sys.stderr.write(f"Error: objcopy failed on {target[0].name}\n")
                return 1
            with open(bin_path, "rb") as f:
                image = f.read()
            with open(elf_path, "rb") as f:
                elf = f.read()
            elf = image_tool.fill_elf(elf, image, link_addr)
        except image_tool.ImageError as e:
            sys.stderr.write(f"Error: {target[0].name}: {e}\n")
            return 1
        finally:
            if isfile(bin_path):
                os.remove(bin_path)
        with open(elf_path, "wb") as f:
            f.write(elf)
        return 0

    return env.VerboseAction(
        action, "Filling in the image descriptor of ${TARGET.file}"
    )


def check_image_desc(link_offset: str):
    # Bank image action: the binary carries a correct descriptor.
    def action(target, source, env):
        with open(target[0].get_abspath(), "rb") as f:
            image = f.read()
        try:
            image_tool.check(image, int(env.subst(link_offset), 0))
        except image_tool.ImageError as e:
            sys.stderr.write(f"Error: {target[0].name}: {e}\n")
            return 1
        return 0

    return env.VerboseAction(action, "Checking the image descriptor of ${TARGET.file}")


def ota2_linkflags(env: Environment) -> list:
    flags = []
    found = False
    for flag in env["LINKFLAGS"]:
        if flag == LDSCRIPT_OTA1:  # "-T <script>" added by PlatformIO's BuildProgram()
            flag = LDSCRIPT_OTA2
            found = True
        elif isinstance(flag, str) and "-Wl,-Map=" in flag:
            flag = '"-Wl,-Map=' + join("$BUILD_DIR", "${PROGNAME}_ota2.map") + '"'
        flags.append(flag)
    if not found:
        raise UserError(f"{LDSCRIPT_OTA1} not found in LINKFLAGS; cannot link bank B")
    return flags


def merge_factory(target, source, env):
    boot_offset = int(env.subst("$FLASH_BOOT_OFFSET"), 0)
    ota1_offset = int(env.subst("$FLASH_OTA1_OFFSET"), 0)
    with open(str(source[0]), "rb") as f:
        boot = f.read()
    with open(str(source[1]), "rb") as f:
        app = f.read()
    if len(boot) > ota1_offset - boot_offset:
        sys.stderr.write("Error: bootloader overlaps the ota1 partition\n")
        return 1
    with open(str(target[0]), "wb") as f:
        f.write(boot + b"\xff" * (ota1_offset - boot_offset - len(boot)) + app)
    return 0


# Stock UF2 action (builder/utils/ltchiptool-util.py), driven by $UF2OTA
uf2_stock_action = env["BUILDERS"]["BuildUF2OTA"].action


def uf2_outputs(env) -> list:
    # what the stock action writes: <name>_<version>_...uf2 (or the fw_output
    # option), firmware.uf2 and a copy named firmware.bin
    build_dir = env.subst("$BUILD_DIR")
    outputs = glob(join(build_dir, "*.uf2")) + [join(build_dir, "firmware.bin")]
    fw_output = env.PioPlatform().custom("fw_output")
    if fw_output:
        outputs.append(join(build_dir, fw_output))
    return outputs


def build_uf2(target, source, env):
    # never leave the output of an earlier build behind
    for path in uf2_outputs(env):
        if isfile(path):
            os.remove(path)
    try:
        SocInterface.get(env["FAMILY_OBJ"])
    except NotImplementedError:
        # The EFM32GG11 SoC module is not in a released ltchiptool yet
        # (libretiny-eu/ltchiptool#93).
        sys.stderr.write(
            "Warning! The installed ltchiptool has no EFM32GG11 support, so the OTA "
            f"update file {target[0].name} was not built (SWD images are built).\n"
        )
        return 0
    # the stock action ignores errors; detect them by the missing output
    uf2_stock_action(target, source, env)
    if not isfile(target[0].get_abspath()):
        sys.stderr.write(f"Error: ltchiptool did not produce {target[0].name}\n")
        return 1
    return 0


def build_uf2ota(env: Environment, target, source):
    elf_ota1 = source
    # same objects and libraries as bank A, other linker script
    elf_ota2 = env.Program(
        join("$BUILD_DIR", "${PROGNAME}_ota2"),
        list(elf_ota1[0].sources),
        LINKFLAGS=ota2_linkflags(env),
    )
    env.Depends(elf_ota2, [join("$BUILD_DIR", LDSCRIPT_OTA2), LDSCRIPT_SECTIONS])
    env.AddPostAction(elf_ota1, fill_image_desc("$FLASH_OTA1_OFFSET"))
    env.AddPostAction(elf_ota2, fill_image_desc("$FLASH_OTA2_OFFSET"))
    # a change to the tool relinks and refills both banks
    env.Depends([elf_ota1, elf_ota2], IMAGE_TOOL)
    image_ota1 = env.Command(
        IMAGE_OTA1, elf_ota1, [OBJCOPY_ACTION, check_image_desc("$FLASH_OTA1_OFFSET")]
    )
    image_ota2 = env.Command(
        IMAGE_OTA2, elf_ota2, [OBJCOPY_ACTION, check_image_desc("$FLASH_OTA2_OFFSET")]
    )
    image_factory = env.Command(
        IMAGE_FACTORY,
        [image_boot, image_ota1],
        env.VerboseAction(merge_factory, "Generating ${TARGET.file}"),
    )
    uf2 = env.Command(
        target,
        image_ota1 + image_ota2,
        env.VerboseAction(build_uf2, "Building UF2 OTA image"),
    )
    env.Depends(uf2, image_factory)
    return uf2


env.Replace(
    UF2OTA=[
        # bank A image for DEVICE_DUAL_1, bank B for DEVICE_DUAL_2
        f"{IMAGE_OTA1},{IMAGE_OTA2}=device:ota1,ota2",
    ],
)
env.AddMethod(build_uf2ota, "BuildUF2OTA")
