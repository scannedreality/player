#include "scan_studio/viewer_common/xrvideo/opengl/opengl_xrvideo_shader.hpp"
#ifdef HAVE_OPENGL

#include <loguru.hpp>

#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"
#include "scan_studio/viewer_common/xrvideo/opengl/opengl_xrvideo_frame.hpp"
#include "scan_studio/viewer_common/opengl/loader.hpp"
#include "scan_studio/viewer_common/opengl/util.hpp"

namespace scan_studio {

OpenGLXRVideoShader::~OpenGLXRVideoShader() {
  Destroy();
}

bool OpenGLXRVideoShader::Initialize(bool outputLinearColors, bool useAlphaBlending, bool useSurfaceNormalShading, bool use_GL_OVR_multiview2) {
  this->useAlphaBlending = useAlphaBlending;
  this->useSurfaceNormalShading = useSurfaceNormalShading;
  this->use_GL_OVR_multiview2 = use_GL_OVR_multiview2;
  
  program.reset(new ShaderProgram());
  
  string vertexShaderSrc;
  if (useSurfaceNormalShading) {
    #ifdef __APPLE__
      vertexShaderSrc = "#version 410\n";
    #else
      vertexShaderSrc = "#version 300 es\n";
    #endif
    if (use_GL_OVR_multiview2) {
      vertexShaderSrc +=
          "#extension GL_OVR_multiview2 : enable\n"
          "layout(num_views = 2) in;\n";
    }
    vertexShaderSrc += R"SHADERCODE(
precision highp float;

in uvec3 inPos;
in uvec4 inNodeIndices;
in uvec4 inNodeWeights;

layout(std140) uniform UniformBufferObject {
)SHADERCODE";
    
    if (use_GL_OVR_multiview2) {
      vertexShaderSrc +=
          "layout(column_major) mat4 modelView[2];\n"
          "layout(column_major) mat4 modelViewProjection[2];\n";
    } else {
      vertexShaderSrc +=
          "layout(column_major) mat4 modelView;\n"
          "layout(column_major) mat4 modelViewProjection;\n";
    }
    
    vertexShaderSrc += R"SHADERCODE(
  float bboxMinX;
  float bboxMinY;
  float bboxMinZ;
  float vertexFactorX;
  float vertexFactorY;
  float vertexFactorZ;
} ubo;

uniform highp usampler2D deformationState;

const int K = 4;  // must be equal to XRVideoVertex::K

out vec3 vViewPos;

vec3 DecodePosition(const in uvec3 encodedPosition) {
  return vec3(
      ubo.bboxMinX + ubo.vertexFactorX * float(encodedPosition.x),
      ubo.bboxMinY + ubo.vertexFactorY * float(encodedPosition.y),
      ubo.bboxMinZ + ubo.vertexFactorZ * float(encodedPosition.z));
}

void main() {
  float weightsAsFloat[K];
  
  // De-quantize the weights to float
  for (int k = 0; k < K; ++ k) {
    weightsAsFloat[k] =
        (inNodeWeights[k] == uint(1)) ? (0.5 * (0.5 / 254.)) :
        ((inNodeWeights[k] == uint(255)) ? (253.75 / 254.) :
          (float(max(inNodeWeights[k], uint(1)) - uint(1)) / 254.));
  }
  
  // Re-normalize the weights
  float weightFactor = 1.f / (weightsAsFloat[0] + weightsAsFloat[1] + weightsAsFloat[2] + weightsAsFloat[3]);  // assumes K == 4
  
  for (int k = 0; k < K; ++ k) {
    weightsAsFloat[k] *= weightFactor;
  }
  
  // Compute the deformed vertex position
  vec3 originalPosition = DecodePosition(inPos);
  vec3 deformedPosition = vec3(0, 0, 0);
  
  for (int k = 0; k < K; ++ k) {
    int nodeIndex = int(inNodeIndices[k]);
    
    ivec2 texSize = textureSize(deformationState, /*lod*/ 0);
    
    int texelIdx = 3 * nodeIndex;
    int texelFetchY = texelIdx / texSize.x;
    vec4 deformationStateA = uintBitsToFloat(texelFetch(deformationState, ivec2(texelIdx - texelFetchY * texSize.x, texelFetchY), /*lod*/ 0));
    
    ++ texelIdx;
    texelFetchY = texelIdx / texSize.x;
    vec4 deformationStateB = uintBitsToFloat(texelFetch(deformationState, ivec2(texelIdx - texelFetchY * texSize.x, texelFetchY), /*lod*/ 0));
    
    ++ texelIdx;
    texelFetchY = texelIdx / texSize.x;
    vec4 deformationStateC = uintBitsToFloat(texelFetch(deformationState, ivec2(texelIdx - texelFetchY * texSize.x, texelFetchY), /*lod*/ 0));
    
    deformedPosition +=
        weightsAsFloat[k] *
        (vec3(deformationStateA[0] * originalPosition.x + deformationStateA[3] * originalPosition.y + deformationStateB[2] * originalPosition.z,
              deformationStateA[1] * originalPosition.x + deformationStateB[0] * originalPosition.y + deformationStateB[3] * originalPosition.z,
              deformationStateA[2] * originalPosition.x + deformationStateB[1] * originalPosition.y + deformationStateC[0] * originalPosition.z) +
         vec3(deformationStateC[1], deformationStateC[2], deformationStateC[3]));
  }
  
)SHADERCODE";
    
    if (use_GL_OVR_multiview2) {
      vertexShaderSrc +=
          "vViewPos = (ubo.modelView[gl_ViewID_OVR] * vec4(deformedPosition, 1.0)).xyz;\n"
          "gl_Position = ubo.modelViewProjection[gl_ViewID_OVR] * vec4(deformedPosition, 1.0);\n";
    } else {
      vertexShaderSrc +=
          "vViewPos = (ubo.modelView * vec4(deformedPosition, 1.0)).xyz;\n"
          "gl_Position = ubo.modelViewProjection * vec4(deformedPosition, 1.0);\n";
    }
    
    vertexShaderSrc += R"SHADERCODE(
}
)SHADERCODE";
  } else {
  #ifdef __APPLE__
    vertexShaderSrc = "#version 410\n";
  #else
    vertexShaderSrc = "#version 300 es\n";
  #endif
    if (use_GL_OVR_multiview2) {
      vertexShaderSrc +=
          "#extension GL_OVR_multiview2 : enable\n"
          "layout(num_views = 2) in;\n";
    }
    vertexShaderSrc += R"SHADERCODE(
precision highp float;

in uvec3 inPos;
in uvec2 inTexcoord;
in uvec4 inNodeIndices;
in uvec4 inNodeWeights;
)SHADERCODE";
    if (useAlphaBlending) {
      vertexShaderSrc += "in uint inVertexAlpha;";
    }
    vertexShaderSrc += R"SHADERCODE(
