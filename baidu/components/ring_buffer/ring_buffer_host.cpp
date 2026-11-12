#include "ring_buffer.h"

#ifdef USE_HOST

#include <algorithm>
#include <chrono>
#include <cstring>

namespace esphome::ring_buffer {

namespace {
template<typename Pred>
bool wait_for(std::condition_variable &cv, std::unique_lock<std::mutex> &lock, TickType_t ticks, Pred pred) {
  if (ticks == portMAX_DELAY) {
    cv.wait(lock, pred);
    return true;
  }
  return cv.wait_for(lock, std::chrono::milliseconds(ticks), pred);
}
}  // namespace

std::unique_ptr<RingBuffer> RingBuffer::create(size_t len, MemoryPreference /*preference*/) {
  auto rb = std::make_unique<RingBuffer>();
  rb->storage_.resize(len);
  return rb;
}

void *RingBuffer::receive_acquire(size_t &length, size_t max_length, TickType_t ticks_to_wait) {
  std::unique_lock<std::mutex> lock(this->mutex_);
  length = 0;
  if (this->acquired_ != 0)
    return nullptr;  // one outstanding item at a time, as FreeRTOS byte buffers
  if (!wait_for(this->readable_, lock, ticks_to_wait, [this] { return this->used_ > 0; }))
    return nullptr;
  // contiguous run from tail_, up to the end of storage
  length = std::min({max_length, this->used_, this->storage_.size() - this->tail_});
  this->acquired_ = length;
  return this->storage_.data() + this->tail_;
}

void RingBuffer::receive_release(void * /*item*/) {
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->tail_ = (this->tail_ + this->acquired_) % this->storage_.size();
    this->used_ -= this->acquired_;
    this->acquired_ = 0;
  }
  this->writable_.notify_all();
}

size_t RingBuffer::read(void *data, size_t len, TickType_t ticks_to_wait) {
  size_t got = 0;
  {
    std::unique_lock<std::mutex> lock(this->mutex_);
    if (!wait_for(this->readable_, lock, ticks_to_wait, [this] { return this->used_ > this->acquired_; }))
      return 0;
    auto *out = static_cast<uint8_t *>(data);
    while (got < len && this->used_ > this->acquired_) {
      size_t n = std::min({len - got, this->used_ - this->acquired_, this->storage_.size() - this->tail_});
      std::memcpy(out + got, this->storage_.data() + this->tail_, n);
      this->tail_ = (this->tail_ + n) % this->storage_.size();
      this->used_ -= n;
      got += n;
    }
  }
  this->writable_.notify_all();
  return got;
}

size_t RingBuffer::write_locked_(const uint8_t *data, size_t len) {
  size_t put = 0;
  while (put < len) {
    size_t n = std::min(len - put, this->storage_.size() - this->head_);
    std::memcpy(this->storage_.data() + this->head_, data + put, n);
    this->head_ = (this->head_ + n) % this->storage_.size();
    put += n;
  }
  this->used_ += len;
  return len;
}

void RingBuffer::discard_locked_(size_t len) {
  len = std::min(len, this->used_ - this->acquired_);
  this->tail_ = (this->tail_ + len) % this->storage_.size();
  this->used_ -= len;
}

size_t RingBuffer::write(const void *data, size_t len) {
  size_t written;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    len = std::min(len, this->storage_.size() - this->acquired_);
    size_t free = this->storage_.size() - this->used_;
    if (free < len)
      this->discard_locked_(len - free);  // replace the oldest data, as on ESP32
    len = std::min(len, this->storage_.size() - this->used_);
    written = this->write_locked_(static_cast<const uint8_t *>(data), len);
  }
  this->readable_.notify_all();
  return written;
}

size_t RingBuffer::write_without_replacement(const void *data, size_t len, TickType_t ticks_to_wait,
                                             bool write_partial) {
  size_t written;
  {
    std::unique_lock<std::mutex> lock(this->mutex_);
    bool fits = wait_for(this->writable_, lock, ticks_to_wait,
                         [this, len] { return this->storage_.size() - this->used_ >= len; });
    if (!fits && !write_partial)
      return 0;
    written = this->write_locked_(static_cast<const uint8_t *>(data),
                                  std::min(len, this->storage_.size() - this->used_));
  }
  this->readable_.notify_all();
  return written;
}

size_t RingBuffer::available() const {
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->used_ - this->acquired_;
}

size_t RingBuffer::free() const {
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->storage_.size() - this->used_;
}

BaseType_t RingBuffer::reset() {
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->discard_locked_(this->used_);
  }
  this->writable_.notify_all();
  return pdTRUE;
}

}  // namespace esphome::ring_buffer

#endif  // USE_HOST
