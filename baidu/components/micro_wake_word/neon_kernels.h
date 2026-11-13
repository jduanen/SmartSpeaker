#pragma once
// NEON int8 CONV_2D and DEPTHWISE_CONV_2D for the host (armhf) build. See neon_kernels.cpp.
#if defined(USE_HOST) && defined(__ARM_NEON)

#include "tensorflow/lite/micro/micro_common.h"

namespace esphome {
namespace micro_wake_word {

TFLMRegistration register_conv_2d_neon();
TFLMRegistration register_depthwise_conv_2d_neon();

}  // namespace micro_wake_word
}  // namespace esphome

#endif