layout(std140) uniform UniformBufferObject {
)SHADERCODE";
    
    if (use_GL_OVR_multiview2) {
      vertexShaderSrc +=
          "layout(column_major) mat4 modelViewProjection[2];\n";
    } else {
      vertexShaderSrc +=
          "layout(column_major) mat4 modelViewProjection;\n";
    }
    
    vertexShaderSrc += R"SHADERCODE(
  float bboxMinX;
  float bboxMinY;
  float bboxMinZ;
  float vertexFactorX;
  float vertexFactorY;
  float vertexFactorZ;
} ubo;

uniform highp usampler2D deformationState;

const int K = 4;  // must be equal to XRVideoVertex::K

centroid out vec2 vTexcoord;
)SHADERCODE";
    if (useAlphaBlending) {
      vertexShaderSrc += "centroid out float vVertexAlpha;";
    }
    vertexShaderSrc += R"SHADERCODE(

vec3 DecodePosition(const in uvec3 encodedPosition) {
  return vec3(
      ubo.bboxMinX + ubo.vertexFactorX * float(encodedPosition.x),
      ubo.bboxMinY + ubo.vertexFactorY * float(encodedPosition.y),
      ubo.bboxMinZ + ubo.vertexFactorZ * float(encodedPosition.z));
}

void main() {
  vTexcoord = vec2(
      0.5 / 65536.0 + (1.0 / 65536.0) * float(inTexcoord.x),
      0.5 / 65536.0 + (1.0 / 65536.0) * float(inTexcoord.y));
)SHADERCODE";
    if (useAlphaBlending) {
      vertexShaderSrc += "vVertexAlpha = (1.0 / 255.0) * float(inVertexAlpha);";
    }
    vertexShaderSrc += R"SHADERCODE(
  
  float weightsAsFloat[K];
  
  // De-quantize the weights to float
  for (int k = 0; k < K; ++ k) {
    weightsAsFloat[k] =
        (inNodeWeights[k] == uint(1)) ? (0.5 * (0.5 / 254.)) :
        ((inNodeWeights[k] == uint(255)) ? (253.75 / 254.) :
          (float(max(inNodeWeights[k], uint(1)) - uint(1)) / 254.));
  }
  
  // Re-normalize the weights
  float weightFactor = 1.f / (weightsAsFloat[0] + weightsAsFloat[1] + weightsAsFloat[2] + weightsAsFloat[3]);  // assumes K == 4
  
  for (int k = 0; k < K; ++ k) {
    weightsAsFloat[k] *= weightFactor;
  }
  
  // Compute the deformed vertex position
  vec3 originalPosition = DecodePosition(inPos);
  vec3 deformedPosition = vec3(0, 0, 0);
  
  for (int k = 0; k < K; ++ k) {
    int nodeIndex = int(inNodeIndices[k]);
    
    ivec2 texSize = textureSize(deformationState, /*lod*/ 0);
    
    int texelIdx = 3 * nodeIndex;
    int texelFetchY = texelIdx / texSize.x;
    vec4 deformationStateA = uintBitsToFloat(texelFetch(deformationState, ivec2(texelIdx - texelFetchY * texSize.x, texelFetchY), /*lod*/ 0));
    
    ++ texelIdx;
    texelFetchY = texelIdx / texSize.x;
    vec4 deformationStateB = uintBitsToFloat(texelFetch(deformationState, ivec2(texelIdx - texelFetchY * texSize.x, texelFetchY), /*lod*/ 0));
    
    ++ texelIdx;
    texelFetchY = texelIdx / texSize.x;
    vec4 deformationStateC = uintBitsToFloat(texelFetch(deformationState, ivec2(texelIdx - texelFetchY * texSize.x, texelFetchY), /*lod*/ 0));
    
    deformedPosition +=
        weightsAsFloat[k] *
        (vec3(deformationStateA[0] * originalPosition.x + deformationStateA[3] * originalPosition.y + deformationStateB[2] * originalPosition.z,
              deformationStateA[1] * originalPosition.x + deformationStateB[0] * originalPosition.y + deformationStateB[3] * originalPosition.z,
              deformationStateA[2] * originalPosition.x + deformationStateB[1] * originalPosition.y + deformationStateC[0] * originalPosition.z) +
         vec3(deformationStateC[1], deformationStateC[2], deformationStateC[3]));
  }
  
)SHADERCODE";
    
    if (use_GL_OVR_multiview2) {
      vertexShaderSrc +=
          "gl_Position = ubo.modelViewProjection[gl_ViewID_OVR] * vec4(deformedPosition, 1.0);\n";
    } else {
      vertexShaderSrc +=
          "gl_Position = ubo.modelViewProjection * vec4(deformedPosition, 1.0);\n";
    }
    
    vertexShaderSrc += R"SHADERCODE(
}
)SHADERCODE";
  }
  if (!program->AttachShader(vertexShaderSrc.c_str(), ShaderProgram::ShaderType::kVertexShader)) { return false; }
  
  // TODO: Especially for the GL ES 2.0 render path, we should probably optimize this fragment shader.
  //       Probably, we can just sample all three textures at the given texcoord and do the YUV->SRGB conversion after interpolation,
  //       instead of first converting all four bilineaer interpolation samples and then manually interpolating the SRGB results.
  // Note that we directly interpolate the sampled SRGB values here instead of first converting to linear RGB, interpolating in linear space,
  // and then converting back to SRGB, since we currently don't have SRGB render target support for this render path and would thus have to do all of that manually.
  string fragmentShaderSrc;
  if (useSurfaceNormalShading) {
    #ifdef __APPLE__
      fragmentShaderSrc = "#version 410\n";
    #else
      fragmentShaderSrc = "#version 300 es\n";
    #endif
    fragmentShaderSrc += R"SHADERCODE(
precision highp float;

in vec3 vViewPos;

out vec4 fragColor;

/// Converts the given linear color value to an sRGB color value.
/// The input and output range for each color component is [0, 1].
vec3 LinearToSRGB(vec3 value) {
  vec3 bLess = step(vec3(0.0031308), value);
  return mix(value * vec3(12.92), 1.055 * pow(value, vec3(1.0 / 2.4)) - vec3(0.055), bLess);
}

void main() {
  vec3 fdx = dFdx(vViewPos);
  vec3 fdy = dFdy(vViewPos);
  vec3 normal = normalize(cross(fdx, fdy));
  
  fragColor = vec4(0.5 * (normal + vec3(1, 1, 1)), 1.0);
)SHADERCODE";
  
  if (!outputLinearColors) {
    fragmentShaderSrc += "  fragColor.rgb = LinearToSRGB(fragColor.rgb);\n";
  }
  
  fragmentShaderSrc += R"SHADERCODE(
}
)SHADERCODE";
  } else {
    #ifdef __APPLE__
      fragmentShaderSrc = "#version 410\n";
    #else
      fragmentShaderSrc = "#version 300 es\n";
    #endif
    fragmentShaderSrc += R"SHADERCODE(
precision highp float;

)SHADERCODE";
    
    #ifdef __EMSCRIPTEN__
      fragmentShaderSrc += R"SHADERCODE(
