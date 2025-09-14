#include "scan_studio/viewer_common/xrvideo/opengl/opengl_xrvideo_frame.hpp"
#ifdef HAVE_OPENGL

#include <chrono>
#include <thread>
#include <vector>

#include <zstd.h>

#include <loguru.hpp>

#include <libvis/util/util.h>

#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"
#include "scan_studio/viewer_common/xrvideo/opengl/opengl_xrvideo.hpp"
#include "scan_studio/viewer_common/xrvideo/opengl/opengl_xrvideo_shader.hpp"
#include "scan_studio/viewer_common/opengl/extensions.hpp"
#include "scan_studio/viewer_common/opengl/loader.hpp"
#include "scan_studio/viewer_common/opengl/util.hpp"
#include "scan_studio/viewer_common/debug.hpp"
#include "scan_studio/viewer_common/timing.hpp"

namespace scan_studio {

OpenGLXRVideoFrame::OpenGLXRVideoFrame() {}

OpenGLXRVideoFrame::~OpenGLXRVideoFrame() {
  Destroy();
}

void OpenGLXRVideoFrame::Configure(OpenGLXRVideo* video) {
  this->video = video;
}

void OpenGLXRVideoFrame::UseExternalBuffers(GLuint vertexBuffer, GLuint indexBuffer, GLuint alphaBuffer) {
  this->vertexBuffer.Wrap(GL_ARRAY_BUFFER, vertexBuffer);
  this->indexBuffer.Wrap(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
  this->vertexAlphaBuffer.Wrap(GL_ARRAY_BUFFER, alphaBuffer);
  
  useExternalBuffers = true;
}

#ifndef __EMSCRIPTEN__
bool OpenGLXRVideoFrame::InitializeTextures(u32 textureWidth, u32 textureHeight, void* lumaData, void* chromaUData, void* chromaVData) {
  // Luma:
  if (!textureLuma.Allocate2D(
      textureWidth, textureHeight,
      /*GL_R8*/ 0x8229, /*GL_RED*/ 0x1903, GL_UNSIGNED_BYTE,
      GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE,
      GL_NEAREST, GL_NEAREST,  // we perform the bilinear interpolation ourself in the shader
      lumaData)) {
    return false;
  }
  
  // Chroma:
  for (GLTexture* texture : {&textureChromaU, &textureChromaV}) {
    if (!texture->Allocate2D(
        textureWidth / 2, textureHeight / 2,
        /*GL_R8*/ 0x8229, /*GL_RED*/ 0x1903, GL_UNSIGNED_BYTE,
        GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE,
        GL_NEAREST, GL_NEAREST,
        (texture == &textureChromaU) ? chromaUData : chromaVData)) {
      return false;
    }
  }
  
  return true;
}
#endif

// We use different code paths for emscripten and non-emscripten here.
//
// This is because with emscripten, we have to proxy all WebGL calls to the main thread
// using functions such as emscripten_sync_run_in_main_runtime_thread() (or its async variants).
// Such scheduled commands may take some time to complete if the main thread is busy doing other
// things (e.g., rendering, or texture transfer). Thus, those commands may have a large latency
// to execute, even if they themselves do very little.
//
// - So, we should avoid having to wait for any such calls in the same thread that calls
//   XRVideoDecompressContent() (which does the expensive video texture decoding).
// - However, the 'standard' code path benefits from mapping OpenGL buffers and having
//   XRVideoDecompressContent() directly decompress into the mapped memory (in contrast,
//   glMapBufferRange() does not even exist in WebGL).
//
// Both code paths are performance-sensitive (the web path for slow mobile devices, the
// 'standard' path for Meta Quest 2).
#ifdef __EMSCRIPTEN__
bool OpenGLXRVideoFrame::Initialize(
    const XRVideoFrameMetadata& metadata,
    const u8* contentPtr,
    TextureFramePromise* textureFramePromise,
    XRVideoDecodingContext* decodingContext,
    bool verboseDecoding) {
  this->verboseDecoding = verboseDecoding;
  if (verboseDecoding) {
    LOG(1) << "OpenGLXRVideoFrame: Initialize() ...";
  }
  
  TimePoint frameLoadingStartTime;
  if (verboseDecoding) {
    frameLoadingStartTime = Clock::now();
  }
  
  this->metadata = metadata;
  
  // glMapBufferRange() does not exist in WebGL, so we use a staging buffer in CPU memory
  if (metadata.isKeyframe) {
    vertexStagingBuffer.resize(metadata.GetRenderableVertexDataSize());
    vertexBufferPtr = vertexStagingBuffer.data();
    
    indexStagingBuffer.resize(metadata.GetIndexDataSize());
    indexBufferPtr = reinterpret_cast<u16*>(indexStagingBuffer.data());
  }
  
  // Note: See the call to ChooseTextureSizeForTexelCount() in OpenGLXRVideo::InitializeImpl() for why we simply assume 2048 for maxTextureSize.
  ChooseTextureSizeForTexelCount(
      /*minTexelCount*/ metadata.GetDeformationStateDataSize() / (4 * sizeof(float)), /*minTextureWidth*/ 64, /*maxTextureSize*/ 2048, &deformationStateTextureWidth, &deformationStateTextureHeight);
  if (deformationStateTextureWidth == numeric_limits<u32>::max()) {
    LOG(ERROR) << "No suitable texture size found for deformationStateTexture";
    // TODO: Handle the failure
  }
  if (verboseDecoding) {
    LOG(1) << "Size chosen for deformationStateTexture is " << deformationStateTextureWidth << " x " << deformationStateTextureHeight;
  }
  
  // Decompress the frame data.
  // Note that since the deformation texture size may be slightly larger than required (due to the extra pixels to make it rectangular),
  // we have to allocate deformationState with that larger size, otherwise we'd later read beyond this allocation when transferring the texture data.
  deformationState.resize(deformationStateTextureWidth * deformationStateTextureHeight * 4 * sizeof(float));
  
  textureData.reset();
  
  if (!XRVideoDecompressContent(
      contentPtr, metadata, decodingContext,
      vertexBufferPtr, indexBufferPtr,
      deformationState.data(), nullptr, &vertexAlpha, verboseDecoding)) {
    LOG(ERROR) << "Decompressing XRVideo content failed";
    Destroy(); return false;
  }
  
  if (!textureFramePromise->Wait()) {
    Destroy(); return false;
  }
  textureData = textureFramePromise->Take();
  
  // Start the resource transfers on the main thread.
  // In WebGL, all OpenGL function calls are proxied to the main thread, where they use a single WebGL context (because WebGL does not support resource sharing between contexts).
  // This means that if we try to render an OpenGLXRVideoFrame's texture, which happens sometime after this call to WaitForResourceTransfers(),
  // then its resource loading calls will have been done in the same WebGL context beforehand. This means that no explicit synchronization is required.
  transferCall = emscripten_async_waitable_run_in_main_runtime_thread(EM_FUNC_SIG_II, &OpenGLXRVideoFrame::WaitForResourceTransfers_MainThreadStatic, reinterpret_cast<int>(this));
  
  if (verboseDecoding) {
    const TimePoint frameLoadingEndTime = Clock::now();
    LOG(1) << "OpenGLXRVideoFrame: Frame loading time without I/O, with texture wait: " << (MillisecondsDuration(frameLoadingEndTime - frameLoadingStartTime).count()) << " ms";
  }
  isInitialized = true;
  
  return true;
}
#else
bool OpenGLXRVideoFrame::Initialize(
    const XRVideoFrameMetadata& metadata,
    const u8* contentPtr,
    TextureFramePromise* textureFramePromise,
    XRVideoDecodingContext* decodingContext,
    bool verboseDecoding) {
  this->verboseDecoding = verboseDecoding;
  if (verboseDecoding) {
    LOG(1) << "OpenGLXRVideoFrame: Initialize() ...";
  }
  
  TimePoint frameLoadingStartTime;
  if (verboseDecoding) {
    frameLoadingStartTime = Clock::now();
  }
  
  this->metadata = metadata;
  
  CHECK_OPENGL_NO_ERROR();
  
  // For frame re-use, destroy some OpenGL objects
  if (isInitialized) {
    CHECK_OPENGL_NO_ERROR();
    
    gl.glDeleteSync(initializeCompleteFence);
    initializeCompleteFence = nullptr;
    gl.glDeleteSync(transferCompleteFence);
    transferCompleteFence = nullptr;
    
    isInitialized = false;
    CHECK_OPENGL_NO_ERROR();
  }
  
  // Allocate and map vertex and index buffer
  vertexBufferPtr = nullptr;
  indexBufferPtr = nullptr;
  
  const u32 vertexCount = metadata.isKeyframe ? metadata.GetRenderableVertexCount() : 0;
  if (vertexCount > 0) {
    if (useExternalBuffers) {
      if (metadata.GetRenderableVertexDataSize() > vertexBuffer.Size()) {
        // Note: Here in the OpenGL render path, we could easily resize the buffer without changing its OpenGL buffer 'name' in this situation.
        //       However, in other render paths, that is not possible. Thus, we prefer to abort here and tell the developer about
        //       the issue so they can fix it, to prevent surprises with the program working with OpenGL but failing with other render paths.
        LOG(ERROR) << "External vertex buffer is too small. Available bytes: " << vertexBuffer.Size() << ". Required bytes: " << metadata.GetRenderableVertexDataSize();
        Destroy(); return false;
      }
      gl.glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer.BufferName());
    } else {
      vertexBuffer.Allocate(metadata.GetRenderableVertexDataSize(), GL_ARRAY_BUFFER, GL_STATIC_DRAW);
    }
    
    vertexBufferPtr = gl.glMapBufferRange(
        GL_ARRAY_BUFFER, /*offset*/ 0, /*length*/ metadata.GetRenderableVertexDataSize(),
        GL_MAP_WRITE_BIT | /*GL_MAP_INVALIDATE_BUFFER_BIT*/ 0x0008 | /*GL_MAP_UNSYNCHRONIZED_BIT*/ 0x0020);
    if (vertexBufferPtr == nullptr) {
      LOG(ERROR) << "Failed to map the vertex buffer";
      CHECK_OPENGL_NO_ERROR();
      Destroy(); return false;
    }
  }
  
  if (metadata.isKeyframe && metadata.indexCount > 0) {
    if (useExternalBuffers) {
      if (metadata.GetIndexDataSize() > indexBuffer.Size()) {
        // Note: Here in the OpenGL render path, we could easily resize the buffer without changing its OpenGL buffer 'name' in this situation.
        //       However, in other render paths, that is not possible. Thus, we prefer to abort here and tell the developer about
        //       the issue so they can fix it, to prevent surprises with the program working with OpenGL but failing with other render paths.
        LOG(ERROR) << "External index buffer is too small. Available bytes: " << indexBuffer.Size() << ". Required bytes: " << metadata.GetIndexDataSize();
        Destroy(); return false;
      }
      gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer.BufferName());
    } else {
      indexBuffer.Allocate(metadata.GetIndexDataSize(), GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW);
    }
    
    indexBufferPtr = static_cast<u16*>(gl.glMapBufferRange(
        GL_ELEMENT_ARRAY_BUFFER, /*offset*/ 0, /*length*/ metadata.GetIndexDataSize(),
        GL_MAP_WRITE_BIT | /*GL_MAP_INVALIDATE_BUFFER_BIT*/ 0x0008 | /*GL_MAP_UNSYNCHRONIZED_BIT*/ 0x0020));
    if (indexBufferPtr == nullptr) {
      LOG(ERROR) << "Failed to map the index buffer";
      CHECK_OPENGL_NO_ERROR();
      Destroy(); return false;
    }
  }
  
