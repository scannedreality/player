#ifdef HAVE_OPENXR

#include "scan_studio/viewer_common/openxr/action_set.hpp"

#include <loguru.hpp>

#include "scan_studio/viewer_common/util.hpp"

namespace scan_studio {

OpenXRActionSet::OpenXRActionSet() {}

OpenXRActionSet::OpenXRActionSet(OpenXRActionSet&& other)
    : actionSet(other.actionSet) {
  other.actionSet = XR_NULL_HANDLE;
}

OpenXRActionSet& OpenXRActionSet::operator=(OpenXRActionSet&& other) {
  Destroy();
  
  actionSet = other.actionSet;
  
  other.actionSet = XR_NULL_HANDLE;
  return *this;
}

OpenXRActionSet::~OpenXRActionSet() {
  Destroy();
}

bool OpenXRActionSet::Initialize(const char* name, const char* localizedName, XrInstance instance) {
  XrActionSetCreateInfo actionsetInfo = {XR_TYPE_ACTION_SET_CREATE_INFO};
  SafeStringCopy(actionsetInfo.actionSetName, XR_MAX_ACTION_SET_NAME_SIZE, name);
  SafeStringCopy(actionsetInfo.localizedActionSetName, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE, localizedName);
  if (!XrCheckResult(xrCreateActionSet(instance, &actionsetInfo, &actionSet))) {
    LOG(ERROR) << "xrCreateActionSet() failed";
    return false;
  }
  return true;
}

void OpenXRActionSet::Destroy() {
  if (actionSet != XR_NULL_HANDLE) {
    xrDestroyActionSet(actionSet);
    actionSet = XR_NULL_HANDLE;
  }
}

XrAction OpenXRActionSet::CreateAction(XrActionType type, const char* name, const char* localizedName, vector<XrPath>* subactionPaths) {
  XrActionCreateInfo actionInfo = {XR_TYPE_ACTION_CREATE_INFO};
  
  if (subactionPaths && !subactionPaths->empty()) {
    actionInfo.countSubactionPaths = subactionPaths->size();
    actionInfo.subactionPaths = subactionPaths->data();
  }
  
  actionInfo.actionType = type;
  SafeStringCopy(actionInfo.actionName, XR_MAX_ACTION_NAME_SIZE, name);
  SafeStringCopy(actionInfo.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, localizedName);
  
  XrAction action;
  if (!XrCheckResult(xrCreateAction(actionSet, &actionInfo, &action))) {
    return XR_NULL_HANDLE;
  } else {
    return action;
  }
}

}

#endif
