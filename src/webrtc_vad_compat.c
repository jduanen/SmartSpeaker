/* WebRTC VAD symbol added after version 0.1 — provide a stub so the build
   links against the xenial libwebrtc_audio_processing (v0.1) sysroot.
   Returning 0 means "valid" and lets AudioInput proceed without the check. */
#include <stddef.h>

int WebRtcVad_ValidRateAndFrameLength(int rate, size_t frame_length) {
    (void)rate;
    (void)frame_length;
    return 0;
}
