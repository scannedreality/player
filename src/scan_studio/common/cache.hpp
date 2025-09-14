#pragma once

#include <mutex>
#include <vector>

#include "scan_studio/common/common_defines.hpp"

namespace scan_studio {
using namespace vis;

/// Generic thread-safe class to cache objects for re-use that are expensive to allocate.
/// For example, a typical use-case is to cache memory buffers.
///
/// Note that the class passed in for `ObjectT` may store a reference/pointer to the `Cache`
/// object to simplify putting it back correctly, even if it had been passed around during its use.
template <typename ObjectT>
class Cache {
 public:
  /// If an object is cached, takes it out of the cache and returns it.
  /// Otherwise, returns a newly allocated object.
  ///
  /// Notice that if you use the variadic args to customize the object's allocation,
  /// then all calls to this function should use the same args such that the objects
  /// in the cache are actually re-usable.
  template <typename ...Args>
  inline ObjectT TakeOrAllocate(Args&& ...args) {
    lock_guard<mutex> cacheLock(cacheMutex);
    
    if (!cachedObjects.empty()) {
      ObjectT result = move(cachedObjects.back());
      cachedObjects.pop_back();
      return result;
    }
    
    return ObjectT(std::forward<Args>(args)...);
  }
  
  /// Returns an object into the cache.
  inline void PutBack(ObjectT&& object) {
    lock_guard<mutex> cacheLock(cacheMutex);
    
    cachedObjects.push_back(move(object));
  }
  
  /// Clears the cache.
  inline void Clear() {
    lock_guard<mutex> cacheLock(cacheMutex);
    
    cachedObjects = vector<ObjectT>();
  }
  
 private:
  mutex cacheMutex;
  vector<ObjectT> cachedObjects;
};

}
