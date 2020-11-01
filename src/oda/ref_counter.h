/*
 * ODANT jscript, CounterCounter
*/


#pragma once


#include "util.h"

#include <atomic>
#include <type_traits>


namespace node {
namespace jscript {


class RefCounter {
public:
  RefCounter();

  RefCounter(const RefCounter&) = delete;
  RefCounter& operator=(const RefCounter&) = delete;

  virtual ~RefCounter() = default;

  friend inline void addRefecence(const RefCounter* p);
  friend inline void releaseRefecence(const RefCounter* p);

  template <typename Derived,
            typename = std::enable_if_t< std::is_convertible<Derived*, RefCounter*>::value >
  >
  class Ptr {
   public:
    Ptr() : _p(nullptr) {}
    Ptr(Derived* p) : _p(p) { addRefecence(_p); }
    Ptr(Ptr const& other) : _p(other._p) { addRefecence(_p); }
    Ptr(Ptr&& other) : _p(other._p) { other._p = nullptr; }

    Ptr& operator=(Ptr const& other) {
      reset(other._p);
      return *this;
    }
    Ptr& operator=(Ptr&& other) {
      _p = other._p;
      other._p = nullptr;
      return *this;
    }

    ~Ptr() { releaseRefecence(_p); }

    void reset() {
      releaseRefecence(_p);
      _p = nullptr;
    }

    void reset(Derived* p) {
      releaseRefecence(_p);
      _p = p;
      addRefecence(_p);
    }

    void adopt(Derived* p) {
      releaseRefecence(_p);
      _p = p;
    }

    Derived* get() const { return _p; }

    Derived* detach() {
      Derived* p = _p;
      _p = nullptr;
      return p;
    }

    Derived& operator*() const {
      CHECK_NOT_NULL(_p);
      return *_p;
    }

    Derived* operator->() const {
      CHECK_NOT_NULL(_p);
      return _p;
    }

    explicit operator bool() const { return _p != nullptr; }

   private:
    Derived* _p;
  };

private:
  void addRef() const;
  std::size_t releaseRef() const;

  mutable std::atomic_size_t _refCount;
};


inline RefCounter::RefCounter()
  :
    _refCount{0}
{}

inline void RefCounter::addRef() const {
  ++_refCount;
}

inline std::size_t RefCounter::releaseRef() const {
  return --_refCount;
}


inline void addRefecence(const RefCounter* p) {
  if (p != nullptr) {
    p->addRef();
  }
}

inline void releaseRefecence(const RefCounter* p) {
  if (p != nullptr) {
    const auto refCount = p->releaseRef();
    if (refCount == 0) {
      delete p;
    }
  }
}


} // namespace node
} // namespace jscript
