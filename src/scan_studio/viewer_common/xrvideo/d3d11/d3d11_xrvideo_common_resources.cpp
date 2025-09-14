#ifdef HAVE_D3D11
#include "scan_studio/viewer_common/xrvideo/d3d11/d3d11_xrvideo_common_resources.hpp"

#include <d3dcompiler.h>

#include <loguru.hpp>

#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"

namespace scan_studio {

/// Adjusted from: https://github.com/walbourn/directx-sdk-samples/blob/main/BasicCompute11/BasicCompute11.cpp
static HRESULT CreateComputeShader(const string& src, LPCSTR sourceName, LPCSTR entrypoint, ID3D11Device* device, ID3D11ComputeShader** shader) {
  DWORD shaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
  #ifdef _DEBUG
    // Set the D3DCOMPILE_DEBUG flag to embed debug information in the shaders.
    // Setting this flag improves the shader debugging experience, but still allows
    // the shaders to be optimized and to run exactly the way they will run in
    // the release configuration of this program.
    shaderFlags |= D3DCOMPILE_DEBUG;
  
    // Disable optimizations to further improve shader debugging
    shaderFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
  #endif
  
  // We generally prefer to use the higher CS shader profile when possible as CS 5.0 has better performance on DX11-class hardware
  LPCSTR pTarget = (device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0) ? "cs_5_0" : "cs_4_0";
  
  ID3DBlob* errorBlob = nullptr;
  ID3DBlob* codeBlob = nullptr;
  HRESULT hr = D3DCompile(src.data(), src.size(), sourceName, /*pDefines*/ nullptr, /*pInclude*/ nullptr, entrypoint, pTarget, shaderFlags, /*Flags2 - ignored*/ 0, &codeBlob, &errorBlob);
  if (FAILED(hr)) {
    if (errorBlob) { LOG(ERROR) << (char*)errorBlob->GetBufferPointer(); errorBlob->Release(); }
    if (codeBlob) { codeBlob->Release(); }
    return hr;
  }
  
  hr = device->CreateComputeShader(codeBlob->GetBufferPointer(), codeBlob->GetBufferSize(), /*pClassLinkage*/ nullptr, shader);
  
  if (errorBlob) { errorBlob->Release(); }
  if (codeBlob) { codeBlob->Release(); }
  
  // #if defined(_DEBUG) || defined(PROFILE)
  //   if (SUCCEEDED(hr)) {
  //     (*shader)->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(pFunctionName), pFunctionName);
  //   }
  // #endif
  
  return hr;
}


D3D11XRVideoCommonResources::~D3D11XRVideoCommonResources() {
  Destroy();
}

bool D3D11XRVideoCommonResources::Initialize(ID3D11Device5* device) {
  // TODO: The D3D11 render path is currently only implemented for use in the Unity plugin.
  //       Thus, it currently cannot render the meshes itself, since Unity does that when the plugin is used.
  //       So, no render pipelines are initialized here.
  
  // Initialize deformation state interpolation
  const string interpolateDeformationStateShaderSrc = R"SHADERCODE(
StructuredBuffer<float> state1 : register(t0);
StructuredBuffer<float> state2 : register(t1);
RWStructuredBuffer<float> outputState : register(u0);

cbuffer globals : register(b0) {
    float factor;
    uint coefficientCount;
};

[numthreads(256, 1, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
  if (tid.x < coefficientCount) {
    outputState[tid.x] =
        (1.0f - factor) * state1[tid.x] +
        factor * state2[tid.x];
  }
}
)SHADERCODE";
  ID3D11ComputeShader* interpolateDeformationStateShader = nullptr;
  if (FAILED(CreateComputeShader(
      interpolateDeformationStateShaderSrc, /*sourceName*/ "interpolateDeformationState", /*entrypoint*/ "CSMain",
      device, &interpolateDeformationStateShader))) {
    LOG(ERROR) << "Failed to compile interpolateDeformationState shader";
    Destroy(); return false;
  }
  interpolateDeformationState.reset(interpolateDeformationStateShader, [](ID3D11ComputeShader* shader) { shader->Release(); });
  
  const string interpolateDeformationStateFromIdentityShaderSrc = R"SHADERCODE(
StructuredBuffer<float> state : register(t0);
RWStructuredBuffer<float> outputState : register(u0);

cbuffer globals : register(b0) {
    float factor;
    uint coefficientCount;
};

[numthreads(256, 1, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
  if (tid.x < coefficientCount) {
    uint coefficientIndex = tid.x % 12;
    float identityCoefficient = (coefficientIndex == 0 || coefficientIndex == 4 || coefficientIndex == 8) ? 1.0f : 0.0f;
    outputState[tid.x] =
        (1.0f - factor) * identityCoefficient +
        factor * state[tid.x];
  }
}
)SHADERCODE";
  ID3D11ComputeShader* interpolateDeformationStateFromIdentityShader = nullptr;
  if (FAILED(CreateComputeShader(
      interpolateDeformationStateFromIdentityShaderSrc, /*sourceName*/ "interpolateDeformationStateFromIdentity", /*entrypoint*/ "CSMain",
      device, &interpolateDeformationStateFromIdentityShader))) {
    LOG(ERROR) << "Failed to compile interpolateDeformationStateFromIdentity shader";
    Destroy(); return false;
  }
  interpolateDeformationStateFromIdentity.reset(interpolateDeformationStateFromIdentityShader, [](ID3D11ComputeShader* shader) { shader->Release(); });
  
  return true;
}

void D3D11XRVideoCommonResources::Destroy() {
  interpolateDeformationState.reset();
  interpolateDeformationStateFromIdentity.reset();
}

}

#endif
