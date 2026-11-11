/*
 * SmartSpeaker: ESPHome native API plaintext framing
 */

#include "esphome/frame.hpp"

#include "pb_encode.h"

namespace smartspeaker {
namespace esphome {

// Largest payload we accept from a client; HA's biggest messages (TTS
// announcements with long URLs, voice audio) are far smaller.
static const uint32_t MAX_PAYLOAD = 64 * 1024;

static bool read_varint(const uint8_t *data, size_t len, size_t *pos,
                        uint32_t *value) {
  uint32_t result = 0;
  for (int shift = 0; shift < 32; shift += 7) {
    if (*pos >= len)
      return false;
    uint8_t b = data[(*pos)++];
    result |= (uint32_t)(b & 0x7f) << shift;
    if (!(b & 0x80)) {
      *value = result;
      return true;
    }
  }
  return false;
}

static void write_varint(std::vector<uint8_t> &out, uint32_t value) {
  while (value >= 0x80) {
    out.push_back((uint8_t)(value | 0x80));
    value >>= 7;
  }
  out.push_back((uint8_t)value);
}

FrameResult parse_frame(const uint8_t *data, size_t len, uint32_t *type,
                        size_t *payload_off, size_t *payload_len,
                        size_t *consumed) {
  if (len == 0)
    return FrameResult::INCOMPLETE;
  if (data[0] != 0x00)
    return FrameResult::ERROR; // 0x01 is a Noise client; not supported yet

  size_t pos = 1;
  uint32_t length;
  if (!read_varint(data, len, &pos, &length))
    return len > 6 ? FrameResult::ERROR : FrameResult::INCOMPLETE;
  if (length > MAX_PAYLOAD)
    return FrameResult::ERROR;
  if (!read_varint(data, len, &pos, type))
    return len > 11 ? FrameResult::ERROR : FrameResult::INCOMPLETE;
  if (len - pos < length)
    return FrameResult::INCOMPLETE;

  *payload_off = pos;
  *payload_len = length;
  *consumed = pos + length;
  return FrameResult::OK;
}

bool encode_frame(uint32_t type, const pb_msgdesc_t *fields, const void *msg,
                  std::vector<uint8_t> &out) {
  size_t size = 0;
  if (!pb_get_encoded_size(&size, fields, msg))
    return false;

  out.push_back(0x00);
  write_varint(out, (uint32_t)size);
  write_varint(out, type);

  size_t start = out.size();
  out.resize(start + size);
  pb_ostream_t stream = pb_ostream_from_buffer(out.data() + start, size);
  return pb_encode(&stream, fields, msg);
}

} // namespace esphome
} // namespace smartspeaker
