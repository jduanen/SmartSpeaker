"""Glue for running ESPHome's host platform on the Xiaodu (TinaLinux, GLIBC 2.23).

Compiles glibc_compat.c into the program: the cross toolchain's headers and
static libstdc++ reference symbols the device's GLIBC 2.23 doesn't export.
"""

import esphome.config_validation as cv

CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    pass