  // Note: See the call to ChooseTextureSizeForTexelCount() in OpenGLXRVideo::InitializeImpl() for why we simply assume 2048 for maxTextureSize.
  ChooseTextureSizeForTexelCount(
      /*minTexelCount*/ metadata.GetDeformationStateDataSize() / (4 * sizeof(float)), /*minTextureWidth*/ 64, /*maxTextureSize*/ 2048, &deformationStateTextureWidth, &deformationStateTextureHeight);
  if (deformationStateTextureWidth == numeric_limits<u32>::max()) { LOG(ERROR) << "No suitable texture size found for deformationStateTexture"; Destroy(); return false; }
  
  // Decompress the frame data.
  // Note that since the deformation texture size may be slightly larger than required (due to the extra pixels to make it rectangular),
  // we have to allocate deformationState with that larger size, otherwise we'd later read beyond this allocation when transferring the texture data.
  deformationState.resize(deformationStateTextureWidth * deformationStateTextureHeight * 4 * sizeof(float));
  
  textureData.reset();
  
  if (!XRVideoDecompressContent(
      contentPtr, metadata, decodingContext,
      vertexBufferPtr, indexBufferPtr,
      deformationState.data(), nullptr, &vertexAlpha, verboseDecoding)) {
    LOG(ERROR) << "Decompressing XRVideo content failed";
    Destroy(); return false;
  }
  