uniform sampler2D textureYUV;
)SHADERCODE";
    #else
      fragmentShaderSrc += R"SHADERCODE(
uniform sampler2D textureLuma;
uniform sampler2D textureChromaU;
uniform sampler2D textureChromaV;
)SHADERCODE";
    #endif
    
    fragmentShaderSrc += R"SHADERCODE(
uniform highp vec2 textureSizeVec;  // do not call this "textureSize", that is the name of a predeclared GLSL function

centroid in highp vec2 vTexcoord;
)SHADERCODE";
    if (useAlphaBlending) {
      fragmentShaderSrc += "centroid in mediump float vVertexAlpha;";
    }
    fragmentShaderSrc += R"SHADERCODE(

out vec4 fragColor;

/// Converts the given sRGB color value to a linear color value.
/// The input and output range for each color component is [0, 1].
vec3 SRGBToLinear(vec3 value) {
  vec3 bLess = step(vec3(0.04045), value);
  return mix(value / vec3(12.92), pow((value + vec3(0.055)) / vec3(1.055), vec3(2.4)), bLess);
}

vec3 SampleRGB(const in ivec2 xy) {
)SHADERCODE";
    
    #ifdef __EMSCRIPTEN__
      fragmentShaderSrc += R"SHADERCODE(
  float luma = texelFetch(textureYUV, xy, 0).x;
  ivec2 chromaXY = xy / 2;
  ivec2 chromaAtlasXY = ivec2(chromaXY.x + (((chromaXY.y & 1) == 1) ? (int(textureSizeVec.x) / 2) : 0), chromaXY.y / 2);
  float chromaU = texelFetch(textureYUV, ivec2(0, textureSizeVec.y) + chromaAtlasXY, 0).x;
  float chromaV = texelFetch(textureYUV, ivec2(0, (int(textureSizeVec.y) * 5) / 4) + chromaAtlasXY, 0).x;
)SHADERCODE";
    #else
      fragmentShaderSrc += R"SHADERCODE(
  float luma = texelFetch(textureLuma, xy, 0).x;
  float chromaU = texelFetch(textureChromaU, xy / 2, 0).x;
  float chromaV = texelFetch(textureChromaV, xy / 2, 0).x;
)SHADERCODE";
    #endif
    
    fragmentShaderSrc += R"SHADERCODE(
  
  luma -= 16. / 255.;
  chromaU -= 128. / 255.;
  chromaV -= 128. / 255.;
  
  vec3 srgb = clamp(vec3(
      1.164 * luma                   + 1.596 * chromaV,
      1.164 * luma - 0.392 * chromaU - 0.813 * chromaV,
      1.164 * luma + 2.017 * chromaU                  ), 0., 1.);
)SHADERCODE";
  
  if (outputLinearColors) {
    fragmentShaderSrc += "  return SRGBToLinear(srgb);\n";
  } else {
    fragmentShaderSrc += "  return srgb;\n";
  }
  
  fragmentShaderSrc += R"SHADERCODE(
}

