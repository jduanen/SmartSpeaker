/*
* SmartSpeaker derived from Genie
*/

// -*- mode: cpp; indent-tabs-mode: nil; c-basic-offset: 2 -*-
//
// This file is part of Genie
//
// Copyright 2021 The Board of Trustees of the Leland Stanford Junior University
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

namespace smartspeaker {

class AudioInputDriver {
public:
  AudioInputDriver(){};
  virtual ~AudioInputDriver(){};
  virtual bool init(gchar *audio_input_device, int sample_rate, int channels,
                    int max_frame_length) = 0;
  virtual AudioFrame read_frame(int32_t frame_length) = 0;
};

class AudioVolumeDriver {
public:
  AudioVolumeDriver(){};
  virtual ~AudioVolumeDriver(){};
  virtual void set_volume(int volume) = 0;
  virtual int get_volume() = 0;
  virtual void duck() = 0;
  virtual void unduck() = 0;
};

} // namespace smartspeaker