  CHECK_OPENGL_NO_ERROR();
  
  // Unmap the vertex and index buffers
  if (vertexCount > 0) {
    if (gl.glUnmapBuffer(GL_ARRAY_BUFFER) != GL_TRUE) { LOG(ERROR) << "Vertex buffer data store contents have become corrupt"; Destroy(); return false; }
  }
  if (metadata.isKeyframe && metadata.indexCount > 0) {
    if (gl.glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER) != GL_TRUE) { LOG(ERROR) << "Index buffer data store contents have become corrupt"; Destroy(); return false; }
  }
  CHECK_OPENGL_NO_ERROR();
  
  if (!textureFramePromise->Wait()) {
    Destroy(); return false;
  }
  textureData = textureFramePromise->Take();
  
  initializeCompleteFence = gl.glFenceSync(/*GL_SYNC_GPU_COMMANDS_COMPLETE*/ 0x9117, /*flags*/ 0);
  // Since we are going to wait for initializeCompleteFence in another OpenGL context, we have to flush the current context here
  // to make sure that it will be signaled by the GPU.
  gl.glFlush();
  
  if (verboseDecoding) {
    const TimePoint frameLoadingEndTime = Clock::now();
    LOG(1) << "OpenGLXRVideoFrame: Complete frame initialization time (without I/O): " << (MillisecondsDuration(frameLoadingEndTime - frameLoadingStartTime).count()) << " ms";
  }
  isInitialized = true;
  
  return true;
}
#endif

