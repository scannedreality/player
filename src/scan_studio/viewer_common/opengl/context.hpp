#pragma once

#include <libvis/vulkan/libvis.h>

namespace scan_studio {
using namespace vis;

/// Abstracts a few functions of OpenGL contexts with the purpose of allowing worker threads
/// to make an OpenGL context current without having to know the required specific platform / library
/// details to interact with OpenGL contexts (e.g., SDL, Android, etc.)
class GLContext {
 public:
  inline virtual ~GLContext() {}
  
  /// Makes this class represent the context that is current to the thread which executes this function.
  /// Note that this will result in this context getting deleted once this GLContext-derived object gets destructed
  /// To avoid that, call Detach() before it is destructed.
  virtual void AttachToCurrent() = 0;
  
  /// Detaches the context from this object, meaning that when the object gets destructed, it will not delete the context.
  virtual void Detach() = 0;
  
  /// Deletes the context.
  virtual void Destroy() = 0;
  
  /// Makes this context current in the current thread.
  /// Returns true on success, false on failure.
  virtual bool MakeCurrent() = 0;
};

}
