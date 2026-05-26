# SPDX-License-Identifier: MSLA
# Copyright (c) Kuba Szczodrzyński 2022-3-13 (pattern adapted)
# Phase 1 builder for silabs-efm32gg11 — minimum surface to link.

from os.path import isdir, join

from SCons.Script import DefaultEnvironment, Environment

env: Environment = DefaultEnvironment()

# Family-level setup
queue = env.AddLibraryQueue("silabs-efm32gg11")
env.ConfigureFamily()

SDK_DIR = env.subst("$SDK_DIR")
if not isdir(SDK_DIR):
    raise RuntimeError(f"Gecko SDK not present at {SDK_DIR}")

GSDK = join(SDK_DIR, "gecko_sdk")
DEVICE_DIR = join(GSDK, "platform", "Device", "SiliconLabs", "EFM32GG11B")
EMLIB_DIR = join(GSDK, "platform", "emlib")
CMSIS_DIR = join(GSDK, "platform", "CMSIS", "Core")
COMMON_DIR = join(GSDK, "platform", "common")

# Compiler flags
env.Append(
    CPPDEFINES=[
        "EFM32GG11B820F2048GM64=1",
        "ARM_MATH_CM4",
        ("F_CPU", "72000000L"),
        "LT_HAS_FREERTOS=1",
        # Redirect the GSDK startup's `bl __START` to LibreTiny's lt_main(),
        # which calls lt_init_family() + __libc_init_array() + main(). The
        # default is _start (newlib), which would skip lt_init_family — leaving
        # the chip on the uncalibrated HFRCO (~19 MHz) instead of 72 MHz HFXO+DPLL,
        # and Phase 1 framework=base sketches would observe wrong timing.
        # Mirrors the Reset_Handler -> lt_main routing used by beken-72xx and
        # lightning-ln882h (which patch Reset_Handler directly; the GSDK startup
        # exposes a __START hook so we redirect via preprocessor instead).
        ("__START", "lt_main"),
    ],
    CCFLAGS=[
        "-mcpu=cortex-m4",
        "-mthumb",
        "-mfloat-abi=hard",
        "-mfpu=fpv4-sp-d16",
        "-Os",
        "-ffunction-sections",
        "-fdata-sections",
        "--specs=nano.specs",
        "--specs=nosys.specs",
    ],
    LINKFLAGS=[
        "-mcpu=cortex-m4",
        "-mthumb",
        "-mfloat-abi=hard",
        "-mfpu=fpv4-sp-d16",
        "--specs=nano.specs",
        "--specs=nosys.specs",
        "-Wl,--gc-sections",
        "-Wl,-Map=" + join("$BUILD_DIR", "${PROGNAME}.map"),
        # NOTE: -T <ldscript> is added automatically by PIO's piobuild.py from
        # $LDSCRIPT_PATH (set by env_configure() from board.build.ldscript). The
        # filename is resolved via LIBPATH, which frameworks/base.py prepends
        # with $CORES_DIR/<family>/misc — so we do NOT pass -T here ourselves.
    ],
    CPPPATH=[
        join(DEVICE_DIR, "Include"),
        join(EMLIB_DIR, "inc"),
        join(CMSIS_DIR, "Include"),
        join(COMMON_DIR, "inc"),
        join("$FAMILY_DIR", "base"),
    ],
)

# Do NOT override LDSCRIPT_PATH here: env_configure() sets it from
# board.build.ldscript ("efm32gg11b820.ld") and frameworks/base.py prepends
# $CORES_DIR/<family>/misc to LIBPATH so the linker can find the script.

# Add SDK sources: minimum (vendor startup + system) — enough to link a C main().
queue.AddLibrary(
    name="silabs-device",
    base_dir=DEVICE_DIR,
    srcs=[
        "+<Source/system_efm32gg11b.c>",
        "+<Source/GCC/startup_efm32gg11b.S>",
    ],
)

# emlib core — keep minimal; will grow as features land in later tasks.
queue.AddLibrary(
    name="silabs-emlib-core",
    base_dir=EMLIB_DIR,
    srcs=[
        "+<src/em_system.c>",
        "+<src/em_core.c>",
        "+<src/em_assert.c>",
        "+<src/em_cmu.c>",
        "+<src/em_gpio.c>",
        "+<src/em_usart.c>",
        "+<src/em_emu.c>",
        "+<src/em_wdog.c>",
        "+<src/em_timer.c>",
        "+<src/em_msc.c>",
    ],
)

# FreeRTOS kernel — wired from the GSDK mirror.
# Confirmed for GSDK v4.5.0: kernel sources at this path (tasks.c, queue.c, etc.
# at the root; portable/ subtree per port). Older GSDK lines used a Source/
# subdir — re-verify if bumping past v4.5.x.
FREERTOS_DIR = join(GSDK, "util", "third_party", "freertos", "kernel")

