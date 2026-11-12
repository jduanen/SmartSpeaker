"""ALSA microphone for the Xiaodu codec, with Speex echo cancellation.

Captures 3 channels at 16 kHz from the sun8iw18 codec (MIC1, MIC2 and MIC3,
which the board wires to the speaker signal as the echo reference) and outputs
16-bit mono: the two mics averaged and DC-blocked, then Speex AEC against the
reference and Speex noise suppression. Ported from the C++ app's
src/audio/alsa/input.cpp.
"""

import esphome.codegen as cg
from esphome.components import audio, microphone
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["host"]

CONF_DEVICE = "device"
CONF_ECHO_CANCELLATION = "echo_cancellation"

baidu_mic_ns = cg.esphome_ns.namespace("baidu_mic")
BaiduMicrophone = baidu_mic_ns.class_(
    "BaiduMicrophone", microphone.Microphone, cg.Component
)


def _set_stream_limits(config):
    audio.set_stream_limits(
        min_bits_per_sample=16,
        max_bits_per_sample=16,
        min_channels=1,
        max_channels=1,
        min_sample_rate=16000,
        max_sample_rate=16000,
    )(config)
    return config


CONFIG_SCHEMA = cv.All(
    microphone.MICROPHONE_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(BaiduMicrophone),
            cv.Optional(CONF_DEVICE, default="hw:audiocodec"): cv.string_strict,
            # needs the codec's 3rd capture channel (speaker reference)
            cv.Optional(CONF_ECHO_CANCELLATION, default=True): cv.boolean,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _set_stream_limits,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await microphone.register_microphone(var, config)
    cg.add(var.set_device(config[CONF_DEVICE]))
    cg.add(var.set_echo_cancellation(config[CONF_ECHO_CANCELLATION]))
    cg.add_build_flag("-lasound")
    cg.add_build_flag("-lspeexdsp")
