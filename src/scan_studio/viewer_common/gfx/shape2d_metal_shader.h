#pragma once

#include <simd/simd.h>

//-----------------------------------------------------------------------------------------------------//
// ATTENTION: CMake does not know that the .metal files use these .h files.                            //
//            If you change a header (only), make sure to touch the .metal file to have it recompiled. //
//-----------------------------------------------------------------------------------------------------//

/// Buffer index values
typedef enum Shape2D_InputIndex {
  Shape2D_VertexInputIndex_vertices     = 0,
  Shape2D_VertexInputIndex_instanceData = 1,
  
  Shape2D_FragmentInputIndex_instanceData = 0,
} Shape2D_InputIndex;

/// Vertex layout
typedef struct {
  simd_float2 position;
} Shape2D_Vertex;

/// Per-instance data (vertex shader)
struct Shape2D_Vertex_InstanceData {
  simd_float4x4 modelViewProjection;
};

/// Per-instance data (fragment shader)
struct Shape2D_Fragment_InstanceData {
  simd_float4 color;
};
