#pragma once
// baidu: the FreeRTOS pieces micro_wake_word.cpp uses (event group, queue,
// StaticTask, vTaskSuspend/vTaskResume), on std::thread for the host platform,
// so upstream's inference task logic compiles unchanged. Ticks are ms
// (baidu_host/host_shim.h).

#ifdef USE_HOST

#include "esphome/components/baidu_host/host_shim.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <pthread.h>
#include <thread>
#include <vector>

using EventBits_t = uint32_t;

struct HostEventGroup {
  std::mutex mutex;
  std::condition_variable cond;
  EventBits_t bits{0};
};
using EventGroupHandle_t = HostEventGroup *;

inline EventGroupHandle_t xEventGroupCreate() { return new HostEventGroup(); }

inline EventBits_t xEventGroupGetBits(EventGroupHandle_t group) {
  std::lock_guard<std::mutex> lock(group->mutex);
  return group->bits;
}

inline EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits) {
  std::lock_guard<std::mutex> lock(group->mutex);
  group->bits |= bits;
  group->cond.notify_all();
  return group->bits;
}

inline EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits) {
  std::lock_guard<std::mutex> lock(group->mutex);
  EventBits_t before = group->bits;
  group->bits &= ~bits;
  return before;
}

/// Returns the bits when the wait ended (before any clearing), like FreeRTOS.
inline EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits, BaseType_t clear_on_exit,
                                       BaseType_t wait_for_all, TickType_t ticks) {
  std::unique_lock<std::mutex> lock(group->mutex);
  auto met = [&] { return wait_for_all ? (group->bits & bits) == bits : (group->bits & bits) != 0; };
  if (ticks == portMAX_DELAY) {
    group->cond.wait(lock, met);
  } else {
    group->cond.wait_for(lock, std::chrono::milliseconds(ticks), met);
  }
  EventBits_t result = group->bits;
  if (met() && clear_on_exit)
    group->bits &= ~bits;
  return result;
}

struct HostQueue {
  std::mutex mutex;
  std::condition_variable cond;
  size_t length;
  size_t item_size;
  std::deque<std::vector<uint8_t>> items;
};
using QueueHandle_t = HostQueue *;

inline QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
  auto *queue = new HostQueue();
  queue->length = length;
  queue->item_size = item_size;
  return queue;
}

inline BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks) {
  std::unique_lock<std::mutex> lock(queue->mutex);
  auto room = [&] { return queue->items.size() < queue->length; };
  if (ticks == portMAX_DELAY) {
    queue->cond.wait(lock, room);
  } else if (!queue->cond.wait_for(lock, std::chrono::milliseconds(ticks), room)) {
    return pdFALSE;
  }
  const auto *p = static_cast<const uint8_t *>(item);
  queue->items.emplace_back(p, p + queue->item_size);
  queue->cond.notify_all();
  return pdTRUE;
}

inline BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks) {
  std::unique_lock<std::mutex> lock(queue->mutex);
  auto ready = [&] { return !queue->items.empty(); };
  if (ticks == portMAX_DELAY) {
    queue->cond.wait(lock, ready);
  } else if (!queue->cond.wait_for(lock, std::chrono::milliseconds(ticks), ready)) {
    return pdFALSE;
  }
  std::memcpy(item, queue->items.front().data(), queue->item_size);
  queue->items.pop_front();
  queue->cond.notify_all();
  return pdTRUE;
}

inline BaseType_t xQueueReset(QueueHandle_t queue) {
  std::lock_guard<std::mutex> lock(queue->mutex);
  queue->items.clear();
  queue->cond.notify_all();
  return pdTRUE;
}

using TaskHandle_t = std::thread *;
using TaskFunction_t = void (*)(void *);

// A thread can't be suspended from outside on host. The inference task calls
// vTaskSuspend(nullptr) as its last step to wait for deletion; here it just
// returns, so the thread ends and StaticTask::deallocate() joins it. The OTA
// suspend/resume pair becomes a no-op (host OTA replaces the process anyway).
inline void vTaskSuspend(TaskHandle_t) {}
inline void vTaskResume(TaskHandle_t) {}

namespace esphome {

/// esphome/core/static_task.h (ESP32-only) on std::thread; stack size,
/// priority and PSRAM placement don't apply.
class StaticTask {
 public:
  bool is_created() const { return this->thread_ != nullptr; }
  TaskHandle_t get_handle() const { return this->thread_; }

  bool create(TaskFunction_t fn, const char *name, uint32_t stack_size, void *param, UBaseType_t priority,
              bool use_psram) {
    if (this->thread_ != nullptr)
      return false;
    this->thread_ = new std::thread(fn, param);
    pthread_setname_np(this->thread_->native_handle(), name);  // for top -H
    return true;
  }

  /// Joins the finished thread; the caller only calls this after the task signalled it stopped.
  bool deallocate() {
    if (this->thread_ == nullptr)
      return true;
    if (this->thread_->joinable())
      this->thread_->join();
    delete this->thread_;
    this->thread_ = nullptr;
    return true;
  }

 protected:
  std::thread *thread_{nullptr};
};

}  // namespace esphome

#endif  // USE_HOST
