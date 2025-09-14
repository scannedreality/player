#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"

#include <atomic>
#include <chrono>
#include <vector>

#include <Eigen/Core>

#include <zstd.h>

#include <dav1d/dav1d.h>
#include <../vcs_version.h>

#include <loguru.hpp>

#include "scan_studio/common/xrvideo_file.hpp"
#include "scan_studio/viewer_common/debug.hpp"
#include "scan_studio/viewer_common/timing.hpp"

namespace scan_studio {

bool XRVideoDecodingContext::Initialize() {
  zstdCtx.reset(ZSTD_createDCtx(), [](ZSTD_DCtx* ctx) { ZSTD_freeDCtx(ctx); });
  return true;
}

void XRVideoDecodingContext::Destroy() {
  zstdCtx.reset();
}

bool XRVideoReadMetadata(const u8** data, usize dataSize, XRVideoFrameMetadata* metadata) {
  if (dataSize < XRVideoHeaderScheme::GetConstantSize()) {
    return false;
  }
  
  u8 version;
  u8 bitflags;
  StructuredPtrReader<XRVideoHeaderScheme>(*data)
      .Read(&version)
      .Read(&bitflags)
      .Read(&metadata->deformationNodeCount)
      .Read(&metadata->startTimestamp)
      .Read(&metadata->endTimestamp)
      .Read(&metadata->textureWidth)
      .Read(&metadata->textureHeight)
      .Read(&metadata->compressedDeformationStateSize)
      .Read(&metadata->compressedRGBSize);
  *data += XRVideoHeaderScheme::GetConstantSize();
  
  if (version != 0) {
    LOG(WARNING) << "Unknown XRVideo frame header version: " << version;
  }
  
  metadata->isKeyframe = bitflags & XRVideoIsKeyframeBitflag;
  metadata->hasVertexAlpha = bitflags & XRVideoHasVertexAlphaBitflag;
  metadata->zstdRGBTexture = bitflags & XRVideoZStdRGBTextureBitflag;
  
  metadata->compressedMeshSize = 0;
  
  if (metadata->isKeyframe) {
    if (dataSize < XRVideoHeaderScheme::GetConstantSize() + XRVideoKeyframeHeaderScheme::GetConstantSize()) {
      return false;
    }
    
    u32 triangleCount;
    float bboxData[6];
    StructuredPtrReader<XRVideoKeyframeHeaderScheme>(*data)
        .Read(&metadata->uniqueVertexCount)
        .Read(&metadata->vertexCount)
        .Read(&triangleCount)
        .Read(bboxData)
        .Read(&metadata->compressedMeshSize)
        .Read(&metadata->encodedVertexWeightsSize);
    *data += XRVideoKeyframeHeaderScheme::GetConstantSize();
    
    if (metadata->uniqueVertexCount > metadata->vertexCount) {
      LOG(ERROR) << "Invalid mesh having uniqueVertexCount (" << metadata->uniqueVertexCount << ") > vertexCount(" << metadata->vertexCount << ")";
      return false;
    }
    
    metadata->indexCount = 3 * triangleCount;
    
    metadata->bboxMinX = bboxData[0];
    metadata->bboxMinY = bboxData[1];
    metadata->bboxMinZ = bboxData[2];
    metadata->vertexFactorX = bboxData[3];
    metadata->vertexFactorY = bboxData[4];
    metadata->vertexFactorZ = bboxData[5];
  } else {
    metadata->uniqueVertexCount = 0;
    metadata->vertexCount = 0;
    metadata->indexCount = 0;
  }
  
  // TODO: When we modify the XRVideo file format, we should probably introduce a separate field for this compressed size,
  //       instead of determining it in that way.
  metadata->compressedVertexAlphaSize = dataSize -
      (XRVideoHeaderScheme::GetConstantSize() +
       (metadata->isKeyframe ? XRVideoKeyframeHeaderScheme::GetConstantSize() : 0) +
       metadata->compressedMeshSize +
       metadata->compressedDeformationStateSize +
       metadata->compressedRGBSize);
  
  // LOG(1) << "XRVideoReadMetadata(): version: " << static_cast<int>(version) << ", bitflags: " << static_cast<int>(bitflags) << ", isKeyframe: " << (metadata->isKeyframe ? "yes" : "no");
  // LOG(1) << "XRVideoReadMetadata(): deformationNodeCount: " << metadata->deformationNodeCount;
  // if (metadata->isKeyframe) {
  //   LOG(1) << "XRVideoReadMetadata(): #vertices: " << metadata->vertexCount << ", #indices: " << metadata->indexCount;
  // }
  // LOG(1) << "XRVideoReadMetadata(): textureWidth: " << metadata->textureWidth << ", textureHeight: " << metadata->textureHeight;
  
  // LOG(1) << "XRVideoReadMetadata(): bboxMin: " << metadata->bboxMinX << ", " << metadata->bboxMinY << ", " << metadata->bboxMinZ;
  // // vertexFactor = (bboxMax - bboxMin) / UINT16_MAX
  // // --> bboxMax = vertexFactor * UINT16_MAX + bboxMin
  // LOG(1) << "XRVideoReadMetadata(): bboxMax: "
  //        << (metadata->vertexFactorX * UINT16_MAX + metadata->bboxMinX) << ", "
  //        << (metadata->vertexFactorY * UINT16_MAX + metadata->bboxMinY) << ", "
  //        << (metadata->vertexFactorZ * UINT16_MAX + metadata->bboxMinZ);
  
  return true;
}

static bool DecompressWithZStd(const u8* src, usize compressedSize, u8* dest, usize destCapacity, usize expectedSize, const char* name, bool verboseDecoding, ZSTD_DCtx* zstdCtx) {
  const TimePoint meshDecompressionStartTime = Clock::now();
  const usize decompressedBytes = ZSTD_decompressDCtx(zstdCtx, dest, destCapacity, src, compressedSize);
  const TimePoint meshDecompressionEndTime = Clock::now();
  
  if (ZSTD_isError(decompressedBytes)) {
    LOG(ERROR) << "Error decompressing the mesh with zstd: " << ZSTD_getErrorName(decompressedBytes);
    return false;
  } else if (decompressedBytes != expectedSize) {
    LOG(ERROR) << name << ": Obtained unexpected byte count (" << decompressedBytes << ") for decompressed data, expected to be " << expectedSize;
    return false;
  }
  if (verboseDecoding) {
    LOG(1) << name << " decompressed with zstd in " << (MillisecondsDuration(meshDecompressionEndTime - meshDecompressionStartTime).count()) << " ms";
  }
  
  return true;
}

static bool DecompressMeshData(const XRVideoFrameMetadata& metadata, vector<u8>* meshData, u32* encodedTexcoordDataSize, const u8** dataPtr, bool verboseDecoding, ZSTD_DCtx* zstdCtx) {
  *encodedTexcoordDataSize = metadata.vertexCount * 2 * sizeof(u16);
  const usize meshDataSize =
      metadata.uniqueVertexCount * 3 * sizeof(u16) +
      (metadata.vertexCount - metadata.uniqueVertexCount) * sizeof(u16) +
      *encodedTexcoordDataSize +
      metadata.indexCount * sizeof(u16) +
      metadata.encodedVertexWeightsSize;
  
  meshData->resize(meshDataSize);  // TODO: Keep allocated?
  if (!DecompressWithZStd(
      *dataPtr, metadata.compressedMeshSize,
      meshData->data(), /*destCapacity*/ meshDataSize, /*expectedSize*/ meshDataSize,
      "Mesh data", verboseDecoding, zstdCtx)) {
    return false;
  }
  
  *dataPtr += metadata.compressedMeshSize;
  return true;
}

static bool DecompressDeformationStateData(const XRVideoFrameMetadata& metadata, float* outDeformationState, const u8** dataPtr, bool verboseDecoding, ZSTD_DCtx* zstdCtx) {
  const u32 deformationStateDataSize = metadata.GetDeformationStateDataSize();
  
  // Decompress the values
  vector<Eigen::half> encodedValues(deformationStateDataSize / sizeof(float));  // TODO: Consider using a pre-allocated buffer rather than allocating a new buffer for each decode
  const u32 encodedValuesSize = encodedValues.size() * sizeof(Eigen::half);
  
  if (!DecompressWithZStd(
      *dataPtr, metadata.compressedDeformationStateSize,
      reinterpret_cast<u8*>(encodedValues.data()), /*destCapacity*/ encodedValuesSize, /*expectedSize*/ encodedValuesSize,
      "Deformation state data", verboseDecoding, zstdCtx)) {
    return false;
  }
  *dataPtr += metadata.compressedDeformationStateSize;
  
  // Decode the values
  for (int i = 0, size = encodedValues.size(); i < size; ++ i) {
    const int coeffIdx = i % 12;
    const bool isOneInIdentity = coeffIdx == 0 || coeffIdx == 4 || coeffIdx == 8;
    
    outDeformationState[i] = static_cast<float>(encodedValues[i]) + (isOneInIdentity ? 1.f : 0);
  }
  
  return true;
}

struct VertexWeights {
  u16 nodeIndices[XRVideoVertex::K];
  u8 nodeWeights[XRVideoVertex::K];
};

static void DecodeVertexWeights(const XRVideoFrameMetadata& metadata, const u8* vertexWeightsPtr, vector<VertexWeights>* decodedVertexWeights) {
  decodedVertexWeights->resize(metadata.vertexCount);
  
  const u8* vertexWeightsEndPtr = vertexWeightsPtr + metadata.encodedVertexWeightsSize;
  VertexWeights* weightsPtr = decodedVertexWeights->data();
  
  while (vertexWeightsPtr < vertexWeightsEndPtr) {
    const u16 firstNodeIndexWithEncodedNodeCount = *reinterpret_cast<const u16*>(vertexWeightsPtr);
    
    if (firstNodeIndexWithEncodedNodeCount == UINT16_MAX) {
      // The vertex does not have any nodes assigned. This case should in theory never occur.
      LOG(WARNING) << "Encountered a vertex without any assigned nodes";
      vertexWeightsPtr += 2;
      for (int k = 0; k < XRVideoVertex::K; ++ k) {
        weightsPtr->nodeIndices[k] = 0;
        weightsPtr->nodeWeights[k] = 0;
      }
      ++ weightsPtr;
      continue;
    }
    
    u32 nodeAssignmentCount = ((firstNodeIndexWithEncodedNodeCount & 0xc000) >> 14) + 1;
    weightsPtr->nodeIndices[0] = firstNodeIndexWithEncodedNodeCount & 0x3fff;
    vertexWeightsPtr += 2;
    
    for (u32 k = 1; k < nodeAssignmentCount; ++ k) {
      weightsPtr->nodeIndices[k] = *reinterpret_cast<const u16*>(vertexWeightsPtr);
      vertexWeightsPtr += 2;
    }
    for (int k = nodeAssignmentCount; k < XRVideoVertex::K; ++ k) {
      weightsPtr->nodeIndices[k] = weightsPtr->nodeIndices[nodeAssignmentCount - 1];
    }
    
    for (u32 k = 0; k < nodeAssignmentCount; ++ k) {
      weightsPtr->nodeWeights[k] = *vertexWeightsPtr;
      vertexWeightsPtr += 1;
    }
    for (u32 k = nodeAssignmentCount; k < XRVideoVertex::K; ++ k) {
      weightsPtr->nodeWeights[k] = 0;
    }
    
    ++ weightsPtr;
  }
  
  if (vertexWeightsPtr != vertexWeightsEndPtr) {
    LOG(ERROR) << "Deformation graph decoding error: Read past vertexWeightsEndPtr";
  }
  if (weightsPtr != decodedVertexWeights->data() + metadata.uniqueVertexCount) {
    LOG(ERROR) << "Deformation graph decoding error: Vertex count does not match";
  }
}

static void WriteRenderableVertices(
    const XRVideoFrameMetadata& metadata,
    const u16* uniqueVertexData,
    const u16* duplicatedVertexSourceIndices,
    const u16* encodedTexcoordData,
    const VertexWeights* decodedVertexWeights,
    XRVideoVertex* outVertices) {
  XRVideoVertex* vertexPtr = outVertices;
  
  // Unique vertices
  for (usize i = 0; i < metadata.uniqueVertexCount; ++ i) {
    // Position
    u32 base = 3 * i;
    vertexPtr->x = uniqueVertexData[base + 0];
    vertexPtr->y = uniqueVertexData[base + 1];
    vertexPtr->z = uniqueVertexData[base + 2];
    
    // Texture coordinates
    base = 2 * i;
    vertexPtr->tx = encodedTexcoordData[base + 0];
    vertexPtr->ty = encodedTexcoordData[base + 1];
    
    // Vertex weights
    memcpy(&vertexPtr->nodeIndices[0], &decodedVertexWeights[i], sizeof(VertexWeights));
    
    ++ vertexPtr;
  }
  
  // Duplicated vertices
  for (usize i = metadata.uniqueVertexCount; i < metadata.vertexCount; ++ i) {
    u32 sourceVertex = duplicatedVertexSourceIndices[i - metadata.uniqueVertexCount];
    
    // Position
    u32 base = 3 * sourceVertex;
    vertexPtr->x = uniqueVertexData[base + 0];
    vertexPtr->y = uniqueVertexData[base + 1];
    vertexPtr->z = uniqueVertexData[base + 2];
    
    // Texture coordinates
    base = 2 * i;
    vertexPtr->tx = encodedTexcoordData[base + 0];
    vertexPtr->ty = encodedTexcoordData[base + 1];
    
    // Vertex weights
    memcpy(&vertexPtr->nodeIndices[0], &decodedVertexWeights[sourceVertex], sizeof(VertexWeights));
    
    ++ vertexPtr;
  }
}

static bool DecompressVertexAlphaData(const XRVideoFrameMetadata& metadata, vector<u8>* outVertexAlpha, const u8** dataPtr, bool verboseDecoding, ZSTD_DCtx* zstdCtx) {
  // NOTE: We use ZSTD_getFrameContentSize() to get the decompressed size here because for dependent frames,
  //       the vertex count is not known here during decoding.
  unsigned long long decompressedSize = ZSTD_getFrameContentSize(*dataPtr, metadata.compressedVertexAlphaSize);
  if (decompressedSize == ZSTD_CONTENTSIZE_UNKNOWN) {
    LOG(ERROR) << "Got ZSTD_CONTENTSIZE_UNKNOWN while decompressing vertex alpha";
    return false;
  } else if (decompressedSize == ZSTD_CONTENTSIZE_ERROR) {
    LOG(ERROR) << "Got ZSTD_CONTENTSIZE_ERROR while decompressing vertex alpha";
    return false;
  }
  
  outVertexAlpha->resize(decompressedSize);
  
  if (!DecompressWithZStd(
      *dataPtr, metadata.compressedVertexAlphaSize,
      outVertexAlpha->data(), /*destCapacity*/ decompressedSize, /*expectedSize*/ decompressedSize,
      "Vertex alpha data", verboseDecoding, zstdCtx)) {
    return false;
  }
  *dataPtr += metadata.compressedVertexAlphaSize;
  
  return true;
}

bool XRVideoDecompressContent(
    const u8* content,
    const XRVideoFrameMetadata& metadata,
    XRVideoDecodingContext* decodingContext,
    void* outVertices,
    u16* outIndices,
    float* outDeformationState,
    u16* outDuplicatedVertexSourceIndices,
    vector<u8>* outVertexAlpha,
    bool verboseDecoding) {
  const u8* dataPtr = content;
  
  TimePoint decompressionStartTime;
  if (verboseDecoding) {
    decompressionStartTime = Clock::now();
  }
  
  // Decompress the mesh data for keyframes
  vector<u8> meshData;
  u32 encodedTexcoordDataSize = 0;
  if (metadata.isKeyframe &&
      !DecompressMeshData(metadata, &meshData, &encodedTexcoordDataSize, &dataPtr, verboseDecoding, decodingContext->GetZStdContext())) {
    return false;
  }
  
  // Decompress the deformation state data
  if (metadata.compressedDeformationStateSize > 0 &&
      !DecompressDeformationStateData(metadata, outDeformationState, &dataPtr, verboseDecoding, decodingContext->GetZStdContext())) {
    return false;
  }
  
  // Convert the mesh to renderable format
  if (metadata.isKeyframe) {
    TimePoint renderableVertexCreationStartTime;
    if (verboseDecoding) {
      renderableVertexCreationStartTime = Clock::now();
    }
    
    // TODO: This should better be done on the GPU with a compute shader for better performance.
    //       Note that compute shaders are only supported from OpenGL ES 3.1 on,
    //       however they could be emulated with a fragment shader / transform feedback.
    // Set pointers to the mesh data parts (vertices, indices, texcoords)
    const u8* meshDataPtr = meshData.data();
    
    const u16* uniqueVertexData = reinterpret_cast<const u16*>(meshDataPtr);
    meshDataPtr += metadata.uniqueVertexCount * 3 * sizeof(u16);
    
    const u16* duplicatedVertexSourceIndices = reinterpret_cast<const u16*>(meshDataPtr);
    meshDataPtr += (metadata.vertexCount - metadata.uniqueVertexCount) * sizeof(u16);
    
    const u16* encodedTexcoordData = reinterpret_cast<const u16*>(meshDataPtr);
    meshDataPtr += encodedTexcoordDataSize;
    
    const u16* indexData = reinterpret_cast<const u16*>(meshDataPtr);
    meshDataPtr += metadata.GetIndexDataSize();
    
    const u8* encodedVertexWeights = reinterpret_cast<const u8*>(meshDataPtr);
    meshDataPtr += metadata.encodedVertexWeightsSize;
    
    // Copy the index data to the output
    memcpy(outIndices, indexData, metadata.GetIndexDataSize());
    
    // Decode the vertex weights (node indices and node weights)
    vector<VertexWeights> decodedVertexWeights;
    DecodeVertexWeights(metadata, encodedVertexWeights, &decodedVertexWeights);
    
    // Write out the renderable vertices
    WriteRenderableVertices(metadata, uniqueVertexData, duplicatedVertexSourceIndices, encodedTexcoordData, decodedVertexWeights.data(), static_cast<XRVideoVertex*>(outVertices));
    
    // If non-null, copy the duplicated source vertices indices to the output
    if (outDuplicatedVertexSourceIndices != nullptr) {
      memcpy(outDuplicatedVertexSourceIndices, duplicatedVertexSourceIndices, (metadata.vertexCount - metadata.uniqueVertexCount) * sizeof(u16));
    }
    
    if (verboseDecoding) {
      const TimePoint renderableVertexCreationEndTime = Clock::now();
      LOG(1) << "Generated " << metadata.GetRenderableVertexCount() << " renderable vertices in " << (MillisecondsDuration(renderableVertexCreationEndTime - renderableVertexCreationStartTime).count()) << " ms";
    }
  }
  
  // Decompress the vertex alpha data
  dataPtr += metadata.compressedRGBSize;
  if (outVertexAlpha) {
    outVertexAlpha->clear();
    
    if (metadata.compressedVertexAlphaSize > 0 &&
        !DecompressVertexAlphaData(metadata, outVertexAlpha, &dataPtr, verboseDecoding, decodingContext->GetZStdContext())) {
      return false;
    }
  }
  
  if (verboseDecoding) {
    const TimePoint decompressionEndTime = Clock::now();
    LOG(1) << "Frame decoded in " << (MillisecondsDuration(decompressionEndTime - decompressionStartTime).count()) << " ms";
  }
  
  return true;
}

void XRVideoCopyTexture(const Dav1dPicture& picture, u8* outTexture, bool verboseDecoding) {
  const usize lumaPixelCount = picture.p.w * picture.p.h;
  
  XRVideoCopyTexture(
      picture,
      outTexture,
      outTexture + lumaPixelCount,
      outTexture + (lumaPixelCount * 5) / 4,
      verboseDecoding);
}

void XRVideoCopyTexture(const Dav1dPicture& picture, u8* outTextureLuma, u8* outTextureChromaU, u8* outTextureChromaV, bool verboseDecoding) {
  const int width = picture.p.w;
  const int height = picture.p.h;
  
  TimePoint startTime;
  if (verboseDecoding) {
    startTime = Clock::now();
  }
  
  if (outTextureLuma) {
    if (picture.stride[0] == width) {
      // Tight packing
      memcpy(outTextureLuma, picture.data[0], width * height);
    } else {
      for (u32 y = 0; y < height; ++ y) {
        memcpy(
            outTextureLuma + y * width,
            static_cast<const u8*>(picture.data[0]) + y * picture.stride[0],
            width);
      }
    }
  }
  
  if (outTextureChromaU && outTextureChromaV) {
    if (picture.stride[1] == width / 2) {
      // Tight packing
      memcpy(
          outTextureChromaU,
          picture.data[1],
          (width * height) / 4);
      memcpy(
          outTextureChromaV,
          picture.data[2],
          (width * height) / 4);
    } else {
      const u32 tightStride = width / 2;
      for (u32 y = 0; y < height / 2; ++ y) {
        memcpy(
            outTextureChromaU + y * tightStride,
            static_cast<const u8*>(picture.data[1]) + y * picture.stride[1],
            tightStride);
        memcpy(
            outTextureChromaV + y * tightStride,
            static_cast<const u8*>(picture.data[2]) + y * picture.stride[1],
            tightStride);
      }
    }
  }
  
  if (verboseDecoding) {
    const TimePoint endTime = Clock::now();
    LOG(1) << "Copying data out of the dav1d frame took " << (MillisecondsDuration(endTime - startTime).count()) << " ms";
  }
}

// // DEBUG: Convert the YUV data to RGB on the CPU
// vector<u8> debugImage(3 * metadata.textureWidth * metadata.textureHeight);
// 
// for (int y = 0; y < metadata.textureHeight; ++ y) {
//   const u8* lumaRow = static_cast<const u8*>(dav1dPicture->data[0]) + y * dav1dPicture->stride[0];
//   const u8* uRow = static_cast<const u8*>(dav1dPicture->data[1]) + (y / 2) * dav1dPicture->stride[1];
//   const u8* vRow = static_cast<const u8*>(dav1dPicture->data[2]) + (y / 2) * dav1dPicture->stride[1];
//   // u8* rgbaPtr = outTexture + 4 * y * dav1dPicture->p.w;
//   
//   for (int x = 0; x < metadata.textureWidth; ++ x) {
//     float luma = static_cast<float>(*lumaRow) - 16;
//     float u = static_cast<float>(*uRow) - 128;
//     float v = static_cast<float>(*vRow) - 128;
//     
//     // rgbaPtr[0] = static_cast<u8>(max(0, min<int>(255, 1.164 * luma             + 1.596 * v + 0.5f)));
//     // rgbaPtr[1] = static_cast<u8>(max(0, min<int>(255, 1.164 * luma - 0.392 * u - 0.813 * v + 0.5f)));
//     // rgbaPtr[2] = static_cast<u8>(max(0, min<int>(255, 1.164 * luma + 2.017 * u             + 0.5f)));
//     // rgbaPtr[3] = 0;  // unused
//     
//     debugImage[3 * (x + y * metadata.textureWidth) + 0] = static_cast<u8>(max(0, min<int>(255, 1.164 * luma             + 1.596 * v + 0.5f)));
//     debugImage[3 * (x + y * metadata.textureWidth) + 1] = static_cast<u8>(max(0, min<int>(255, 1.164 * luma - 0.392 * u - 0.813 * v + 0.5f)));
//     debugImage[3 * (x + y * metadata.textureWidth) + 2] = static_cast<u8>(max(0, min<int>(255, 1.164 * luma + 2.017 * u             + 0.5f)));
//     
//     lumaRow += 1;
//     if (x & 1) {
//       uRow += 1;
//       vRow += 1;
//     }
//     // rgbaPtr += 4;
//   }
// }
// 
// ostringstream debugImagePath;
// static int debugImageCounter = 0;
// debugImagePath << filesystem::temp_directory_path() / "debug_decoded_image_" << debugImageCounter << ".ppm";
// ++ debugImageCounter;
// SaveDebugImageAsPPM(debugImagePath.str().c_str(), metadata.textureWidth, metadata.textureHeight, /*inColor*/ true, debugImage.data());

}
