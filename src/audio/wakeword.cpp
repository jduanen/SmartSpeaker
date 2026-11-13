/*
 * SmartSpeaker: on-device wake word detection (microWakeWord)
 *
 * Ported from esphome/components/micro_wake_word (micro_wake_word.cpp,
 * streaming_model.cpp, preprocessor_settings.h): same features, same model
 * handling, same detection rule. Models and their manifests are read from
 * [esphome] model_dir: <id>.json + <id>.tflite.
 */

#include "wakeword.hpp"
#include "neon_kernels.hpp"

#include <algorithm>
#include <glib.h>
#include <string.h>

#include <frontend.h>
#include <frontend_util.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>
#include <tensorflow/lite/schema/schema_generated.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::WakeWord"

namespace smartspeaker {

// preprocessor_settings.h: every microWakeWord model is trained on these
static const size_t FEATURE_SIZE = 40;
static const size_t FEATURE_DURATION_MS = 30;
static const size_t FEATURE_STEP_MS = 10;

// streaming_model.h
static const int MIN_SLICES_BEFORE_DETECTION = 100;
static const size_t VARIABLE_ARENA_SIZE = 1024;

static const char *const STOP_ID = "stop";

const char *const WakeWord::SENSITIVITIES[3] = {
    "Slightly sensitive", "Moderately sensitive", "Very sensitive"};

// HA Voice PE's wake_word_sensitivity select (probability cutoffs, 0-255);
// other models keep their manifest cutoff
static const struct {
  const char *id;
  uint8_t cutoff[3];
} CUTOFFS[] = {
    {"okay_nabu", {217, 176, 143}},
    {"hey_jarvis", {247, 235, 212}},
    {"hey_mycroft", {253, 242, 237}},
};

/** One manifest + model file; the interpreter exists only while in use. */
struct WakeWord::Model {
  std::string id;
  std::string phrase;
  std::vector<std::string> languages;
  std::vector<uint8_t> flatbuffer;
  uint8_t default_cutoff = 0;
  uint8_t cutoff = 0;
  size_t window_size = 5;
  size_t arena_size = 0;

  tflite::MicroMutableOpResolver<20> resolver;
  std::vector<uint8_t> tensor_arena;
  std::vector<uint8_t> var_arena;
  std::unique_ptr<tflite::MicroInterpreter> interpreter;
  std::vector<uint8_t> probabilities;
  size_t last_index = 0;
  uint8_t stride_step = 0;
  int ignore_windows = -MIN_SLICES_BEFORE_DETECTION;
  bool fresh = false; // a new probability since the last check

  bool load();
  void unload();
  bool infer(const int8_t *features);
  bool detected();
  void reset();
};

// ── manifest parsing ─────────────────────────────────────────────────────────
// The manifests are small flat JSON objects; pull out the few keys we need.

static const char *find_key(const std::string &json, const char *key) {
  std::string quoted = std::string("\"") + key + "\"";
  size_t at = json.find(quoted);
  if (at == std::string::npos)
    return nullptr;
  at = json.find(':', at + quoted.size());
  if (at == std::string::npos)
    return nullptr;
  const char *p = json.c_str() + at + 1;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
    p++;
  return p;
}

static std::string json_string(const std::string &json, const char *key) {
  const char *p = find_key(json, key);
  if (!p || *p != '"')
    return "";
  const char *end = strchr(p + 1, '"');
  return end ? std::string(p + 1, end) : "";
}

static double json_number(const std::string &json, const char *key,
                          double fallback) {
  const char *p = find_key(json, key);
  return p ? g_ascii_strtod(p, nullptr) : fallback;
}

static std::vector<std::string> json_strings(const std::string &json,
                                             const char *key) {
  std::vector<std::string> out;
  const char *p = find_key(json, key);
  if (!p || *p != '[')
    return out;
  const char *end = strchr(p, ']');
  for (p++; end && p < end; p++) {
    if (*p != '"')
      continue;
    const char *close = strchr(p + 1, '"');
    if (!close || close > end)
      break;
    out.emplace_back(p + 1, close);
    p = close;
  }
  return out;
}

// ── model ────────────────────────────────────────────────────────────────────

// streaming_model.cpp register_streaming_ops_(), with the NEON conv kernels
// (neon_kernels.cpp) in place of TFLM's reference ones
static void register_ops(tflite::MicroMutableOpResolver<20> &r) {
  r.AddCallOnce();
  r.AddVarHandle();
  r.AddReshape();
  r.AddReadVariable();
  r.AddStridedSlice();
  r.AddConcatenation();
  r.AddAssignVariable();
#ifdef __ARM_NEON
  r.AddConv2D(register_conv_2d_neon());
#else
  r.AddConv2D();
#endif
  r.AddMul();
  r.AddAdd();
  r.AddMean();
  r.AddFullyConnected();
  r.AddLogistic();
  r.AddQuantize();
#ifdef __ARM_NEON
  r.AddDepthwiseConv2D(register_depthwise_conv_2d_neon());
#else
  r.AddDepthwiseConv2D();
#endif
  r.AddAveragePool2D();
  r.AddMaxPool2D();
  r.AddPad();
  r.AddPack();
  r.AddSplitV();
}

bool WakeWord::Model::load() {
  const tflite::Model *model = tflite::GetModel(flatbuffer.data());
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    g_warning("%s: unsupported model schema %u", id.c_str(), model->version());
    return false;
  }

