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
EMLIB_DIR  = join(GSDK, "platform", "emlib")
CMSIS_DIR  = join(GSDK, "platform", "CMSIS", "Core")
COMMON_DIR = join(GSDK, "platform", "common")

# Compiler flags
env.Append(
    CPPDEFINES=[
        "EFM32GG11B820F2048GM64=1",
        "ARM_MATH_CM4",
        ("F_CPU", "72000000L"),
        "LT_HAS_FREERTOS=1",
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
    ],
    LINKFLAGS=[
        "-mcpu=cortex-m4",
        "-mthumb",
        "-mfloat-abi=hard",
        "-mfpu=fpv4-sp-d16",
        "--specs=nano.specs",
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

env.Append(CPPPATH=[
    join(FREERTOS_DIR, "include"),
    join(FREERTOS_DIR, "portable", "GCC", "ARM_CM4F"),
    join("$FAMILY_DIR", "base", "config"),  # for FreeRTOSConfig.h
])

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
