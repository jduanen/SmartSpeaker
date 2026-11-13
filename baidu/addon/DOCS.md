# ESPHome Device Builder (baidu)

The official ESPHome Device Builder add-on, plus the armhf cross compiler and
libraries that the Xiaodu (baidu) speakers' ESPHome configs need. Configs that
use `baidu_host:` are cross-compiled for the speaker; every other config
(ESP32 and so on) builds exactly as in the official add-on.

Run this add-on instead of the official one (stop or uninstall that first):
Home Assistant keeps one Device Builder for its ESPHome firmware updates.
Both use the same config folder, `/config/esphome`.

See `baidu/README.md` in the SmartSpeaker repository for setting up a speaker.