  tensor_arena.assign(arena_size, 0);
  var_arena.assign(VARIABLE_ARENA_SIZE, 0);
  tflite::MicroAllocator *allocator =
      tflite::MicroAllocator::Create(var_arena.data(), var_arena.size());
  tflite::MicroResourceVariables *variables =
      tflite::MicroResourceVariables::Create(allocator, 20);
  interpreter.reset(new tflite::MicroInterpreter(
      model, resolver, tensor_arena.data(), tensor_arena.size(), variables));
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    g_warning("%s: cannot allocate tensors (arena %zu bytes)", id.c_str(),
              arena_size);
    unload();
    return false;
  }

  TfLiteTensor *input = interpreter->input(0);
  TfLiteTensor *output = interpreter->output(0);
  if (input->dims->size != 3 || input->dims->data[0] != 1 ||
      input->dims->data[2] != (int)FEATURE_SIZE || input->type != kTfLiteInt8 ||
      output->type != kTfLiteUInt8) {
    g_warning("%s: unexpected model input/output", id.c_str());
    unload();
    return false;
  }

  reset();
  stride_step = 0;
  return true;
}

void WakeWord::Model::unload() {
  interpreter.reset();
  // the allocator and variables live inside the arenas
  std::vector<uint8_t>().swap(tensor_arena);
  std::vector<uint8_t>().swap(var_arena);
}

// streaming_model.cpp perform_streaming_inference()
bool WakeWord::Model::infer(const int8_t *features) {
  TfLiteTensor *input = interpreter->input(0);
  uint8_t stride = input->dims->data[1];
  stride_step = stride_step % stride;
  memcpy(tflite::GetTensorData<int8_t>(input) + FEATURE_SIZE * stride_step,
         features, FEATURE_SIZE);
  ++stride_step;

  if (stride_step >= stride) {
    if (interpreter->Invoke() != kTfLiteOk) {
      g_warning("%s: inference failed", id.c_str());
      return false;
    }
    last_index = (last_index + 1) % window_size;
    probabilities[last_index] = interpreter->output(0)->data.uint8[0];
    fresh = true;
  }
  // cool off after a detection: count down only below the cutoff
  if (probabilities[last_index] < cutoff)
    ignore_windows = std::min(ignore_windows + 1, 0);
  return true;
}

// streaming_model.cpp WakeWordModel::determine_detected()
bool WakeWord::Model::detected() {
  if (!fresh || ignore_windows < 0)
    return false;
  fresh = false;
  uint32_t sum = 0;
  uint8_t max = 0;
  for (uint8_t p : probabilities) {
    sum += p;
    max = std::max(max, p);
  }
  if (sum <= (uint32_t)cutoff * window_size)
    return false;
  g_message("Detected '%s' (average %.2f, max %.2f)", phrase.c_str(),
            sum / (255.0 * window_size), max / 255.0);
  reset();
  return true;
}

void WakeWord::Model::reset() {
  probabilities.assign(window_size, 0);
  ignore_windows = -MIN_SLICES_BEFORE_DETECTION;
  fresh = false;
}