queue.AddLibrary(
    name="freertos-kernel",
    base_dir=FREERTOS_DIR,
    srcs=[
        "+<*.c>",
        "+<portable/MemMang/heap_4.c>",
        "+<portable/GCC/ARM_CM4F/port.c>",
    ],
)

env.Append(
    CPPPATH=[
        join(FREERTOS_DIR, "include"),
        join(FREERTOS_DIR, "portable", "GCC", "ARM_CM4F"),
        join("$FAMILY_DIR", "base", "config"),  # for FreeRTOSConfig.h
    ]
)

# Note: cores/silabs-efm32gg11/base/ sources (api/*.c, port/*.c, fixups/*.c)
# are added automatically by frameworks/base.py via env.AddCoreSources() over
# family.inheritance. We do NOT re-add them here — doing so would compile each
# file twice and produce duplicate-symbol link errors.
#
# Same for cores/silabs-efm32gg11/arduino/{src,libraries}/ when framework=arduino:
# frameworks/arduino.py handles those via the equivalent mechanism. Family
# builders only add SDK sources and any special fixup libraries here.

# Linker: add nosys to LIBS so libg_nano.a's abort()/exit() paths can resolve
# their references to _exit/_kill/_getpid/etc. against newlib's nosys stubs.
# Our cores/silabs-efm32gg11/base/fixups/syscalls.c provides strong overrides
# (notably _exit -> NVIC_SystemReset) — strong symbols defeat the weak nosys
# defaults, but nosys backstops the rest of the newlib syscall surface (sbrk,
# read, write, fstat, ...). Same pattern used by beken-72xx, lightning-ln882h,
# realtek-ambz, realtek-ambz2.
queue.AppendPublic(
    LIBS=["nosys"],
)

queue.BuildLibraries()

# ----------------------------------------------------------------------------
# Phase 1 post-link wiring
#
# main.py's BuildProgram() produces ${BUILD_DIR}/raw_firmware.elf. We need:
#   - firmware.bin: raw flash image (Commander/J-Link flashes this directly)
#   - firmware.elf: same as raw_firmware.elf, renamed so the size dispatcher's
#       `arm-none-eabi-size .../firmware.elf` finds it
#   - firmware.uf2: stub copy of firmware.bin so main.py's downstream targets
#       (which Depends() on this path) resolve. Phase 1 has no proper UF2
#       packaging — ltchiptool's UF2 writer requires a per-family SocInterface
#       module (a hardcoded if-chain in ltchiptool/soc/interface.py: bk72xx /
#       ambz / ambz2 / ln882h) which doesn't yet exist for EFM32. Adding one
#       is upstream-ltchiptool work, not in Phase 1 scope. Phase 2 will write
#       a proper SocInterface and PR it to libretiny-eu/ltchiptool.
# ----------------------------------------------------------------------------
firmware_elf = "${BUILD_DIR}/firmware.elf"
firmware_bin = "${BUILD_DIR}/firmware.bin"

env.AddPostAction(
    "${BUILD_DIR}/${PROGNAME}.elf",
    [
        env.VerboseAction(
            " ".join(
                [
                    "$OBJCOPY",
                    "-O",
                    "binary",
                    "${BUILD_DIR}/${PROGNAME}.elf",
                    firmware_bin,
                ]
            ),
            "Producing firmware.bin",
        ),
        env.VerboseAction(
            "cp ${BUILD_DIR}/${PROGNAME}.elf " + firmware_elf,
            "Producing firmware.elf",
        ),
    ],
)


# Override main.py's BuildUF2OTA for Phase 1: skip the ltchiptool UF2 packer
# (no SocInterface yet — see comment block above) and just copy firmware.bin to
# firmware.uf2 so downstream Depends() resolves.
def _phase1_uf2_stub(env, *args, **kwargs):
    import shutil

    print("|-- firmware.uf2 (Phase 1 stub: copy of firmware.bin; proper UF2")
    print("|   packaging deferred until ltchiptool SoC plugin lands)")
    shutil.copy(env.subst(firmware_bin), env.subst("${BUILD_DIR}/firmware.uf2"))


from SCons.Script import Builder  # noqa: E402

env.Append(
    BUILDERS=dict(
        BuildUF2OTA=Builder(
            action=[env.VerboseAction(_phase1_uf2_stub, "Phase 1: UF2 stub")]
        )
    )
)

env.Replace(
    # Placeholder; main.py still references env["UF2OTA"] for upload logic.
    UF2OTA=[
        f"{firmware_bin}=flasher:app",
    ],
)
