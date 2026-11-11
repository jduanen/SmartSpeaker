/*
 * SmartSpeaker: ESPHome native API plaintext framing
 *
 * A frame is: 0x00, varint payload length, varint message type, payload
 * (the protobuf-encoded message). See aioesphomeapi _frame_helper/plain_text.py
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pb.h"

namespace smartspeaker {
namespace esphome {

enum class FrameResult { OK, INCOMPLETE, ERROR };

/**
 * Parse one frame from the front of `data`. On OK, the payload is
 * data[payload_off, payload_off + payload_len) and `consumed` bytes
 * belong to the frame.
 */
FrameResult parse_frame(const uint8_t *data, size_t len, uint32_t *type,
                        size_t *payload_off, size_t *payload_len,
                        size_t *consumed);

/** Append the frame for `msg` (described by `fields`) to `out`. */
bool encode_frame(uint32_t type, const pb_msgdesc_t *fields, const void *msg,
                  std::vector<uint8_t> &out);

} // namespace esphome
} // namespace smartspeaker