static std::unique_ptr<WakeWord::Model> read_model(const char *dir,
                                                   const char *json_name) {
  gchar *json_path = g_build_filename(dir, json_name, nullptr);
  gchar *text = nullptr;
  bool ok = g_file_get_contents(json_path, &text, nullptr, nullptr);
  g_free(json_path);
  if (!ok)
    return nullptr;
  std::string json(text);
  g_free(text);

  if (json_string(json, "type") != "micro" ||
      json_number(json, "version", 0) != 2)
    return nullptr;

  std::unique_ptr<WakeWord::Model> m(new WakeWord::Model());
  std::string name(json_name);
  m->id = name.substr(0, name.size() - 5); // strip ".json"
  m->phrase = json_string(json, "wake_word");
  m->languages = json_strings(json, "trained_languages");
  m->default_cutoff = m->cutoff = (uint8_t)std::min(
      255.0, json_number(json, "probability_cutoff", 0.97) * 255.0 + 0.5);
  m->window_size = (size_t)json_number(json, "sliding_window_size", 5);
  m->arena_size = (size_t)json_number(json, "tensor_arena_size", 0);
  if (json_number(json, "feature_step_size", FEATURE_STEP_MS) != FEATURE_STEP_MS) {
    g_warning("%s: feature step size isn't %zu ms; skipped", m->id.c_str(),
              FEATURE_STEP_MS);
    return nullptr;
  }

  gchar *model_path =
      g_build_filename(dir, json_string(json, "model").c_str(), nullptr);
  gchar *data = nullptr;
  gsize size = 0;
  ok = g_file_get_contents(model_path, &data, &size, nullptr);
  g_free(model_path);
  if (!ok || m->phrase.empty() || m->arena_size == 0) {
    g_warning("%s: incomplete manifest or missing model", m->id.c_str());
    g_free(data);
    return nullptr;
  }
  // GetModel() needs the flatbuffer aligned; vector storage is
  m->flatbuffer.assign(data, data + size);
  g_free(data);
  m->probabilities.assign(m->window_size, 0);
  register_ops(m->resolver);
  return m;
}

// ── WakeWord ─────────────────────────────────────────────────────────────────

WakeWord::WakeWord(Config *config)
    : pv_frame_length(512), sample_rate(16000), config(config) {
  const char *dir = config->esphome_model_dir;
  GDir *d = g_dir_open(dir, 0, nullptr);
  if (d) {
    std::vector<std::string> names;
    while (const gchar *name = g_dir_read_name(d))
      if (g_str_has_suffix(name, ".json"))
        names.push_back(name);
    g_dir_close(d);
    std::sort(names.begin(), names.end());
    for (const auto &name : names) {
      auto m = read_model(dir, name.c_str());
      if (!m)
        continue;
      if (m->id == STOP_ID) {
        stop_model = m.get();
      } else {
        infos.push_back({m->id, m->phrase, m->languages});
      }
      models.push_back(std::move(m));
    }
  }
  if (infos.empty())
    g_warning("No wake word models in %s; use PLAYPAUSE to listen", dir);

  // ESPHome's preprocessor settings, with the models' 10 ms step
  struct FrontendConfig fc;
  FrontendFillConfigWithDefaults(&fc);
  fc.window.size_ms = FEATURE_DURATION_MS;
  fc.window.step_size_ms = FEATURE_STEP_MS;
  fc.filterbank.num_channels = FEATURE_SIZE;
  fc.filterbank.lower_band_limit = 125.0;
  fc.filterbank.upper_band_limit = 7500.0;
  fc.noise_reduction.smoothing_bits = 10;
  fc.noise_reduction.even_smoothing = 0.025;
  fc.noise_reduction.odd_smoothing = 0.06;
  fc.noise_reduction.min_signal_remaining = 0.05;
  fc.pcan_gain_control.enable_pcan = 1;
  fc.pcan_gain_control.strength = 0.95;
  fc.pcan_gain_control.offset = 80.0;
  fc.pcan_gain_control.gain_bits = 21;
  fc.log_scale.enable_log = 1;
  fc.log_scale.scale_shift = 6;
  frontend = new FrontendState();
  if (!FrontendPopulateState(&fc, frontend, sample_rate))
    g_error("Cannot allocate the wake word feature frontend");

  for (size_t i = 0; i < G_N_ELEMENTS(SENSITIVITIES); i++)
    if (g_strcmp0(config->wakeword_sensitivity, SENSITIVITIES[i]) == 0)
      sensitivity_level = i;
  if (!set_active(config->wakeword_active) && !infos.empty())
    set_active(infos[0].id);
}

