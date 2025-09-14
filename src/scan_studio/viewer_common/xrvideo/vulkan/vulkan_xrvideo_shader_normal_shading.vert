#version 450

layout (location = 0) in uvec3 inPos;
layout (location = 2) in uvec4 inNodeIndices;
layout (location = 3) in uvec4 inNodeWeights;

layout (set = 0, binding = 0, std140) uniform VertexUBO {
  mat4 modelView;
  mat4 modelViewProjection;
  float invTextureWidth;
  float invTextureHeight;
  float bboxMinX;
  float bboxMinY;
  float bboxMinZ;
  float vertexFactorX;
  float vertexFactorY;
  float vertexFactorZ;
} ubo;

layout (set = 0, binding = 1, std430) readonly buffer DeformationStateStorageBuffer { float data[]; } deformationState;

const int K = 4;  // must be equal to XRVideoVertex::K

layout (location = 0) out vec3 vViewPos;

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
        (inNodeWeights[k] == 1) ? (0.5 * (0.5 / 254.)) :
        ((inNodeWeights[k] == 255) ? (253.75 / 254.) :
          ((max(inNodeWeights[k], 1) - 1) / 254.));
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
    uint baseIdx = 12 * inNodeIndices[k];
    
    deformedPosition +=
        weightsAsFloat[k] *
        (vec3(deformationState.data[baseIdx + 0] * originalPosition.x + deformationState.data[baseIdx + 3] * originalPosition.y + deformationState.data[baseIdx + 6] * originalPosition.z,
              deformationState.data[baseIdx + 1] * originalPosition.x + deformationState.data[baseIdx + 4] * originalPosition.y + deformationState.data[baseIdx + 7] * originalPosition.z,
              deformationState.data[baseIdx + 2] * originalPosition.x + deformationState.data[baseIdx + 5] * originalPosition.y + deformationState.data[baseIdx + 8] * originalPosition.z) +
         vec3(deformationState.data[baseIdx + 9], deformationState.data[baseIdx + 10], deformationState.data[baseIdx + 11]));
  }
  
  vViewPos = (ubo.modelView * vec4(deformedPosition, 1.0)).xyz;
  gl_Position = ubo.modelViewProjection * vec4(deformedPosition, 1.0);
}
