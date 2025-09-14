#pragma once
#ifdef HAVE_OPENXR

#include <vector>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/openxr/openxr.hpp"

namespace scan_studio {
using namespace vis;

class OpenXRActionSet {
 public:
  /// This does not initialize the object yet. Initialize() must be called for that.
  OpenXRActionSet();
  
  OpenXRActionSet(const OpenXRActionSet& other) = delete;
  OpenXRActionSet& operator= (const OpenXRActionSet& other) = delete;
  
  OpenXRActionSet(OpenXRActionSet&& other);
  OpenXRActionSet& operator= (OpenXRActionSet&& other);
  
  /// Destroys the action set.
  ~OpenXRActionSet();
  
  /// Attempts to initialize the OpenXR action set.
  /// Returns true if successful, false otherwise.
  bool Initialize(const char* name, const char* localizedName, XrInstance instance);
  void Destroy();
  
  /// Creates an action within this action set. Returns the action on success, or XR_NULL_HANDLE on failure.
  /// Note that there is no need to call xrDestroyAction() on the result manually, since actions that are part of an
  /// action set are automatically destroyed when the action set’s handle is destroyed.
  XrAction CreateAction(XrActionType type, const char* name, const char* localizedName, vector<XrPath>* subactionPaths = nullptr);
  
  /// Returns the underlying XrActionSet handle.
  inline const XrActionSet& GetSession() const { return actionSet; }
  inline operator XrActionSet() const { return actionSet; }
  
 private:
  XrActionSet actionSet = XR_NULL_HANDLE;
};

}

#endif
