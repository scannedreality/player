#include <metal_stdlib>

#include "metal_interpolate_deformation_state.h"

using namespace metal;

kernel void InterpolateDeformationState(
    device const float* state1 [[buffer(InterpolateDeformationState_InputIndex_state1)]],
    device const float* state2 [[buffer(InterpolateDeformationState_InputIndex_state2)]],
    device float* outputState [[buffer(InterpolateDeformationState_InputIndex_outputState)]],
    constant float& factor [[buffer(InterpolateDeformationState_InputIndex_factor)]],
    constant uint32_t& coefficientCount [[buffer(InterpolateDeformationState_InputIndex_coefficientCount)]],
    uint index [[thread_position_in_grid]]) {
  if (index < coefficientCount) {
    outputState[index] =
        (1.0f - factor) * state1[index] +
        factor * state2[index];
  }
}

kernel void InterpolateDeformationStateFromIdentity(
    device const float* state [[buffer(InterpolateDeformationStateFromIdentity_InputIndex_state1)]],
    device float* outputState [[buffer(InterpolateDeformationStateFromIdentity_InputIndex_outputState)]],
    constant float& factor [[buffer(InterpolateDeformationStateFromIdentity_InputIndex_factor)]],
    constant uint32_t& coefficientCount [[buffer(InterpolateDeformationStateFromIdentity_InputIndex_coefficientCount)]],
    uint index [[thread_position_in_grid]]) {
  if (index < coefficientCount) {
    uint coefficientIndex = index % 12;
    float identityCoefficient = (coefficientIndex == 0 || coefficientIndex == 4 || coefficientIndex == 8) ? 1.0f : 0.0f;
    
    outputState[index] =
        (1.0f - factor) * identityCoefficient +
        factor * state[index];
  }
}
