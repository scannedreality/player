#include "scan_studio/viewer_common/metal/library_cache.hpp"

#include <loguru.hpp>

namespace scan_studio {

MTL::Library* MetalLibraryCache::GetOrCreateLibrary(const char* name, const void* data, usize size, MTL::Device* device) {
  const string nameStr = name;
  
  auto it = cache.find(nameStr);
  if (it != cache.end()) {
    return it->second.get();
  }
  
  // LOG(INFO) << "Debug: Allocating new Metal Library";
  
  auto library_data = dispatch_data_create(data, size, dispatch_get_main_queue(), {});
  
  NS::Error* err;
  NS::SharedPtr<MTL::Library> library = NS::TransferPtr(device->newLibrary(library_data, &err));
  if (!library) {
    LOG(ERROR) << "Failed to load Metal library: " << err->localizedDescription()->utf8String();
    return nullptr;
  }
  
  cache[nameStr] = library;
  return library.get();
}

MetalVertexAndFragmentFunctions MetalLibraryCache::AssignVertexAndFragmentFunctions(
    const char* vertexFunctionName, const char* fragmentFunctionName, MTL::RenderPipelineDescriptor* pipelineDesc,
    const char* libraryName, const void* libraryData, usize librarySize, MTL::Device* device) {
  MetalVertexAndFragmentFunctions result;
  
  MTL::Library* shaderLibrary = GetOrCreateLibrary(libraryName, libraryData, librarySize, device);
  if (!shaderLibrary) { return result; }
  
  result.vertexFunction = NS::TransferPtr(shaderLibrary->newFunction(NS::String::string(vertexFunctionName, NS::UTF8StringEncoding)));
  if (!result.vertexFunction) { LOG(ERROR) << "Failed to load " << vertexFunctionName; return result; }
  result.fragmentFunction = NS::TransferPtr(shaderLibrary->newFunction(NS::String::string(fragmentFunctionName, NS::UTF8StringEncoding)));
  if (!result.fragmentFunction) { LOG(ERROR) << "Failed to load " << fragmentFunctionName; return result; }
  
  pipelineDesc->setVertexFunction(result.vertexFunction.get());
  pipelineDesc->setFragmentFunction(result.fragmentFunction.get());
  
  return result;
}

}
