"""Media player for the Xiaodu: a media and an announcement source mixed by
baidu_speaker.

Each source fetches (libcurl, or a local file) and decodes (FLAC, MP3, WAV)
one item at a time on its own thread, downmixed and resampled to the mixer's
48 kHz mono. Announcements play over the media; ducking the media is up to
the YAML (baidu_speaker.apply_ducking), as with Voice PE. Both purposes ask HA
for FLAC 48 kHz mono, the mixer's own format. Ported from the C++ app's
src/audio/{source,stream,decoder}.cpp and its media_player entity.
"""

import esphome.codegen as cg
from esphome.components import media_player
import esphome.config_validation as cv
from esphome.const import CONF_SPEAKER

CONF_VOLUME_INCREMENT = "volume_increment"
CONF_VOLUME_INITIAL = "volume_initial"

from ..baidu_speaker.speaker import BaiduSpeaker

DEPENDENCIES = ["host"]
AUTO_LOAD = ["baidu_speaker"]

baidu_media_player_ns = cg.esphome_ns.namespace("baidu_media_player")
BaiduMediaPlayer = baidu_media_player_ns.class_(
    "BaiduMediaPlayer", media_player.MediaPlayer, cg.Component
)

CONFIG_SCHEMA = media_player.media_player_schema(BaiduMediaPlayer).extend(
    {
        cv.Required(CONF_SPEAKER): cv.use_id(BaiduSpeaker),
        cv.Optional(CONF_VOLUME_INCREMENT, default=0.05): cv.percentage,
        cv.Optional(CONF_VOLUME_INITIAL, default=0.5): cv.percentage,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await media_player.new_media_player(config)
    await cg.register_component(var, config)
    spkr = await cg.get_variable(config[CONF_SPEAKER])
    cg.add(var.set_speaker(spkr))
    cg.add(var.set_volume_increment(config[CONF_VOLUME_INCREMENT]))
    cg.add(var.set_volume_initial(config[CONF_VOLUME_INITIAL]))
    cg.add_build_flag("-lcurl")