void main() {
  // Simple bilinear interpolation only (in linear RGB space, after converting the four input colors from YUV to linear RGB).
  // TODO: Can we use mip-mapping and anisotropic filtering as well? We need to make sure that the texture is suited for that.
  
  vec2 xy = textureSizeVec * vTexcoord - vec2(0.5, 0.5);
  ivec2 baseXY = ivec2(xy);
  vec2 frac = fract(xy);
  
  vec3 topLeft = SampleRGB(baseXY);
  vec3 topRight = SampleRGB(ivec2(baseXY.x + 1, baseXY.y));
  vec3 bottomLeft = SampleRGB(ivec2(baseXY.x, baseXY.y + 1));
  vec3 bottomRight = SampleRGB(ivec2(baseXY.x + 1, baseXY.y + 1));
  
  // Bilinear interpolation.
  float topLeftWeight     = (1.0 - frac.x) * (1.0 - frac.y);
  float topRightWeight    =        frac.x  * (1.0 - frac.y);
  float bottomLeftWeight  = (1.0 - frac.x) *        frac.y;
  float bottomRightWeight =        frac.x  *        frac.y;
  fragColor = vec4(
      topLeftWeight * topLeft +
      topRightWeight * topRight +
      bottomLeftWeight * bottomLeft +
      bottomRightWeight * bottomRight,
)SHADERCODE";
    if (useAlphaBlending) {
      fragmentShaderSrc += "vVertexAlpha);";
    } else {
      fragmentShaderSrc += "1.0);";
    }
    fragmentShaderSrc += R"SHADERCODE(
}
)SHADERCODE";
  }
  if (!program->AttachShader(fragmentShaderSrc.c_str(), ShaderProgram::ShaderType::kFragmentShader)) { return false; }
  
  // LOG(1) << "OpenGLXRVideo: Linking shader ...";
  if (!program->LinkProgram()) { return false; }
  
  program->UseProgram();
  
  inPosAttrib = gl.glGetAttribLocation(program->program_name(), "inPos");
  if (!useSurfaceNormalShading) {
    inTexcoordAttrib = gl.glGetAttribLocation(program->program_name(), "inTexcoord");
  }
  inNodeIndicesAttrib = gl.glGetAttribLocation(program->program_name(), "inNodeIndices");
  inNodeWeightsAttrib = gl.glGetAttribLocation(program->program_name(), "inNodeWeights");
  if (useAlphaBlending) {
    inVertexAlphaAttrib = gl.glGetAttribLocation(program->program_name(), "inVertexAlpha");
  }
  
  uniformBlockIndex = gl.glGetUniformBlockIndex(program->program_name(), "UniformBufferObject");
  gl.glGetActiveUniformBlockiv(program->program_name(), uniformBlockIndex, /*GL_UNIFORM_BLOCK_DATA_SIZE*/ 0x8A40, &uniformBlockSize);
  uboBuffer.Allocate(uniformBlockSize, GL_ARRAY_BUFFER, GL_STREAM_DRAW);
  ubo.resize(uniformBlockSize);
  gl.glBindBuffer(GL_ARRAY_BUFFER, 0);
  
  deformationStateLocation = program->GetUniformLocationOrAbort("deformationState");
  
  if (!useSurfaceNormalShading) {
    #ifdef __EMSCRIPTEN__
      textureYUVLocation = program->GetUniformLocationOrAbort("textureYUV");
    #else
      textureLumaLocation = program->GetUniformLocationOrAbort("textureLuma");
      textureChromaULocation = program->GetUniformLocationOrAbort("textureChromaU");
      textureChromaVLocation = program->GetUniformLocationOrAbort("textureChromaV");
    #endif
    textureSizeLocation = program->GetUniformLocationOrAbort("textureSizeVec");
  }
  
  return true;
}

