"""Glue for running ESPHome's host platform on the Xiaodu (TinaLinux, GLIBC 2.23).

Compiles glibc_compat.c into the program: the cross toolchain's headers and
static libstdc++ reference symbols the device's GLIBC 2.23 doesn't export.

Also selects the armhf cross compiler for this build. ESPHome's host build
reads CC, CXX, AR, OBJDUMP and READELF from the environment when it starts
(esphome/host/toolchain.py), in the same process as this code generation, so
setting them here makes every config with `baidu_host:` cross-compile while
other configs built by the same image (or Device Builder) are unaffected.
They are always overridden: the smartspeaker-cross base image sets CC and CXX
without the sysroot flags.
"""

import os

import esphome.config_validation as cv
from esphome.core import CORE

CONFIG_SCHEMA = cv.All(cv.Schema({}), cv.only_on(["host"]))

CROSS = "arm-linux-gnueabihf-"
SYSROOT = "/sysroot"
# as the C++ app's scripts/cross_file.txt; -O2 because the host build adds no
# optimisation flag, -pthread because GLIBC 2.23 keeps pthreads in libpthread
FLAGS = (
    "-O2 -march=armv7-a -mfloat-abi=hard -mfpu=neon "
    f"--sysroot={SYSROOT} "
    f"-B{SYSROOT}/usr/lib/arm-linux-gnueabihf "
    f"-L{SYSROOT}/usr/lib/arm-linux-gnueabihf "
    f"-L{SYSROOT}/lib/arm-linux-gnueabihf "
    "-static-libgcc -static-libstdc++ -pthread -Wl,--allow-shlib-undefined"
)


async def to_code(config):
    if not CORE.is_host:
        return
    for var, tool in (
        ("CC", f"{CROSS}gcc {FLAGS}"),
        ("CXX", f"{CROSS}g++ {FLAGS}"),
        ("AR", f"{CROSS}ar"),
        ("OBJDUMP", f"{CROSS}objdump"),
        ("READELF", f"{CROSS}readelf"),
    ):
        os.environ[var] = tool
