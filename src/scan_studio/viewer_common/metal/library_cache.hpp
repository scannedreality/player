#pragma once
#ifdef __APPLE__

#include <string>
#include <unordered_map>

#include <libvis/vulkan/libvis.h>

#include <Metal/Metal.hpp>

namespace scan_studio {
using namespace vis;

struct MetalVertexAndFragmentFunctions {
  inline operator bool() {
    return vertexFunction.get() != nullptr &&
           fragmentFunction.get() != nullptr;
  }
  
  NS::SharedPtr<MTL::Function> vertexFunction;
  NS::SharedPtr<MTL::Function> fragmentFunction;
};

class MetalLibraryCache {
 public:
  MTL::Library* GetOrCreateLibrary(const char* name, const void* data, usize size, MTL::Device* device);
  
  /// Convenience function to query a vertex and a fragment function from a shader library, and assign them to a render pipeline descriptor.
  /// You may use operator bool() of the result to conveniently check for success.
  /// You must keep the result object around until creating the pipeline.
  MetalVertexAndFragmentFunctions AssignVertexAndFragmentFunctions(
      const char* vertexFunctionName, const char* fragmentFunctionName, MTL::RenderPipelineDescriptor* pipelineDesc,
      const char* libraryName, const void* libraryData, usize librarySize, MTL::Device* device);
  
 private:
  unordered_map<string, NS::SharedPtr<MTL::Library>> cache;
};

}

#endif