void OpenGLXRVideoShader::Destroy() {
  program.reset();
  uboBuffer.Destroy();
}

void OpenGLXRVideoShader::Use(
    GLuint vertexBuffer, GLuint vertexAlphaBuffer, u32 textureWidth, u32 textureHeight,
    const float* modelViewDataColumnMajor, const float* modelViewProjectionDataColumnMajor,
    const float* modelViewDataColumnMajor2, const float* modelViewProjectionDataColumnMajor2,
    float bboxMinX, float bboxMinY, float bboxMinZ, float vertexFactorX, float vertexFactorY, float vertexFactorZ) {
  if (!program) {
    LOG(ERROR) << "Trying to use an uninitialized OpenGLXRVideoShader";
    return;
  }
  
  program->UseProgram();
  CHECK_OPENGL_NO_ERROR();
  
  // Configure attributes.
  // Note: Don't forget to use glVertexAttribIPointer() (with "I" before Pointer) instead of glVertexAttribPointer() for integer types.
  gl.glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
  CHECK_OPENGL_NO_ERROR();
  usize offset = 0;
  
  gl.glEnableVertexAttribArray(inPosAttrib);
  gl.glVertexAttribIPointer(inPosAttrib, 4, GL_UNSIGNED_SHORT, sizeof(XRVideoVertex), reinterpret_cast<void*>(offset));
  offset += 4 * sizeof(u16);
  
  if (!useSurfaceNormalShading) {
    gl.glEnableVertexAttribArray(inTexcoordAttrib);
    gl.glVertexAttribIPointer(inTexcoordAttrib, 2, GL_UNSIGNED_SHORT, sizeof(XRVideoVertex), reinterpret_cast<void*>(offset));
  }
  offset += 2 * sizeof(u16);
  
  gl.glEnableVertexAttribArray(inNodeIndicesAttrib);
  gl.glVertexAttribIPointer(inNodeIndicesAttrib, XRVideoVertex::K, GL_UNSIGNED_SHORT, sizeof(XRVideoVertex), reinterpret_cast<void*>(offset));
  offset += XRVideoVertex::K * sizeof(u16);
  
  gl.glEnableVertexAttribArray(inNodeWeightsAttrib);
  gl.glVertexAttribIPointer(inNodeWeightsAttrib, XRVideoVertex::K, GL_UNSIGNED_BYTE, sizeof(XRVideoVertex), reinterpret_cast<void*>(offset));
  offset += XRVideoVertex::K * sizeof(u8);
  
  if (useAlphaBlending) {
    gl.glBindBuffer(GL_ARRAY_BUFFER, vertexAlphaBuffer);
    
    gl.glEnableVertexAttribArray(inVertexAlphaAttrib);
    gl.glVertexAttribIPointer(inVertexAlphaAttrib, 1, GL_UNSIGNED_BYTE, sizeof(u8), reinterpret_cast<void*>(0));
  }
  
  CHECK_OPENGL_NO_ERROR();
  
  // Update uniforms.
  u8* uboPtr = ubo.data();
  
  if (useSurfaceNormalShading) {
    memcpy(uboPtr, modelViewDataColumnMajor, 4 * 4 * sizeof(float));
    uboPtr += 4 * 4 * sizeof(float);
    if (use_GL_OVR_multiview2) {
      memcpy(uboPtr, modelViewDataColumnMajor2, 4 * 4 * sizeof(float));
      uboPtr += 4 * 4 * sizeof(float);
    }
  }
  
  memcpy(uboPtr, modelViewProjectionDataColumnMajor, 4 * 4 * sizeof(float));
  uboPtr += 4 * 4 * sizeof(float);
  if (use_GL_OVR_multiview2) {
    memcpy(uboPtr, modelViewProjectionDataColumnMajor2, 4 * 4 * sizeof(float));
    uboPtr += 4 * 4 * sizeof(float);
  }
  
  memcpy(uboPtr, &bboxMinX, sizeof(bboxMinX));
  uboPtr += sizeof(bboxMinX);
  
  memcpy(uboPtr, &bboxMinY, sizeof(bboxMinY));
  uboPtr += sizeof(bboxMinY);
  
  memcpy(uboPtr, &bboxMinZ, sizeof(bboxMinZ));
  uboPtr += sizeof(bboxMinZ);
  
  memcpy(uboPtr, &vertexFactorX, sizeof(vertexFactorX));
  uboPtr += sizeof(vertexFactorX);
  
  memcpy(uboPtr, &vertexFactorY, sizeof(vertexFactorY));
  uboPtr += sizeof(vertexFactorY);
  
  memcpy(uboPtr, &vertexFactorZ, sizeof(vertexFactorZ));
  uboPtr += sizeof(vertexFactorZ);
  
  gl.glBindBufferBase(/*GL_UNIFORM_BUFFER*/ 0x8A11, /*index*/ 0, uboBuffer.BufferName());           // bind uboBuffer to GL_UNIFORM_BUFFER[0]
  gl.glBufferSubData(/*GL_UNIFORM_BUFFER*/ 0x8A11, /*offset*/ 0, uboPtr - ubo.data(), ubo.data());  // transfer updated ubo data to uboBuffer on the GPU
  gl.glUniformBlockBinding(program->program_name(), uniformBlockIndex, /*uniformBlockBinding*/ 0);  // use the buffer bound at GL_UNIFORM_BUFFER[0] for the uniform block in the program
  
  // Note: GL_MAX_TEXTURE_IMAGE_UNITS must be at least 16 in OpenGL ES 3.0, so we are fine here
  gl.glUniform1i(deformationStateLocation, 3);  // use GL_TEXTURE0 + 3
  
  if (!useSurfaceNormalShading) {
    #ifdef __EMSCRIPTEN__
      gl.glUniform1i(textureYUVLocation, 0);  // use GL_TEXTURE0
    #else
      gl.glUniform1i(textureLumaLocation, 0);  // use GL_TEXTURE0
      gl.glUniform1i(textureChromaULocation, 1);  // use GL_TEXTURE0 + 1
      gl.glUniform1i(textureChromaVLocation, 2);  // use GL_TEXTURE0 + 2
    #endif
    
    gl.glUniform2f(textureSizeLocation, textureWidth, textureHeight);
  }
  
  CHECK_OPENGL_NO_ERROR();
}

