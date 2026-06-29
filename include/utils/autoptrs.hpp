#pragma once

#include <glib-object.h>

namespace smartspeaker {

enum class adopt_mode {
  owned,    // caller already owns a ref (e.g. g_object_new, json_builder_new)
  ref_sink, // sink a floating GInitiallyUnowned ref (e.g. gst_pipeline_new)
};

template <typename T>
class auto_gobject_ptr {
  T *_ptr;

  template <typename U>
  friend class auto_gobject_ptr;

public:
  auto_gobject_ptr() : _ptr(nullptr) {}

  auto_gobject_ptr(T *p, adopt_mode mode) : _ptr(p) {
    if (_ptr && mode == adopt_mode::ref_sink)
      g_object_ref_sink(G_OBJECT(_ptr));
  }

  ~auto_gobject_ptr() {
    if (_ptr)
      g_object_unref(G_OBJECT(_ptr));
  }

  auto_gobject_ptr(const auto_gobject_ptr &o) : _ptr(o._ptr) {
    if (_ptr)
      g_object_ref(G_OBJECT(_ptr));
  }

  auto_gobject_ptr(auto_gobject_ptr &&o) noexcept : _ptr(o._ptr) {
    o._ptr = nullptr;
  }

  auto_gobject_ptr &operator=(const auto_gobject_ptr &o) {
    if (this != &o) {
      if (o._ptr)
        g_object_ref(G_OBJECT(o._ptr));
      if (_ptr)
        g_object_unref(G_OBJECT(_ptr));
      _ptr = o._ptr;
    }
    return *this;
  }

  auto_gobject_ptr &operator=(auto_gobject_ptr &&o) noexcept {
    if (this != &o) {
      if (_ptr)
        g_object_unref(G_OBJECT(_ptr));
      _ptr = o._ptr;
      o._ptr = nullptr;
    }
    return *this;
  }

  T *get() const { return _ptr; }

  GType type() const { return G_OBJECT_TYPE(G_OBJECT(_ptr)); }

  template <typename U>
  auto_gobject_ptr<U> cast(GType) const {
    auto_gobject_ptr<U> result;
    result._ptr = reinterpret_cast<U *>(_ptr);
    if (result._ptr)
      g_object_ref(G_OBJECT(result._ptr));
    return result;
  }

  explicit operator bool() const { return _ptr != nullptr; }
};

template <typename T, void (*FN)(T *)>
struct fn_deleter {
  void operator()(T *p) const {
    if (p)
      FN(p);
  }
};

} // namespace smartspeaker
