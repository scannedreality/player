#pragma once

#include <simd/simd.h>

//-----------------------------------------------------------------------------------------------------//
// ATTENTION: CMake does not know that the .metal files use these .h files.                            //
//            If you change a header (only), make sure to touch the .metal file to have it recompiled. //
//-----------------------------------------------------------------------------------------------------//

/// Buffer index values
typedef enum XRVideo_InputIndex {
  XRVideo_VertexInputIndex_vertices     = 0,
  XRVideo_VertexInputIndex_instanceData = 1,
  XRVideo_VertexInputIndex_deformationState = 2,
  
  XRVideo_FragmentInputIndex_instanceData = 0,
  
  XRVideo_FragmentTextureInputIndex_textureLuma = 0,
  XRVideo_FragmentTextureInputIndex_textureChromaU = 1,
  XRVideo_FragmentTextureInputIndex_textureChromaV = 2,
} XRVideo_InputIndex;

/// Vertex layout
typedef struct {
  simd_packed_ushort4 position;
  simd_packed_ushort2 texcoord;
  simd_packed_ushort4 nodeIndices;
  simd_packed_uchar4 nodeWeights;
} XRVideo_Vertex;

/// Per-instance data (vertex shader)
struct XRVideo_Vertex_InstanceData {
  simd_float4x4 modelViewProjection;
  simd_float4 bboxMin;       // 4th component is unused
  simd_float4 vertexFactor;  // 4th component is unused
};

/// Per-instance data (fragment shader)
struct XRVideo_Fragment_InstanceData {
  simd_float2 textureSize;
};
