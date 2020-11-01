/*
 * ODANT jscript, ExecuterCounter
*/


#pragma once


#include "node_mutex.h"


namespace node {
namespace jscript {


class ExecutorCounter {
public:
  ExecutorCounter();

  static ExecutorCounter& global();
  void waitAllStop();

  class ScopeExecute;

private:
  std::size_t _count;

  node::Mutex _mutex;
  node::ConditionVariable _cv;
};


inline ExecutorCounter::ExecutorCounter()
  :
    _count{0}
{}

inline ExecutorCounter& ExecutorCounter::global() {
  static ExecutorCounter executorCounter{};
  return executorCounter;
}

inline void ExecutorCounter::waitAllStop() {
  node::Mutex::ScopedLock lock{_mutex};
  while (_count != 0) {
    _cv.Wait(lock);
  }
}


class ExecutorCounter::ScopeExecute {
public:
  ScopeExecute();
  ~ScopeExecute();
};

inline ExecutorCounter::ScopeExecute::ScopeExecute() {
  ExecutorCounter& executorCounter = ExecutorCounter::global();
  node::Mutex::ScopedLock lock{executorCounter._mutex};
  ++(executorCounter._count);
}

inline ExecutorCounter::ScopeExecute::~ScopeExecute() {
  ExecutorCounter& executorCounter = ExecutorCounter::global();
  node::Mutex::ScopedLock lock{executorCounter._mutex};
  --(executorCounter._count);
  executorCounter._cv.Broadcast(lock);
}


} // namespace node
} // namespace jscript
