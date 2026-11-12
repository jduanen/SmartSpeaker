/*
 * SmartSpeaker: Noise_NNpsk0_25519_ChaChaPoly_SHA256, responder side
 *
 * The ESPHome native API's encrypted transport (aioesphomeapi uses the
 * Python noiseprotocol package as the initiator). One handshake round:
 *   -> psk, e      (read_message)
 *   <- e, ee       (write_message), then Split()
 * X25519 and ChaCha20-Poly1305 come from Monocypher; SHA-256, HMAC and the
 * Noise HKDF are in noise.cpp.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace smartspeaker {
namespace esphome {

class Noise {
public:
  static const size_t KEY_SIZE = 32;
  static const size_t TAG_SIZE = 16;

  /** `psk` is the base64-decoded API encryption key (all zeros: none). */
  Noise(const uint8_t psk[KEY_SIZE], const uint8_t *prologue,
        size_t prologue_len);
  ~Noise();

  /** The client's handshake message; false if the PSK doesn't match. */
  bool read_message(const uint8_t *msg, size_t len);
  /** Our handshake reply; afterwards encrypt/decrypt work. */
  bool write_message(std::vector<uint8_t> &out);

  /** Transport messages: ciphertext || 16-byte tag. */
  bool decrypt(const uint8_t *in, size_t len, std::vector<uint8_t> &out);
  void encrypt(const uint8_t *in, size_t len, std::vector<uint8_t> &out);

private:
  // SymmetricState
  uint8_t h[32], ck[32], k[32];
  uint64_t n = 0;
  uint8_t psk[KEY_SIZE];
  uint8_t re[32];               // client ephemeral
  uint8_t e_priv[32], e_pub[32]; // ours
  // CipherStates after Split()
  uint8_t recv_k[32], send_k[32];
  uint64_t recv_n = 0, send_n = 0;

  void mix_hash(const uint8_t *data, size_t len);
  void mix_key(const uint8_t *ikm, size_t len);
};

} // namespace esphome
} // namespace smartspeaker