void OpenGLXRVideoShader::DoneUsing() {
  if (useAlphaBlending) {
    gl.glDisableVertexAttribArray(inVertexAlphaAttrib);
  }
  gl.glDisableVertexAttribArray(inNodeWeightsAttrib);
  gl.glDisableVertexAttribArray(inNodeIndicesAttrib);
  if (!useSurfaceNormalShading) {
    gl.glDisableVertexAttribArray(inTexcoordAttrib);
  }
  gl.glDisableVertexAttribArray(inPosAttrib);
}


OpenGLXRVideoStateInterpolationShader::~OpenGLXRVideoStateInterpolationShader() {
  Destroy();
}

bool OpenGLXRVideoStateInterpolationShader::Initialize(bool interpolateFromIdentity) {
  this->interpolateFromIdentity = interpolateFromIdentity;
  
  program.reset(new ShaderProgram());
  
  // The vertex shader generates a viewport-filling triangle without requiring any input vertices or indices. See:
  // https://stackoverflow.com/a/59739538/2676564
  #ifdef __APPLE__
    string vertexShaderSrc = "#version 410\n";
  #else
    string vertexShaderSrc = "#version 300 es\n";
  #endif
  vertexShaderSrc += R"SHADERCODE(
precision highp float;
void main() {
  vec2 vertices[3] = vec2[3](vec2(-1, -1), vec2(3, -1), vec2(-1, 3));
  gl_Position = vec4(vertices[gl_VertexID], 0, 1);
  // texcoords = 0.5 * gl_Position.xy + vec2(0.5);
}
)SHADERCODE";
  if (!program->AttachShader(vertexShaderSrc.c_str(), ShaderProgram::ShaderType::kVertexShader)) { return false; }
  
  string fragmentShaderSrc;
  if (interpolateFromIdentity) {
    // The fragment shader interpolates four float values which are a part of a column-major 3x4 matrix.
    // Conveniently for us, the corresponding four values of the column-major 3x4 identity matrix happen to be always (1, 0, 0, 0)
    // regardless of whether we get the first, second, or third part of the matrix.
    #ifdef __APPLE__
      fragmentShaderSrc = "#version 410\n";
    #else
      fragmentShaderSrc = "#version 300 es\n";
    #endif
    fragmentShaderSrc += R"SHADERCODE(
precision highp float;

uniform int viewportWidth;
uniform float factor;

uniform sampler2D state;

out highp uvec4 result;

void main() {
  ivec2 texelCoord = ivec2(gl_FragCoord.xy);
  int texelIndex = texelCoord.x + viewportWidth * texelCoord.y;
  
  ivec2 texSize = textureSize(state, /*lod*/ 0);
  int texelFetchY = texelIndex / texSize.x;
  vec4 stateVal = texelFetch(state, ivec2(texelIndex - texelFetchY * texSize.x, texelFetchY), /*lod*/ 0);
  
  result = floatBitsToUint(vec4(
      (1.0 - factor) + factor * stateVal.x,
      factor * stateVal.yzw));
}
)SHADERCODE";
  } else {
    #ifdef __APPLE__
      fragmentShaderSrc = "#version 410\n";
    #else
      fragmentShaderSrc = "#version 300 es\n";
    #endif
    fragmentShaderSrc += R"SHADERCODE(
precision highp float;

uniform int viewportWidth;
uniform float factor;

uniform sampler2D state1;
uniform sampler2D state2;

out highp uvec4 result;

void main() {
  ivec2 texelCoord = ivec2(gl_FragCoord.xy);
  int texelIndex = texelCoord.x + viewportWidth * texelCoord.y;
  
  ivec2 texSize = textureSize(state1, /*lod*/ 0);
  int texelFetchY = texelIndex / texSize.x;
  vec4 state1Val = texelFetch(state1, ivec2(texelIndex - texelFetchY * texSize.x, texelFetchY), /*lod*/ 0);
  
  // We assume that state1 and state2 have the same size
  vec4 state2Val = texelFetch(state2, ivec2(texelIndex - texelFetchY * texSize.x, texelFetchY), /*lod*/ 0);
  
  result = floatBitsToUint((1.0 - factor) * state1Val + factor * state2Val);
}
)SHADERCODE";
  }
  if (!program->AttachShader(fragmentShaderSrc.c_str(), ShaderProgram::ShaderType::kFragmentShader)) { return false; }
  
  // LOG(1) << "OpenGLXRVideoStateInterpolationShader: Linking shader ...";
  if (!program->LinkProgram()) { return false; }
  
  program->UseProgram();
  
  if (interpolateFromIdentity) {
    state1Location = program->GetUniformLocationOrAbort("state");
  } else {
    state1Location = program->GetUniformLocationOrAbort("state1");
    state2Location = program->GetUniformLocationOrAbort("state2");
  }
  
  viewportWidthLocation = program->GetUniformLocationOrAbort("viewportWidth");
  factorLocation = program->GetUniformLocationOrAbort("factor");
  
  return true;
}

void OpenGLXRVideoStateInterpolationShader::Destroy() {
  program.reset();
}

void OpenGLXRVideoStateInterpolationShader::Use(int viewportWidth, float factor, int state1TextureUnit, int state2TextureUnit) {
  if (!program) {
    LOG(ERROR) << "Trying to use an uninitialized OpenGLXRVideoStateInterpolationShader";
    return;
  }
  
  program->UseProgram();
  CHECK_OPENGL_NO_ERROR();
  
  // Update uniforms
  gl.glUniform1i(state1Location, state1TextureUnit);
  if (!interpolateFromIdentity) {
    gl.glUniform1i(state2Location, state2TextureUnit);
  }
  
  gl.glUniform1i(viewportWidthLocation, viewportWidth);
  gl.glUniform1f(factorLocation, factor);
  
  CHECK_OPENGL_NO_ERROR();
}

}

#endif
