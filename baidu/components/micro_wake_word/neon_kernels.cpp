// NEON int8 CONV_2D and DEPTHWISE_CONV_2D for the host (armhf) build.
//
// TFLite Micro's portable reference kernels recompute a 4-D index for every
// multiply-add (about 75 cycles each on the Cortex-A7); with them, CONV_2D and
// DEPTHWISE_CONV_2D took 90% of an inference. These keep TFLM's own Init and
// Prepare (quantisation parameters, padding) and replace only Eval, with the
// reference arithmetic: acc = bias + sum(filter * (input + input_offset)), then
// MultiplyByQuantizedMultiplier, + output_offset, clamp. The output is
// bit-identical to the reference. Cases they don't cover (non-int8, grouped
// conv, depth multiplier != 1) go to TFLM's own Eval.
#if defined(USE_HOST) && defined(__ARM_NEON)

#include "neon_kernels.h"

#include <arm_neon.h>
#include <algorithm>
#include <vector>

#include "tensorflow/lite/kernels/internal/common.h"
#include "tensorflow/lite/kernels/kernel_util.h"
#include "tensorflow/lite/micro/kernels/conv.h"
#include "tensorflow/lite/micro/kernels/depthwise_conv.h"
#include "tensorflow/lite/micro/kernels/kernel_util.h"

