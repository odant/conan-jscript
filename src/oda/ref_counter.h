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

  template<typename Derived>
  using IsConstructPtr = std::enable_if_t< std::is_convertible<Derived*, RefCounter*>::value >;

  template <typename Derived,
            typename = IsConstructPtr<Derived>
  >
  class Ptr;

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


/* RefCounter::Ptr implementation */


template<typename Derived, typename T>
class RefCounter::Ptr
{
public:
  Ptr();

  Ptr(Derived* p);
  Ptr(const Ptr& other);
  Ptr(Ptr&& other);

  Ptr& operator= (const Ptr& other);
  Ptr& operator= (Ptr&& other);

  ~ Ptr();

  void reset();
  void reset(Derived* p);
  void adopt(Derived* p);
  Derived* get();
  Derived* detach();

  Derived& operator* () const;
  Derived* operator-> () const;

  explicit operator bool() const;

private:
  Derived* _p;
};


template<typename Derived, typename T>
RefCounter::Ptr<Derived, T>::Ptr()
  :
    _p{nullptr}
{}


template<typename Derived, typename T>
RefCounter::Ptr<Derived, T>::Ptr(Derived* p)
  :
    _p{p}
{
  addRefecence(_p);
}

template<typename Derived, typename T>
RefCounter::Ptr<Derived, T>::Ptr(Ptr const& other)
  :
    _p{other._p}
{
  addRefecence(_p);
}

template<typename Derived, typename T>
RefCounter::Ptr<Derived, T>::Ptr(Ptr&& other)
  :
    _p{other._p}
{
  other._p = nullptr;
}

template<typename Derived, typename T>
RefCounter::Ptr<Derived, T>& RefCounter::Ptr<Derived, T>::operator= (const Ptr<Derived, T>& other) {
  reset(other._p);
  return *this;
}


template<typename Derived, typename T>
RefCounter::Ptr<Derived, T>& RefCounter::Ptr<Derived, T>::operator=(Ptr<Derived, T>&& other) {
    _p = other._p;
    other._p = nullptr;
    return *this;
}

template<typename Derived, typename T>
RefCounter::Ptr<Derived, T>::~Ptr() {
  releaseRefecence(_p);
}

template<typename Derived, typename T>
void RefCounter::Ptr<Derived, T>::reset() {
  releaseRefecence(_p);
  _p = nullptr;
}

template<typename Derived, typename T>
void RefCounter::Ptr<Derived, T>::reset(Derived* p) {
  releaseRefecence(_p);
  _p = p;
  addRefecence(_p);
}

template<typename Derived, typename T>
void RefCounter::Ptr<Derived, T>::adopt(Derived* p) {
  releaseRefecence(_p);
  _p = p;
}

template<typename Derived, typename T>
Derived* RefCounter::Ptr<Derived, T>::get() {
  return _p;
}

template<typename Derived, typename T>
Derived* RefCounter::Ptr<Derived, T>::detach() {
  Derived* p = _p;
  _p = nullptr;
  return p;
}

template<typename Derived, typename T>
Derived& RefCounter::Ptr<Derived, T>::operator* () const {
  CHECK_NOT_NULL(_p);
  return *_p;
}

template<typename Derived, typename T>
Derived* RefCounter::Ptr<Derived, T>::operator-> () const {
  CHECK_NOT_NULL(_p);
  return _p;
}

template<typename Derived, typename T>
RefCounter::Ptr<Derived, T>::operator bool () const {
    return _p != nullptr;
}


/* Free functions */


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
