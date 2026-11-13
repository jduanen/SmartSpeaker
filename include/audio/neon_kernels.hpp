/*
 * SmartSpeaker: NEON int8 CONV_2D and DEPTHWISE_CONV_2D for the wake word models
 *
 * Copied from baidu/components/micro_wake_word/neon_kernels.cpp. Registered by
 * wakeword.cpp in place of TFLite Micro's reference kernels.
 */

#pragma once

#ifdef __ARM_NEON

#include <tensorflow/lite/micro/micro_common.h>

namespace smartspeaker {

TFLMRegistration register_conv_2d_neon();
TFLMRegistration register_depthwise_conv_2d_neon();

} // namespace smartspeaker

#endif
