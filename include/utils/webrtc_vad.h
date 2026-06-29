#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WebRtcVadInst VadInst;

VadInst *WebRtcVad_Create(void);
void WebRtcVad_Free(VadInst *handle);
int WebRtcVad_Init(VadInst *handle);
int WebRtcVad_set_mode(VadInst *handle, int mode);
int WebRtcVad_ValidRateAndFrameLength(int rate, size_t frame_length);
int WebRtcVad_Process(VadInst *handle, int fs, const int16_t *audio_frame,
                      size_t frame_length);

#ifdef __cplusplus
}
#endif
