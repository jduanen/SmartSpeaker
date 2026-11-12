"""ALSA output and mixer for the Xiaodu codec.

The only writer to the playback PCM (48 kHz mono S16, 20 ms periods; the
codec has one DAC, so the speaker is mono). Sums the inputs registered by
baidu_media_player (media, which can be ducked, and announcements) and this
speaker's own play() stream. Volume is the codec's LINEOUT control, so the
echo-cancellation reference (tapped after it) follows the volume. Ported from
the C++ app's src/audio/mixer.cpp.
"""

from esphome import automation
import esphome.codegen as cg
from esphome.components import audio, speaker
import esphome.config_validation as cv
from esphome.const import CONF_DURATION, CONF_ID

DEPENDENCIES = ["host"]

CONF_DEVICE = "device"
CONF_VOLUME_CONTROL = "volume_control"
CONF_DECIBEL_REDUCTION = "decibel_reduction"

baidu_speaker_ns = cg.esphome_ns.namespace("baidu_speaker")
BaiduSpeaker = baidu_speaker_ns.class_("BaiduSpeaker", speaker.Speaker, cg.Component)


def _set_stream_limits(config):
    # play() takes 16-bit mono or stereo at any rate; it downmixes and resamples
    audio.set_stream_limits(
        min_bits_per_sample=16,
        max_bits_per_sample=16,
        min_channels=1,
        max_channels=2,
    )(config)
    return config


CONFIG_SCHEMA = cv.All(
    speaker.SPEAKER_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(BaiduSpeaker),
            cv.Optional(CONF_DEVICE, default="hw:audiocodec"): cv.string_strict,
            cv.Optional(CONF_VOLUME_CONTROL, default="LINEOUT volume"): cv.string_strict,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _set_stream_limits,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await speaker.register_speaker(var, config)
    cg.add(var.set_device(config[CONF_DEVICE]))
    cg.add(var.set_volume_control(config[CONF_VOLUME_CONTROL]))
    cg.add_build_flag("-lasound")
    cg.add_build_flag("-lspeexdsp")


# Same as Voice PE's mixer_speaker.apply_ducking; ducks the media input only
automation.register_apply_action(
    "baidu_speaker.apply_ducking",
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(BaiduSpeaker),
            cv.Required(CONF_DECIBEL_REDUCTION): cv.templatable(
                cv.int_range(min=0, max=255)
            ),
            cv.Optional(CONF_DURATION, default="0.0s"): cv.templatable(
                cv.positive_time_period_milliseconds
            ),
        }
    ),
    automation.ApplyCall(
        "apply_ducking({}, {})",
        ((CONF_DECIBEL_REDUCTION, cg.uint8), (CONF_DURATION, cg.uint32)),
    ),
)