void OpenGLXRVideoFrame::Destroy() {
  textureData.reset();
  
  if (isInitialized) {
    CHECK_OPENGL_NO_ERROR();
    deformationStateTexture.Destroy();
    vertexAlphaBuffer.Destroy();
    #ifndef __EMSCRIPTEN__
      gl.glDeleteSync(initializeCompleteFence);
      initializeCompleteFence = nullptr;
      gl.glDeleteSync(transferCompleteFence);
      transferCompleteFence = nullptr;
    #endif
    vertexBuffer.Destroy();
    indexBuffer.Destroy();
    #ifdef __EMSCRIPTEN__
      texture.Destroy();
    #else
      textureLuma.Destroy();
      textureChromaU.Destroy();
      textureChromaV.Destroy();
    #endif
    isInitialized = false;
    CHECK_OPENGL_NO_ERROR();
  }
}

void OpenGLXRVideoFrame::Render(
    const float* modelViewDataColumnMajor,
    const float* modelViewProjectionDataColumnMajor,
    const float* modelViewDataColumnMajor2,
    const float* modelViewProjectionDataColumnMajor2,
    GLuint interpolatedDeformationStateTexture,
    OpenGLXRVideoShader* shader,
    const OpenGLXRVideoFrame* lastKeyframe) const {
  if (!isInitialized) {
    LOG(ERROR) << "Trying to render an uninitialized OpenGLXRVideoFrame";
    return;
  }
  
  const OpenGLXRVideoFrame* baseFrame = metadata.isKeyframe ? this : lastKeyframe;
  if (baseFrame->metadata.indexCount == 0) {
    return;
  }
  
  gl.glEnable(GL_DEPTH_TEST);
  if (metadata.hasVertexAlpha) {
    gl.glEnable(GL_BLEND);
    gl.glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    gl.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  }
  
  CHECK_OPENGL_NO_ERROR();
  shader->Use(
      baseFrame->vertexBuffer.BufferName(), vertexAlphaBuffer.BufferName(), metadata.textureWidth, metadata.textureHeight,
      modelViewDataColumnMajor, modelViewProjectionDataColumnMajor,
      modelViewDataColumnMajor2, modelViewProjectionDataColumnMajor2,
      baseFrame->metadata.bboxMinX, baseFrame->metadata.bboxMinY, baseFrame->metadata.bboxMinZ, baseFrame->metadata.vertexFactorX, baseFrame->metadata.vertexFactorY, baseFrame->metadata.vertexFactorZ);
  
  // Set texture
  gl.glActiveTexture(GL_TEXTURE0);
  #ifdef __EMSCRIPTEN__
    gl.glBindTexture(GL_TEXTURE_2D, texture.Name());
  #else
    gl.glBindTexture(GL_TEXTURE_2D, textureLuma.Name());
    gl.glActiveTexture(GL_TEXTURE1);
    gl.glBindTexture(GL_TEXTURE_2D, textureChromaU.Name());
    gl.glActiveTexture(GL_TEXTURE2);
    gl.glBindTexture(GL_TEXTURE_2D, textureChromaV.Name());
  #endif
  gl.glActiveTexture(GL_TEXTURE3);
  gl.glBindTexture(GL_TEXTURE_2D, interpolatedDeformationStateTexture);
  gl.glActiveTexture(GL_TEXTURE0);  // not strictly necessary, just to reset the state
  CHECK_OPENGL_NO_ERROR();
  
  // Draw
  gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, baseFrame->indexBuffer.BufferName());
  gl.glDrawElements(GL_TRIANGLES, baseFrame->metadata.indexCount, GL_UNSIGNED_SHORT, /*indices*/ nullptr);
  CHECK_OPENGL_NO_ERROR();
  
  // Disable textures again.
  // Otherwise, for example in case of our Unity plugin, it may happen that the external part of the application
  // sets properties on the textures while they are still bound, for example, enabling anisotropic filtering, which can cause issues with our rendering.
  gl.glActiveTexture(GL_TEXTURE0);
  gl.glBindTexture(GL_TEXTURE_2D, 0);
  #ifndef __EMSCRIPTEN__
    gl.glActiveTexture(GL_TEXTURE1);
    gl.glBindTexture(GL_TEXTURE_2D, 0);
    gl.glActiveTexture(GL_TEXTURE2);
    gl.glBindTexture(GL_TEXTURE_2D, 0);
  #endif
  gl.glActiveTexture(GL_TEXTURE3);
  gl.glBindTexture(GL_TEXTURE_2D, 0);
  gl.glActiveTexture(GL_TEXTURE0);  // not strictly necessary, just to reset the state
  
  // Disable vertex attributes again.
  // Otherwise, enabled attributes could be left without a buffer bound to them, letting all subsequent draw calls fail.
  shader->DoneUsing();
  
  if (metadata.hasVertexAlpha) {
    gl.glDisable(GL_BLEND);
  }
}

