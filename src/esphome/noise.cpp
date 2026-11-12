/*
 * SmartSpeaker: Noise_NNpsk0_25519_ChaChaPoly_SHA256, responder side
 *
 * Follows the Noise Protocol Framework (revision 34): sections 5.1-5.3
 * (CipherState, SymmetricState, HandshakeState), 9 (PSK: the "e" token also
 * calls MixKey), 12.3 (ChaChaPoly nonce: 32 zero bits + 64-bit little-endian
 * counter) and 12.4 (SHA-256 HMAC/HKDF).
 */

#include "esphome/noise.hpp"

#include <stdio.h>
#include <string.h>

#include "monocypher.h"

namespace smartspeaker {
namespace esphome {

// ── SHA-256 (FIPS 180-4) ─────────────────────────────────────────────────────

namespace {

struct Sha256 {
  uint32_t s[8];
  uint8_t buf[64];
  size_t buf_len = 0;
  uint64_t total = 0;

  Sha256() {
    static const uint32_t IV[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                   0xa54ff53a, 0x510e527f, 0x9b05688c,
                                   0x1f83d9ab, 0x5be0cd19};
    memcpy(s, IV, sizeof(s));
  }

  static uint32_t ror(uint32_t x, int r) { return (x >> r) | (x << (32 - r)); }

  void block(const uint8_t *p) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
      w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 |
             (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
      uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s[0], b = s[1], c = s[2], d = s[3], e = s[4], f = s[5],
             g = s[6], hh = s[7];
    for (int i = 0; i < 64; i++) {
      uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) +
                    ((e & f) ^ (~e & g)) + K[i] + w[i];
      uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) +
                    ((a & b) ^ (a & c) ^ (b & c));
      hh = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    s[0] += a;
    s[1] += b;
    s[2] += c;
    s[3] += d;
    s[4] += e;
    s[5] += f;
    s[6] += g;
    s[7] += hh;
  }

  void update(const uint8_t *p, size_t len) {
    total += len;
    while (len > 0) {
      size_t take = 64 - buf_len < len ? 64 - buf_len : len;
      memcpy(buf + buf_len, p, take);
      buf_len += take;
      p += take;
      len -= take;
      if (buf_len == 64) {
        block(buf);
        buf_len = 0;
      }
    }
  }

