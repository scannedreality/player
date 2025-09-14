#pragma once
#ifdef HAVE_OPENGL

#include <memory>
#include <vector>

#ifdef __EMSCRIPTEN__
  #include <emscripten/threading.h>
#endif

#if defined(_WIN32) || defined(__APPLE__)
  #include <SDL_opengl.h>
#else
  #include <GLES2/gl2.h>
#endif

extern "C" {
#include <dav1d/picture.h>
}

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/opengl/buffer.hpp"
#include "scan_studio/viewer_common/opengl/texture.hpp"

#include "scan_studio/viewer_common/xrvideo/decoding_thread.hpp"
#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"
#include "scan_studio/viewer_common/xrvideo/xrvideo_frame.hpp"

namespace scan_studio {
using namespace vis;

class OpenGLXRVideo;
class OpenGLXRVideoShader;
class TextureFramePromise;

/// Represents a single frame of an XRVideo, for rendering with OpenGL.
class OpenGLXRVideoFrame : public XRVideoFrame {
 public:
  OpenGLXRVideoFrame();
  ~OpenGLXRVideoFrame();
  
  inline OpenGLXRVideoFrame(OpenGLXRVideoFrame&& other) = default;
  inline OpenGLXRVideoFrame& operator= (OpenGLXRVideoFrame&& other) = default;
  
  inline OpenGLXRVideoFrame(const OpenGLXRVideoFrame& other) = delete;
  inline OpenGLXRVideoFrame& operator= (const OpenGLXRVideoFrame& other) = delete;
  
  void Configure(OpenGLXRVideo* video);
  
  void UseExternalBuffers(GLuint vertexBuffer, GLuint indexBuffer, GLuint alphaBuffer);
  
  #ifndef __EMSCRIPTEN__
  bool InitializeTextures(u32 textureWidth, u32 textureHeight, void* lumaData, void* chromaUData, void* chromaVData);
  #endif
  
  bool Initialize(
      const XRVideoFrameMetadata& metadata,
      const u8* contentPtr,
      TextureFramePromise* textureFramePromise,
      XRVideoDecodingContext* decodingContext,
      bool verboseDecoding);
  void Destroy();
  
  /// If this frame is a keyframe, then lastKeyframe may be nullptr.
  void Render(
      const float* modelViewDataColumnMajor,
      const float* modelViewProjectionDataColumnMajor,
      const float* modelViewDataColumnMajor2,
      const float* modelViewProjectionDataColumnMajor2,
      GLuint interpolatedDeformationStateTexture,
      OpenGLXRVideoShader* shader,
      const OpenGLXRVideoFrame* lastKeyframe) const;
  
  /// This function ensures that the frame was loaded
  /// by the background thread's OpenGL context, by waiting for its fence (if necessary).
  void WaitForResourceTransfers();
  
  inline const GLTexture& GetDeformationStateTexture() const { return deformationStateTexture; }
  
  #ifdef __EMSCRIPTEN__
    inline GLuint TextureLuma() const { LOG(ERROR) << "The WebGL path uses a combined texture atlas"; return 0; }
    inline GLuint TextureChromaU() const { LOG(ERROR) << "The WebGL path uses a combined texture atlas"; return 0; }
    inline GLuint TextureChromaV() const { LOG(ERROR) << "The WebGL path uses a combined texture atlas"; return 0; }
  #else
    inline GLuint TextureLuma() const { return textureLuma.Name(); }
    inline GLuint TextureChromaU() const { return textureChromaU.Name(); }
    inline GLuint TextureChromaV() const { return textureChromaV.Name(); }
  #endif
  
 private:
  #ifdef __EMSCRIPTEN__
    static int WaitForResourceTransfers_MainThreadStatic(int thisInt);
    bool WaitForResourceTransfers_MainThread();
  #endif
  
  #ifdef __EMSCRIPTEN__
    em_queued_call* transferCall = nullptr;
  #else
    GLsync initializeCompleteFence = nullptr;
    GLsync transferCompleteFence = nullptr;
  #endif
  
  // Temporary state passed between the Initialize(Impl1/2) functions:
  void* vertexBufferPtr;
  u16* indexBufferPtr;
  
  #ifdef __EMSCRIPTEN__
    vector<u8> vertexStagingBuffer;
    vector<u8> indexStagingBuffer;
  #endif
  
  UniqueDav1dPicturePtr textureData;
  
  vector<float> deformationState;
  u32 deformationStateTextureWidth;
  u32 deformationStateTextureHeight;
  
  vector<u8> vertexAlpha;
  
  // OpenGL resources
  GLBuffer vertexBuffer;
  GLBuffer indexBuffer;
  GLTexture deformationStateTexture;
  GLBuffer vertexAlphaBuffer;
  
  // NOTE: For emscripten, we use a texture 'atlas', respectively a single YUV texture that contains
  //       the luma and chroma U/V data. This is because for some reason, at some point glTexImage2D()
  //       seemed to have an insane per-call overhead in Safari on iOS, so the workaround was to reduce
  //       the number of textures used.
  // TODO: However, using a single atlas / YUV texture probably makes sense in general, since it generally
  //       reduces the number of render API calls, and would help us to simplify our D3D11 render path
  //       (which currently, as the only one of our render paths, has the requirement to decode the
  //       luma and chroma data to non-continuous memory buffers for the current use in different staging
  //       textures).
  #ifdef __EMSCRIPTEN__
    GLTexture texture;
  #else
    GLTexture textureLuma;
    GLTexture textureChromaU;
    GLTexture textureChromaV;
  #endif
  
  bool useExternalBuffers = false;
  
  bool verboseDecoding;
  bool isInitialized = false;
  
  OpenGLXRVideo* video;  // not owned
};

}

#endif