// As for Initialize(), we use different code paths for WaitForResourceTransfers() for emscripten and non-emscripten.
#ifdef __EMSCRIPTEN__
void OpenGLXRVideoFrame::WaitForResourceTransfers() {
  // Wait for the transfers to finish on the main thread
  if (transferCall) {
    int returnValue;
    const EMSCRIPTEN_RESULT waitResult = emscripten_wait_for_call_i(transferCall, /*timeoutMSecs*/ 10 * 1000, &returnValue);
    
    if (waitResult == EMSCRIPTEN_RESULT_SUCCESS) {
      if (returnValue == 0) {
        LOG(ERROR) << "Resource transfers failed";
        // TODO: Handle allocation failure
      }
    } else {
      LOG(WARNING) << "emscripten_wait_for_call_i() returned " << waitResult;
    }
    
    emscripten_async_waitable_close(transferCall);
    transferCall = nullptr;
  } else {
    LOG(ERROR) << "transferCall is null in WaitForResourceTransfers()";
  }
  
  // Free the CPU staging data memory
  vertexStagingBuffer = vector<u8>();
  indexStagingBuffer = vector<u8>();
  deformationState = vector<float>();
  vertexAlpha = vector<u8>();
  textureData.reset();
}

int OpenGLXRVideoFrame::WaitForResourceTransfers_MainThreadStatic(int thisInt) {
  OpenGLXRVideoFrame* thisPtr = reinterpret_cast<OpenGLXRVideoFrame*>(thisInt);
  return thisPtr->WaitForResourceTransfers_MainThread() ? 1 : 0;
}