  void final(uint8_t out[32]) {
    uint64_t bits = total * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t zero = 0;
    while (buf_len != 56)
      update(&zero, 1);
    uint8_t len_be[8];
    for (int i = 0; i < 8; i++)
      len_be[i] = (uint8_t)(bits >> (56 - 8 * i));
    update(len_be, 8);
    for (int i = 0; i < 8; i++) {
      out[4 * i] = (uint8_t)(s[i] >> 24);
      out[4 * i + 1] = (uint8_t)(s[i] >> 16);
      out[4 * i + 2] = (uint8_t)(s[i] >> 8);
      out[4 * i + 3] = (uint8_t)s[i];
    }
  }
};

void hmac(const uint8_t key[32], const uint8_t *a, size_t a_len,
          const uint8_t *b, size_t b_len, uint8_t out[32]) {
  uint8_t pad[64];
  uint8_t inner[32];
  memset(pad, 0x36, sizeof(pad));
  for (int i = 0; i < 32; i++)
    pad[i] ^= key[i];
  Sha256 in;
  in.update(pad, 64);
  in.update(a, a_len);
  in.update(b, b_len);
  in.final(inner);
  memset(pad, 0x5c, sizeof(pad));
  for (int i = 0; i < 32; i++)
    pad[i] ^= key[i];
  Sha256 outer;
  outer.update(pad, 64);
  outer.update(inner, 32);
  outer.final(out);
  crypto_wipe(pad, sizeof(pad));
  crypto_wipe(inner, sizeof(inner));
}

/** Noise HKDF with two or three outputs. */
void hkdf(const uint8_t ck[32], const uint8_t *ikm, size_t ikm_len,
          uint8_t o1[32], uint8_t o2[32], uint8_t *o3) {
  uint8_t temp[32];
  hmac(ck, ikm, ikm_len, nullptr, 0, temp);
  uint8_t one = 1, two = 2, three = 3;
  hmac(temp, &one, 1, nullptr, 0, o1);
  hmac(temp, o1, 32, &two, 1, o2);
  if (o3)
    hmac(temp, o2, 32, &three, 1, o3);
  crypto_wipe(temp, sizeof(temp));
}

void make_nonce(uint64_t n, uint8_t nonce[12]) {
  memset(nonce, 0, 4);
  for (int i = 0; i < 8; i++)
    nonce[4 + i] = (uint8_t)(n >> (8 * i));
}

void aead_seal(const uint8_t key[32], uint64_t n, const uint8_t *ad,
               size_t ad_len, const uint8_t *in, size_t len,
               std::vector<uint8_t> &out) {
  uint8_t nonce[12];
  make_nonce(n, nonce);
  crypto_aead_ctx ctx;
  crypto_aead_init_ietf(&ctx, key, nonce);
  size_t at = out.size();
  out.resize(at + len + Noise::TAG_SIZE);
  crypto_aead_write(&ctx, out.data() + at, out.data() + at + len, ad, ad_len,
                    in, len);
  crypto_wipe(&ctx, sizeof(ctx));
}

bool aead_open(const uint8_t key[32], uint64_t n, const uint8_t *ad,
               size_t ad_len, const uint8_t *in, size_t len,
               std::vector<uint8_t> &out) {
  if (len < Noise::TAG_SIZE)
    return false;
  size_t text = len - Noise::TAG_SIZE;
  uint8_t nonce[12];
  make_nonce(n, nonce);
  crypto_aead_ctx ctx;
  crypto_aead_init_ietf(&ctx, key, nonce);
  out.resize(text);
  int r = crypto_aead_read(&ctx, out.data(), in + text, ad, ad_len, in, text);
  crypto_wipe(&ctx, sizeof(ctx));
  return r == 0;
}

bool random_bytes(uint8_t *out, size_t len) {
  FILE *f = fopen("/dev/urandom", "rb");
  if (!f)
    return false;
  bool ok = fread(out, 1, len, f) == len;
  fclose(f);
  return ok;
}

} // namespace

// ── Noise ────────────────────────────────────────────────────────────────────

static const char PROTOCOL_NAME[] = "Noise_NNpsk0_25519_ChaChaPoly_SHA256";

Noise::Noise(const uint8_t key[KEY_SIZE], const uint8_t *prologue,
             size_t prologue_len) {
  memcpy(psk, key, KEY_SIZE);
  // InitializeSymmetric: the name is longer than 32 bytes, so h = HASH(name)
  Sha256 sha;
  sha.update((const uint8_t *)PROTOCOL_NAME, strlen(PROTOCOL_NAME));
  sha.final(h);
  memcpy(ck, h, 32);
  mix_hash(prologue, prologue_len);
}

Noise::~Noise() {
  crypto_wipe(k, sizeof(k));
  crypto_wipe(ck, sizeof(ck));
  crypto_wipe(psk, sizeof(psk));
  crypto_wipe(e_priv, sizeof(e_priv));
  crypto_wipe(recv_k, sizeof(recv_k));
  crypto_wipe(send_k, sizeof(send_k));
}

void Noise::mix_hash(const uint8_t *data, size_t len) {
  Sha256 sha;
  sha.update(h, 32);
  sha.update(data, len);
  sha.final(h);
}

void Noise::mix_key(const uint8_t *ikm, size_t len) {
  hkdf(ck, ikm, len, ck, k, nullptr);
  n = 0;
}

bool Noise::read_message(const uint8_t *msg, size_t len) {
  // psk: MixKeyAndHash
  uint8_t temp_h[32];
  hkdf(ck, psk, KEY_SIZE, ck, temp_h, k);
  n = 0;
  mix_hash(temp_h, 32);

  // e: MixHash(re), and MixKey(re) in a PSK handshake
  if (len < 32 + TAG_SIZE)
    return false;
  memcpy(re, msg, 32);
  mix_hash(re, 32);
  mix_key(re, 32);

  // payload: DecryptAndHash (empty from aioesphomeapi)
  std::vector<uint8_t> payload;
  if (!aead_open(k, n, h, 32, msg + 32, len - 32, payload))
    return false;
  n++;
  mix_hash(msg + 32, len - 32);
  return true;
}

bool Noise::write_message(std::vector<uint8_t> &out) {
  // e
  if (!random_bytes(e_priv, sizeof(e_priv)))
    return false;
  crypto_x25519_public_key(e_pub, e_priv);
  out.insert(out.end(), e_pub, e_pub + 32);
  mix_hash(e_pub, 32);
  mix_key(e_pub, 32);

  // ee
  uint8_t shared[32];
  crypto_x25519(shared, e_priv, re);
  mix_key(shared, 32);
  crypto_wipe(shared, sizeof(shared));
  crypto_wipe(e_priv, sizeof(e_priv));

  // payload: EncryptAndHash(empty)
  size_t at = out.size();
  aead_seal(k, n, h, 32, nullptr, 0, out);
  n++;
  mix_hash(out.data() + at, out.size() - at);

  // Split: the initiator sends with the first key, we with the second
  hkdf(ck, nullptr, 0, recv_k, send_k, nullptr);
  recv_n = send_n = 0;
  return true;
}

bool Noise::decrypt(const uint8_t *in, size_t len, std::vector<uint8_t> &out) {
  if (!aead_open(recv_k, recv_n, nullptr, 0, in, len, out))
    return false;
  recv_n++;
  return true;
}

void Noise::encrypt(const uint8_t *in, size_t len, std::vector<uint8_t> &out) {
  aead_seal(send_k, send_n++, nullptr, 0, in, len, out);
}

} // namespace esphome
} // namespace smartspeaker