namespace esphome {
namespace micro_wake_word {

namespace {

TfLiteStatus (*conv_reference_eval)(TfLiteContext *, TfLiteNode *) = nullptr;
TfLiteStatus (*depthwise_reference_eval)(TfLiteContext *, TfLiteNode *) = nullptr;

inline int32_t horizontal_sum(int32x4_t v) {
  int32x2_t s = vadd_s32(vget_low_s32(v), vget_high_s32(v));
  return vget_lane_s32(vpadd_s32(s, s), 0);
}

inline int8_t requantize(int32_t acc, int32_t multiplier, int32_t shift, int32_t output_offset, int32_t act_min,
                         int32_t act_max) {
  acc = tflite::MultiplyByQuantizedMultiplier(acc, multiplier, shift) + output_offset;
  return static_cast<int8_t>(std::min(std::max(acc, act_min), act_max));
}

TfLiteStatus conv_eval(TfLiteContext *context, TfLiteNode *node) {
  const TfLiteEvalTensor *input = tflite::micro::GetEvalInput(context, node, tflite::kConvInputTensor);
  const TfLiteEvalTensor *filter = tflite::micro::GetEvalInput(context, node, tflite::kConvWeightsTensor);
  const TfLiteEvalTensor *bias =
      (tflite::NumInputs(node) == 3) ? tflite::micro::GetEvalInput(context, node, tflite::kConvBiasTensor) : nullptr;
  TfLiteEvalTensor *output = tflite::micro::GetEvalOutput(context, node, tflite::kConvOutputTensor);
  const tflite::RuntimeShape in_shape = tflite::micro::GetTensorShape(input);
  const tflite::RuntimeShape f_shape = tflite::micro::GetTensorShape(filter);
  const tflite::RuntimeShape out_shape = tflite::micro::GetTensorShape(output);
  if (input->type != kTfLiteInt8 || filter->type != kTfLiteInt8 || (bias != nullptr && bias->type != kTfLiteInt32) ||
      f_shape.Dims(3) != in_shape.Dims(3))  // grouped conv
    return conv_reference_eval(context, node);

  const auto &params = *static_cast<const TfLiteConvParams *>(node->builtin_data);
  const auto &data = *static_cast<const tflite::OpDataConv *>(node->user_data);
  const tflite::ConvParams p = tflite::ConvParamsQuantized(params, data);
  const int8_t *in = tflite::micro::GetTensorData<int8_t>(input);
  const int8_t *w = tflite::micro::GetTensorData<int8_t>(filter);
  const int32_t *b = bias != nullptr ? tflite::micro::GetTensorData<int32_t>(bias) : nullptr;
  int8_t *out = tflite::micro::GetTensorData<int8_t>(output);

  const int batches = in_shape.Dims(0), in_h = in_shape.Dims(1), in_w = in_shape.Dims(2), depth = in_shape.Dims(3);
  const int f_h = f_shape.Dims(1), f_w = f_shape.Dims(2);
  const int out_h = out_shape.Dims(1), out_w = out_shape.Dims(2), out_depth = out_shape.Dims(3);
  const int16x8_t offset = vdupq_n_s16(static_cast<int16_t>(p.input_offset));

  // per output pixel: the inside taps' inputs, widened to int16 with the offset added,
  // and each tap's position in the filter
  static thread_local std::vector<int16_t> taps_in;
  static thread_local std::vector<int> taps_k;
  taps_in.resize(static_cast<size_t>(f_h) * f_w * depth);
  taps_k.resize(static_cast<size_t>(f_h) * f_w);

  for (int batch = 0; batch < batches; batch++) {
    for (int oy = 0; oy < out_h; oy++) {
      for (int ox = 0; ox < out_w; ox++) {
        int taps = 0;
        for (int fy = 0; fy < f_h; fy++) {
          const int iy = oy * p.stride_height - p.padding_values.height + p.dilation_height_factor * fy;
          if (iy < 0 || iy >= in_h)
            continue;
          for (int fx = 0; fx < f_w; fx++) {
            const int ix = ox * p.stride_width - p.padding_values.width + p.dilation_width_factor * fx;
            if (ix < 0 || ix >= in_w)
              continue;
            const int8_t *src = in + ((static_cast<size_t>(batch) * in_h + iy) * in_w + ix) * depth;
            int16_t *dst = taps_in.data() + static_cast<size_t>(taps) * depth;
            int c = 0;
            for (; c + 8 <= depth; c += 8)
              vst1q_s16(dst + c, vaddq_s16(vmovl_s8(vld1_s8(src + c)), offset));
            for (; c < depth; c++)
              dst[c] = static_cast<int16_t>(src[c] + p.input_offset);
            taps_k[taps++] = fy * f_w + fx;
          }
        }
        int8_t *dst = out + ((static_cast<size_t>(batch) * out_h + oy) * out_w + ox) * out_depth;
        for (int oc = 0; oc < out_depth; oc++) {
          const int8_t *wf = w + static_cast<size_t>(oc) * f_h * f_w * depth;
          int32x4_t acc4 = vdupq_n_s32(0);
          int32_t acc = 0;
          for (int t = 0; t < taps; t++) {
            const int16_t *x = taps_in.data() + static_cast<size_t>(t) * depth;
            const int8_t *wt = wf + static_cast<size_t>(taps_k[t]) * depth;
            int c = 0;
            for (; c + 8 <= depth; c += 8) {
              const int16x8_t wv = vmovl_s8(vld1_s8(wt + c));
              const int16x8_t xv = vld1q_s16(x + c);
              acc4 = vmlal_s16(acc4, vget_low_s16(wv), vget_low_s16(xv));
              acc4 = vmlal_s16(acc4, vget_high_s16(wv), vget_high_s16(xv));
            }
            for (; c < depth; c++)
              acc += wt[c] * x[c];
          }
          acc += horizontal_sum(acc4);
          if (b != nullptr)
            acc += b[oc];
          dst[oc] = requantize(acc, data.per_channel_output_multiplier[oc], data.per_channel_output_shift[oc],
                               p.output_offset, p.quantized_activation_min, p.quantized_activation_max);
        }
      }
    }
  }
  return kTfLiteOk;
}

TfLiteStatus depthwise_eval(TfLiteContext *context, TfLiteNode *node) {
  const TfLiteEvalTensor *input = tflite::micro::GetEvalInput(context, node, tflite::kDepthwiseConvInputTensor);
  const TfLiteEvalTensor *filter = tflite::micro::GetEvalInput(context, node, tflite::kDepthwiseConvWeightsTensor);
  const TfLiteEvalTensor *bias =
      (tflite::NumInputs(node) == 3) ? tflite::micro::GetEvalInput(context, node, tflite::kDepthwiseConvBiasTensor)
                                     : nullptr;
  TfLiteEvalTensor *output = tflite::micro::GetEvalOutput(context, node, tflite::kDepthwiseConvOutputTensor);
  const auto &params = *static_cast<const TfLiteDepthwiseConvParams *>(node->builtin_data);
  const tflite::RuntimeShape in_shape = tflite::micro::GetTensorShape(input);
  const tflite::RuntimeShape f_shape = tflite::micro::GetTensorShape(filter);
  const tflite::RuntimeShape out_shape = tflite::micro::GetTensorShape(output);
  if (input->type != kTfLiteInt8 || filter->type != kTfLiteInt8 || (bias != nullptr && bias->type != kTfLiteInt32) ||
      params.depth_multiplier != 1 || f_shape.Dims(3) != in_shape.Dims(3))
    return depthwise_reference_eval(context, node);

  const auto &data = *static_cast<const tflite::OpDataConv *>(node->user_data);
  const tflite::DepthwiseParams p = tflite::DepthwiseConvParamsQuantized(params, data);
  const int8_t *in = tflite::micro::GetTensorData<int8_t>(input);
  const int8_t *w = tflite::micro::GetTensorData<int8_t>(filter);
  const int32_t *b = bias != nullptr ? tflite::micro::GetTensorData<int32_t>(bias) : nullptr;
  int8_t *out = tflite::micro::GetTensorData<int8_t>(output);

  const int batches = in_shape.Dims(0), in_h = in_shape.Dims(1), in_w = in_shape.Dims(2), depth = in_shape.Dims(3);
  const int f_h = f_shape.Dims(1), f_w = f_shape.Dims(2);
  const int out_h = out_shape.Dims(1), out_w = out_shape.Dims(2);
  const int16x8_t offset = vdupq_n_s16(static_cast<int16_t>(p.input_offset));

  static thread_local std::vector<int32_t> acc;
  acc.resize(static_cast<size_t>(depth));

  for (int batch = 0; batch < batches; batch++) {
    for (int oy = 0; oy < out_h; oy++) {
      for (int ox = 0; ox < out_w; ox++) {
        std::fill(acc.begin(), acc.end(), 0);
        for (int fy = 0; fy < f_h; fy++) {
          const int iy = oy * p.stride_height - p.padding_values.height + p.dilation_height_factor * fy;
          if (iy < 0 || iy >= in_h)
            continue;
          for (int fx = 0; fx < f_w; fx++) {
            const int ix = ox * p.stride_width - p.padding_values.width + p.dilation_width_factor * fx;
            if (ix < 0 || ix >= in_w)
              continue;
            const int8_t *x = in + ((static_cast<size_t>(batch) * in_h + iy) * in_w + ix) * depth;
            const int8_t *wt = w + (static_cast<size_t>(fy) * f_w + fx) * depth;
            int c = 0;
            for (; c + 8 <= depth; c += 8) {
              const int16x8_t xv = vaddq_s16(vmovl_s8(vld1_s8(x + c)), offset);
              const int16x8_t wv = vmovl_s8(vld1_s8(wt + c));
              vst1q_s32(&acc[c], vmlal_s16(vld1q_s32(&acc[c]), vget_low_s16(wv), vget_low_s16(xv)));
              vst1q_s32(&acc[c + 4], vmlal_s16(vld1q_s32(&acc[c + 4]), vget_high_s16(wv), vget_high_s16(xv)));
            }
            for (; c < depth; c++)
              acc[c] += wt[c] * (x[c] + p.input_offset);
          }
        }
        int8_t *dst = out + ((static_cast<size_t>(batch) * out_h + oy) * out_w + ox) * depth;
        for (int c = 0; c < depth; c++) {
          const int32_t a = acc[c] + (b != nullptr ? b[c] : 0);
          dst[c] = requantize(a, data.per_channel_output_multiplier[c], data.per_channel_output_shift[c],
                              p.output_offset, p.quantized_activation_min, p.quantized_activation_max);
        }
      }
    }
  }
  return kTfLiteOk;
}

}  // namespace

TFLMRegistration register_conv_2d_neon() {
  TFLMRegistration r = tflite::Register_CONV_2D();
  conv_reference_eval = r.invoke;
  r.invoke = conv_eval;
  return r;
}

TFLMRegistration register_depthwise_conv_2d_neon() {
  TFLMRegistration r = tflite::Register_DEPTHWISE_CONV_2D();
  depthwise_reference_eval = r.invoke;
  r.invoke = depthwise_eval;
  return r;
}

}  // namespace micro_wake_word
}  // namespace esphome

#endif