bool OpenGLXRVideoFrame::WaitForResourceTransfers_MainThread() {
  // NOTE: It is possible that re-allocation of OpenGL resources is faster here than updating existing resources
  //       in case the latter causes some implicit synchronization if the old content is still being used for rendering.
  //       Uncomment the Destroy() calls below to allocate new resources for each frame:
  // deformationStateTexture.Destroy();
  // texture.Destroy();
  // vertexBuffer.Destroy();
  // indexBuffer.Destroy();
  // vertexAlphaBuffer.Destroy();
  
  vector<u8> emptyFrameData;
  if (textureData == nullptr) {
    emptyFrameData.resize((3 * metadata.textureWidth * metadata.textureHeight) / 2, 0);
  }
  
  if (!texture.Allocate2D(
      metadata.textureWidth, (metadata.textureHeight * 3) / 2,
      /*GL_R8*/ 0x8229, /*GL_RED*/ 0x1903, GL_UNSIGNED_BYTE,
      GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE,
      GL_NEAREST, GL_NEAREST,  // we perform the bilinear interpolation ourself in the shader
      textureData ? textureData->data[0] : emptyFrameData.data())) {
    return false;
  }
  
  deformationStateTexture.Allocate2D(
      deformationStateTextureWidth, deformationStateTextureHeight,
      /*GL_RGBA32F*/ 0x8814, GL_RGBA, GL_FLOAT,
      GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE,
      GL_NEAREST, GL_NEAREST,
      deformationState.data());
  
  gl.glBindTexture(GL_TEXTURE_2D, 0);
  
  if (metadata.isKeyframe) {
    vertexBuffer.BufferData(vertexStagingBuffer.data(), vertexStagingBuffer.size(), GL_ARRAY_BUFFER, GL_STATIC_DRAW);
    indexBuffer.BufferData(indexStagingBuffer.data(), indexStagingBuffer.size(), GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW);
    if (vertexAlpha.empty()) { gl.glBindBuffer(GL_ARRAY_BUFFER, 0); }
    gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  } else {
    vertexBuffer.Destroy();
    indexBuffer.Destroy();
  }
  
  if (!vertexAlpha.empty()) {
    vertexAlphaBuffer.BufferData(vertexAlpha.data(), vertexAlpha.size(), GL_ARRAY_BUFFER, GL_STATIC_DRAW);
    gl.glBindBuffer(GL_ARRAY_BUFFER, 0);
  }
  
  return true;
}
#else
void OpenGLXRVideoFrame::WaitForResourceTransfers() {
  // Transfer the deformation state to the GPU as a texture (GLES 3.0 and WebGL 2 do not support shader storage buffer objects)
  if (verboseDecoding) {
    LOG(1) << "Size chosen for deformationStateTexture is " << deformationStateTextureWidth << " x " << deformationStateTextureHeight;
  }
  deformationStateTexture.Allocate2D(
      deformationStateTextureWidth, deformationStateTextureHeight,
      /*GL_RGBA32F*/ 0x8814, GL_RGBA, GL_FLOAT,
      GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE,
      GL_NEAREST, GL_NEAREST,
      deformationState.data());
  deformationState = vector<float>();  // free the CPU memory
  
  // Transfer the texture data to the GPU
  vector<u8> emptyFrameData;
  if (textureData == nullptr) {
    emptyFrameData.resize(metadata.textureWidth * metadata.textureHeight, 0);
  }
  
  if (!InitializeTextures(
      metadata.textureWidth, metadata.textureHeight,
      textureData ? textureData->data[0] : emptyFrameData.data(),
      textureData ? textureData->data[1] : emptyFrameData.data(),
      textureData ? textureData->data[2] : emptyFrameData.data())) {
    // TODO: Handle allocation failure
  }
  
  // Free the CPU texture memory
  textureData.reset();
  
  // Transfer the vertex alpha to the GPU (if present)
  // TODO: If we knew the size of the vertex alpha buffer from the metadata, we could handle this buffer
  //       transfer like the vertex and index buffer transfers, mapping and unmapping the buffer during Initialize().
  if (!vertexAlpha.empty()) {
    vertexAlphaBuffer.BufferData(vertexAlpha.data(), vertexAlpha.size(), GL_ARRAY_BUFFER, GL_STATIC_DRAW);
    vertexAlpha = vector<u8>();  // free the CPU memory
  }
  
  // If we load XRVideo frames in a background thread that uses a different OpenGL context
  // than the foreground rendering thread, then we have to synchronize the GL operations manually
  // to ensure that e.g., the textures actually got uploaded before we use them for rendering.
  // Without this synchronization, I observed textures being incorrectly rendered as black (all-zero).
  transferCompleteFence = gl.glFenceSync(/*GL_SYNC_GPU_COMMANDS_COMPLETE*/ 0x9117, /*flags*/ 0);
  
  constexpr GLuint64 waitTimeoutNanoseconds = 3ull * 1000ull * 1000ull * 1000ull;  // 3 seconds
  for (GLsync* fence : {&initializeCompleteFence, &transferCompleteFence}) {
    const GLenum waitResult = gl.glClientWaitSync(*fence, /*GL_SYNC_FLUSH_COMMANDS_BIT*/ 0x00000001, waitTimeoutNanoseconds);
    if (waitResult == /*GL_TIMEOUT_EXPIRED*/ 0x911B) {
      LOG(WARNING) << "OpenGLXRVideoFrame::WaitForResourceTransfers(): Got GL_TIMEOUT_EXPIRED from glClientWaitSync("
                   << ((fence == &initializeCompleteFence) ? "initializeCompleteFence" : "transferCompleteFence") << ": "
                   << ((fence == &initializeCompleteFence) ? initializeCompleteFence : transferCompleteFence) << ")";
    } else if (waitResult == /*GL_WAIT_FAILED*/ 0x911D) {
      LOG(ERROR) << "OpenGLXRVideoFrame::WaitForResourceTransfers(): Got GL_WAIT_FAILED from glClientWaitSync("
                   << ((fence == &initializeCompleteFence) ? "initializeCompleteFence" : "transferCompleteFence") << ": "
                   << ((fence == &initializeCompleteFence) ? initializeCompleteFence : transferCompleteFence) << ")";
    }
    
    gl.glDeleteSync(*fence);
    *fence = nullptr;
  }
}
#endif

}

#endif
