#include "scan_studio/viewer_common/xrvideo/external/external_xrvideo_frame.hpp"

#include <loguru.hpp>

#include "scan_studio/viewer_common/xrvideo/external/external_xrvideo.hpp"

namespace scan_studio {

ExternalXRVideoFrame::~ExternalXRVideoFrame() {
  Destroy();
}

void ExternalXRVideoFrame::Configure(const ExternalXRVideo* xrVideo) {
  this->xrVideo = xrVideo;
  
  frameUserData = xrVideo->callbacks.constructFrameCallback(xrVideo->callbacks.videoUserData);
}

bool ExternalXRVideoFrame::Initialize(
    const XRVideoFrameMetadata& metadata,
    const u8* contentPtr,
    TextureFramePromise* textureFramePromise,
    XRVideoDecodingContext* decodingContext,
    bool verboseDecoding) {
  this->metadata = metadata;
  
  // Convert the metadata from the internal format into the public struct used by our C API
  frameMetadataForAPI.startTimestampNanoseconds = metadata.startTimestamp;
  frameMetadataForAPI.endTimestampNanoseconds = metadata.endTimestamp;
  frameMetadataForAPI.isKeyframe = metadata.isKeyframe;
  frameMetadataForAPI.textureWidth = metadata.textureWidth;
  frameMetadataForAPI.textureHeight = metadata.textureHeight;
  frameMetadataForAPI.uniqueVertexCount = metadata.uniqueVertexCount;
  frameMetadataForAPI.renderableVertexDataSize = metadata.GetRenderableVertexDataSize();
  frameMetadataForAPI.indexDataSize = metadata.GetIndexDataSize();
  frameMetadataForAPI.deformationDataSize = metadata.GetDeformationStateDataSize();
  frameMetadataForAPI.bboxMinX = metadata.bboxMinX;
  frameMetadataForAPI.bboxMinY = metadata.bboxMinY;
  frameMetadataForAPI.bboxMinZ = metadata.bboxMinZ;
  frameMetadataForAPI.vertexFactorX = metadata.vertexFactorX;
  frameMetadataForAPI.vertexFactorY = metadata.vertexFactorY;
  frameMetadataForAPI.vertexFactorZ = metadata.vertexFactorZ;
  
  // Prepare-decode callback
  void* verticesPtr = nullptr;
  void* indicesPtr = nullptr;
  void* deformationPtr = nullptr;
  void* texturePtr = nullptr;
  void* duplicatedVertexSourceIndicesPtr = nullptr;
  
  if (!xrVideo->callbacks.decodingThread_prepareDecodeFrameCallback(
      xrVideo->callbacks.videoUserData,
      frameUserData,
      &frameMetadataForAPI,
      &verticesPtr,
      &indicesPtr,
      &deformationPtr,
      &texturePtr,
      &duplicatedVertexSourceIndicesPtr)) {
    return false;
  }
  
  // Verify that the prepare-decode callback has set the required output pointers
  if (metadata.isKeyframe && (!verticesPtr || !indicesPtr)) {
    LOG(ERROR) << "The XRVideo prepare-frame callback must provide both a vertices and an indices pointer for keyframes. verticesPtr: " << verticesPtr << ", indicesPtr: " << indicesPtr;
    return false;
  }
  if (!deformationPtr || !texturePtr) {
    LOG(ERROR) << "The XRVideo prepare-frame callback must provide deformation and texture pointers for each frame. deformationPtr: " << deformationPtr << ", texturePtr: " << texturePtr;
    return false;
  }
  
  // Decompress the frame data
  vector<u8> vertexAlpha;
  
  if (!XRVideoDecompressContent(
      contentPtr, metadata, decodingContext,
      verticesPtr,
      static_cast<u16*>(indicesPtr),
      static_cast<float*>(deformationPtr),
      static_cast<u16*>(duplicatedVertexSourceIndicesPtr),
      &vertexAlpha,
      verboseDecoding)) {
    LOG(ERROR) << "Failed to decompress XRVideo content";
    return false;
  }
  
  if (!textureFramePromise->Wait()) {
    return false;
  }
  auto textureData = textureFramePromise->Take();
  // TODO: Try to use zero-copy to improve performance (unless the different allocation slows down decoding more than the removal of the copy helps)
  if (textureData) {
    XRVideoCopyTexture(*textureData, static_cast<u8*>(texturePtr), verboseDecoding);
  } else {
    memset(texturePtr, 0, (3 * metadata.textureWidth * metadata.textureHeight) / 2);
  }
  textureData.reset();
  
  // After-decode callback
  if (!xrVideo->callbacks.decodingThread_afterDecodeFrameCallback(
      xrVideo->callbacks.videoUserData,
      frameUserData,
      &frameMetadataForAPI,
      vertexAlpha.size(),
      vertexAlpha.data())) {
    return false;
  }
  
  return true;
}

void ExternalXRVideoFrame::Destroy() {
  xrVideo->callbacks.destructFrameCallback(xrVideo->callbacks.videoUserData, frameUserData);
}

void ExternalXRVideoFrame::WaitForResourceTransfers() {
  xrVideo->callbacks.transferThread_transferFrameCallback(xrVideo->callbacks.videoUserData, frameUserData, &frameMetadataForAPI);
}

}
