#pragma once

#include <simd/simd.h>

//-----------------------------------------------------------------------------------------------------//
// ATTENTION: CMake does not know that the .metal files use these .h files.                            //
//            If you change a header (only), make sure to touch the .metal file to have it recompiled. //
//-----------------------------------------------------------------------------------------------------//

/// Buffer index values
typedef enum Fontstash_InputIndex {
  Fontstash_VertexInputIndex_vertices     = 0,
  Fontstash_VertexInputIndex_instanceData = 1,
  
  Fontstash_FragmentTextureInputIndex_fontTexture = 0,
} Fontstash_InputIndex;

/// Vertex layout
typedef struct {
  simd_packed_float2 position;
  simd_packed_float2 texcoord;
  simd_packed_uchar4 color;
} Fontstash_Vertex;

/// Per-instance data (vertex shader)
struct Fontstash_Vertex_InstanceData {
  simd_float4x4 modelViewProjection;
};
