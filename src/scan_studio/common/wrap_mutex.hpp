#pragma once

#include <mutex>

#include "scan_studio/common/common_defines.hpp"

namespace scan_studio {
using namespace vis;

template <typename T, typename MutexT = std::mutex>
class WrapMutex;

/// Provides access to the object inside a WrapMutex.
/// There can always be only a single LockedWrapMutex at a time for each WrapMutex.
template <typename T, typename MutexT = std::mutex>
class LockedWrapMutex {
 friend class WrapMutex<T, MutexT>;
 public:
  /// Having a default constructor is useful for cases where a lock shall only be acquired under a condition, for example like that:
  ///
  /// LockedWrapMutex<Object> conditionalLock;
  /// if (condition) {
  ///   conditionalLock = std::move(wrapMutex.Lock());
  /// }
  inline LockedWrapMutex() {}
  
  LockedWrapMutex(const LockedWrapMutex& other) = delete;
  LockedWrapMutex& operator= (const LockedWrapMutex& other) = delete;
  
  inline LockedWrapMutex(LockedWrapMutex&& other)
      : lock(move(other.lock)),
        wrapMutex(other.wrapMutex) {
    other.wrapMutex = nullptr;
  }
  
  inline LockedWrapMutex& operator= (LockedWrapMutex&& other) {
    lock = move(other.lock);
    wrapMutex = other.wrapMutex;
    return *this;
  }
  
  /// Returns the lock object directly such that it can be used for a condition_variable wait, for example.
  inline unique_lock<MutexT>& GetLock() { return lock; }
  
  inline T& operator* () {
    return wrapMutex->object;
  }
  inline const T& operator* () const {
    return wrapMutex->object;
  }
  
  inline T* operator-> () {
    return &wrapMutex->object;
  }
  inline const T* operator-> () const {
    return &wrapMutex->object;
  }
  
 private:
  inline LockedWrapMutex(WrapMutex<T, MutexT>* wrapMutex)
      : lock(wrapMutex->m),
        wrapMutex(wrapMutex) {}
  
  unique_lock<MutexT> lock;
  WrapMutex<T, MutexT>* wrapMutex;
};

/// Const version of LockedWrapMutex.
template <typename T, typename MutexT = std::mutex>
class ConstLockedWrapMutex {
 friend class WrapMutex<T, MutexT>;
 public:
  inline ConstLockedWrapMutex() {}
  
  ConstLockedWrapMutex(const ConstLockedWrapMutex& other) = delete;
  ConstLockedWrapMutex& operator= (const ConstLockedWrapMutex& other) = delete;
  
  inline ConstLockedWrapMutex(ConstLockedWrapMutex&& other)
      : lock(move(other.lock)),
        wrapMutex(other.wrapMutex) {
    other.wrapMutex = nullptr;
  }
  
  inline ConstLockedWrapMutex& operator= (ConstLockedWrapMutex&& other) {
    lock = move(other.lock);
    wrapMutex = other.wrapMutex;
    return *this;
  }
  
  /// Returns the lock object directly such that it can be used for a condition_variable wait, for example.
  inline unique_lock<MutexT>& GetLock() { return lock; }
  
  inline const T& operator* () const {
    return wrapMutex->object;
  }
  
  inline const T* operator-> () const {
    return &wrapMutex->object;
  }
  
 private:
  inline ConstLockedWrapMutex(const WrapMutex<T, MutexT>* wrapMutex)
      : lock(wrapMutex->m),
        wrapMutex(wrapMutex) {}
  
  unique_lock<MutexT> lock;
  const WrapMutex<T, MutexT>* wrapMutex;
};

/// Wraps a mutex around an object of type T.
/// This makes the T object only accessible through this WrapMutex,
/// ensuring that the mutex is always held for each access to the object (and never forgotten).
template <typename T, typename MutexT>
class WrapMutex {
 friend class LockedWrapMutex<T, MutexT>;
 friend class ConstLockedWrapMutex<T, MutexT>;
 public:
  template <typename ...Args>
  inline WrapMutex(Args&& ...args)
      : object(std::forward<Args>(args)...) {}
  
  inline LockedWrapMutex<T, MutexT> Lock() {
    return LockedWrapMutex<T, MutexT>(this);
  }
  
  inline ConstLockedWrapMutex<T, MutexT> Lock() const {
    return ConstLockedWrapMutex<T, MutexT>(this);
  }
  
  /// Returns the mutex to be used directly if needed for synchronization;
  /// does not grant access to the contained object.
  inline MutexT& Mutex() {
    return m;
  }
  inline const MutexT& Mutex() const {
    return m;
  }
  
 private:
  T object;
  mutable MutexT m;
};

}
