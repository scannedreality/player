#include "scan_studio/viewer_common/render_state.hpp"

#include "scan_studio/viewer_common/openxr/openxr_vulkan_application.hpp"

namespace scan_studio {

#ifdef HAVE_OPENXR
VulkanRenderState::VulkanRenderState(VulkanCommandBuffer* cmdBuf, OpenXRVulkanApplication* app)
    : RenderState(RenderState::RenderingAPI::Vulkan),
      device(&app->VKDevice()),
      renderPass(&app->RenderPass()),
      framesInFlightCount(app->MaxFrameInFlightCount()),
      cmdBuf(cmdBuf),
      frameInFlightIndex(app->CurrentFrameInFlightIndex()) {}
#endif

}