WakeWord::~WakeWord() {
  if (frontend) {
    FrontendFreeStateContents(frontend);
    delete frontend;
  }
}

WakeWord::Detection WakeWord::process(const AudioFrame *frame) {
  std::lock_guard<std::mutex> guard(lock);
  if (!active_model && !(stop_enabled && stop_model))
    return Detection::NONE;

  Detection result = Detection::NONE;
  const int16_t *samples = frame->samples;
  size_t left = frame->length;
  while (left > 0) {
    size_t used = 0;
    struct FrontendOutput out =
        FrontendProcessSamples(frontend, samples, left, &used);
    samples += used;
    left -= used;
    if (out.size != FEATURE_SIZE)
      continue;

    // micro_wake_word.cpp generate_features_(): scale the frontend's
    // 0..~670 output to the models' int8 input
    int8_t features[FEATURE_SIZE];
    for (size_t i = 0; i < FEATURE_SIZE; i++) {
      int32_t v = ((int32_t)out.values[i] * 256 + 333) / 666 - 128;
      features[i] = (int8_t)std::max(-128, std::min(127, v));
    }

    if (active_model && active_model->infer(features) &&
        active_model->detected())
      result = Detection::WAKE;
    if (stop_enabled && stop_model && stop_model->interpreter &&
        stop_model->infer(features) && stop_model->detected() &&
        result == Detection::NONE)
      result = Detection::STOP;
  }
  return result;
}

std::string WakeWord::active() {
  std::lock_guard<std::mutex> guard(lock);
  return active_model ? active_model->id : "";
}

std::string WakeWord::active_phrase() {
  std::lock_guard<std::mutex> guard(lock);
  return active_model ? active_model->phrase : "";
}

bool WakeWord::set_active(const std::string &id) {
  std::lock_guard<std::mutex> guard(lock);
  if (active_model && active_model->id == id)
    return true;
  for (auto &m : models) {
    if (m->id != id || m.get() == stop_model)
      continue;
    if (!m->load())
      return false;
    if (active_model)
      active_model->unload();
    active_model = m.get();
    apply_sensitivity();
    g_message("Wake word: %s (%s)", m->phrase.c_str(),
              SENSITIVITIES[sensitivity_level]);
    if (g_strcmp0(config->wakeword_active, id.c_str()) != 0) {
      g_free(config->wakeword_active);
      config->wakeword_active = g_strdup(id.c_str());
      config->set_string("wakeword", "active", id.c_str());
    }
    return true;
  }
  g_warning("Unknown wake word '%s'", id.c_str());
  return false;
}

std::string WakeWord::sensitivity() {
  std::lock_guard<std::mutex> guard(lock);
  return SENSITIVITIES[sensitivity_level];
}

bool WakeWord::set_sensitivity(const std::string &level) {
  std::lock_guard<std::mutex> guard(lock);
  for (size_t i = 0; i < G_N_ELEMENTS(SENSITIVITIES); i++) {
    if (level != SENSITIVITIES[i])
      continue;
    sensitivity_level = i;
    apply_sensitivity();
    g_free(config->wakeword_sensitivity);
    config->wakeword_sensitivity = g_strdup(SENSITIVITIES[i]);
    config->set_string("wakeword", "sensitivity", SENSITIVITIES[i]);
    return true;
  }
  return false;
}

void WakeWord::set_stop_enabled(bool enabled) {
  std::lock_guard<std::mutex> guard(lock);
  if (!stop_model || enabled == stop_enabled)
    return;
  stop_enabled = enabled;
  // load on demand so the stop word starts from a clean state each time
  if (enabled && !stop_model->load())
    stop_enabled = false;
  else if (!enabled)
    stop_model->unload();
}

// with `lock` held
void WakeWord::apply_sensitivity() {
  if (!active_model)
    return;
  active_model->cutoff = active_model->default_cutoff;
  for (const auto &c : CUTOFFS)
    if (active_model->id == c.id)
      active_model->cutoff = c.cutoff[sensitivity_level];
}

} // namespace smartspeaker
