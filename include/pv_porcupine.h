#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pv_porcupine pv_porcupine_t;

typedef enum {
  PV_STATUS_SUCCESS = 0,
  PV_STATUS_OUT_OF_MEMORY,
  PV_STATUS_IO_ERROR,
  PV_STATUS_INVALID_ARGUMENT,
  PV_STATUS_STOP_ITERATION,
  PV_STATUS_KEY_ERROR,
  PV_STATUS_INVALID_STATE,
  PV_STATUS_RUNTIME_ERROR,
  PV_STATUS_ACTIVATION_ERROR,
  PV_STATUS_ACTIVATION_LIMIT_REACHED,
  PV_STATUS_ACTIVATION_THROTTLED,
  PV_STATUS_ACTIVATION_REFUSED,
} pv_status_t;

const char *pv_status_to_string(pv_status_t status);
int32_t pv_sample_rate(void);
int32_t pv_porcupine_frame_length(void);

pv_status_t pv_porcupine_init(const char *model_path, int32_t num_keywords,
                               const char *const *keyword_paths,
                               const float *sensitivities,
                               pv_porcupine_t **object);
void pv_porcupine_delete(pv_porcupine_t *object);
pv_status_t pv_porcupine_process(pv_porcupine_t *object, const int16_t *pcm,
                                  int32_t *keyword_index);

#ifdef __cplusplus
}
#endif
