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
        "-T", env.subst("$LDSCRIPT_PATH"),
    ],
    CPPPATH=[
        join(DEVICE_DIR, "Include"),
        join(EMLIB_DIR, "inc"),
        join(CMSIS_DIR, "Include"),
        join(COMMON_DIR, "inc"),
        join("$PROJECT_CORE_DIR", "base"),
    ],
)

# Linker script path
env.Replace(LDSCRIPT_PATH=join("$PROJECT_CORE_DIR", "misc", "efm32gg11b820.ld"))

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
    ],
)

# Our family's base + arduino sources (currently empty, but the directories
# must be wired so subsequent tasks just drop files in).
queue.AddLibrary(
    name="lt-family-base",
    base_dir=join("$PROJECT_CORE_DIR", "base"),
    srcs=["+<api/*.c>", "+<port/*.c>", "+<fixups/*.c>"],
)

if env.subst("$LT_HAS_ARDUINO") == "1":
    queue.AddLibrary(
        name="lt-family-arduino",
        base_dir=join("$PROJECT_CORE_DIR", "arduino"),
        srcs=["+<src/*.c>", "+<src/*.cpp>"],
    )

queue.BuildLibraries()
