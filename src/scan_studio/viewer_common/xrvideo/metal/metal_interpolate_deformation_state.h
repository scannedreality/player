#pragma once

#include <simd/simd.h>

//-----------------------------------------------------------------------------------------------------//
// ATTENTION: CMake does not know that the .metal files use these .h files.                            //
//            If you change a header (only), make sure to touch the .metal file to have it recompiled. //
//-----------------------------------------------------------------------------------------------------//

typedef enum InterpolateDeformationState_InputIndex {
  InterpolateDeformationState_InputIndex_state1 = 0,
  InterpolateDeformationState_InputIndex_state2 = 1,
  InterpolateDeformationState_InputIndex_outputState = 2,
  InterpolateDeformationState_InputIndex_factor = 3,
  InterpolateDeformationState_InputIndex_coefficientCount = 4,
} InterpolateDeformationState_InputIndex;

typedef enum InterpolateDeformationStateFromIdentity_InputIndex {
  InterpolateDeformationStateFromIdentity_InputIndex_state1 = 0,
  InterpolateDeformationStateFromIdentity_InputIndex_outputState = 1,
  InterpolateDeformationStateFromIdentity_InputIndex_factor = 2,
  InterpolateDeformationStateFromIdentity_InputIndex_coefficientCount = 3,
} InterpolateDeformationStateFromIdentity_InputIndex;
