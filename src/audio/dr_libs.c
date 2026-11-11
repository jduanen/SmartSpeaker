/*
 * Single compilation unit for the header-only dr_libs decoders
 * (third_party/dr_libs). Streams come from ByteStream callbacks, so the
 * stdio helpers are left out.
 */

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#include "dr_flac.h"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "dr_wav.h"
