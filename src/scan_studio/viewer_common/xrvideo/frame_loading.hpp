#pragma once

#include <algorithm>
#include <memory>
#include <vector>

#include <libvis/vulkan/libvis.h>

typedef struct Dav1dPicture Dav1dPicture;
typedef struct ZSTD_DCtx_s ZSTD_DCtx;

namespace scan_studio {
using namespace vis;

// This file contains functions to load XRVideo frames from their compressed storage format.

#pragma pack(push, 1)
/// Vertex format for XRVideo meshes.
/// Note that the 'w' component of the position is unused and exists for padding only.
/// For a long time, we used a vertex format without that padding, but it was required for the following cases:
/// * The Unity plugin. It does not seem to support using three 16-bit numbers for the position attribute
///   at all at the time of writing (November 2022).
/// * Best compatibility for the Vulkan render path, since the R16G16B16 formats have only about 60% support
///   for use in vertex buffers according to https://vulkan.gpuinfo.org at the time of writing (November 2022).
///   For example, it did not work on my Radeon RX 6600 (although it is supported for higher-tier RX 6000 - generation cards).
/// I guess that there is also a chance of improved performance in other cases due to better alignment.
struct XRVideoVertex {
  constexpr static int K = 4;
  
  u16 x;
  u16 y;
  u16 z;
  u16 w;  // unused, for padding only
  u16 tx;
  u16 ty;
  u16 nodeIndices[K];
  u8 nodeWeights[K];
};
#pragma pack(pop)

struct XRVideoFrameMetadata {
  /// Frame start timestamp
  s64 startTimestamp;
  /// Frame end timestamp
  s64 endTimestamp;
  
  /// Whether this XRVideo frame is a keyframe.
  /// Keyframes contain vertex and index data in addition to the usual data.
  bool isKeyframe;
  
  /// Whether this XRVideo frame contains vertex alpha values.
  bool hasVertexAlpha;
  
  /// Whether the texture is stored as zstd-compressed RGB data (instead of AV.1-compressed YUV data)
  bool zstdRGBTexture;
  
  /// Number of unique vertices in the mesh, i.e., excluding vertices duplicated for texturing (for keyframes only)
  u16 uniqueVertexCount;
  
  /// Number of vertices in the mesh, including vertices duplicated for texturing (for keyframes only)
  u16 vertexCount;
  
  /// Number of indices in the mesh (for keyframes only; three times the triangle count)
  u32 indexCount;
  
  /// Texture dimensions in texels
  u32 textureWidth;
  u32 textureHeight;
  
  /// Mesh bounding box, required to decode the vertex positions (for keyframes only)
  float bboxMinX, bboxMinY, bboxMinZ;
  float vertexFactorX, vertexFactorY, vertexFactorZ;
  
  /// Number of deformation nodes in the deformation state.
  /// For keyframes, also specifies the number of deformation nodes in the deformation graph.
  u16 deformationNodeCount;
  
  /// Size of the compressed mesh data (for keyframes only; otherwise, always set to zero)
  u32 compressedMeshSize;
  /// Size of the decompressed but still encoded vertex weight data (for keyframes only)
  u32 encodedVertexWeightsSize;
  /// Size of the compressed deformation state data
  u32 compressedDeformationStateSize;
  /// Size of the compressed texture YUV (or uncompressed texture RGB) data
  u32 compressedRGBSize;
  /// Size of the compressed vertex alpha data
  u32 compressedVertexAlphaSize;
  
  // Derived information:
  
  /// Returns the number of "renderable" vertices (i.e., including vertices duplicated for texturing)
  /// created from the given unique vertices (for keyframes only).
  inline u32 GetRenderableVertexCount() const {
    return isKeyframe ? vertexCount : 0;
  }
  
  /// Size in bytes required for the renderable vertex buffer (for keyframes only).
  inline u32 GetRenderableVertexDataSize() const {
    return isKeyframe ? (vertexCount * sizeof(XRVideoVertex)) : 0;
  }
  
  /// Size in bytes required for the index buffer.
  inline u32 GetIndexDataSize() const {
    return indexCount * sizeof(u16);
  }
  
  /// Size in bytes of the deformation state
  inline u32 GetDeformationStateDataSize() const {
    return deformationNodeCount * 12 * sizeof(float);
  }
  
  /// Size in bytes required for the texture (in YUV I420 format if compressed, or RGB format if uncompressed).
  inline u32 GetTextureDataSize() const {
    return zstdRGBTexture ? (textureWidth * textureHeight * 3) : ((textureWidth * textureHeight * 3) / 2);
  }
  
  /// Size in bytes required for the luma part of the texture
  inline u32 GetTextureLumaDataSize() const { return textureWidth * textureHeight; }
  /// Size in bytes required for the chroma parts of the texture
  inline u32 GetTextureChromaDataSize() const { return (textureWidth * textureHeight) / 4; }
  
  /// Computes the maximum bounding box coordinate.
  inline float GetBBoxMaxX() const { return vertexFactorX * UINT16_MAX + bboxMinX; }
  inline float GetBBoxMaxY() const { return vertexFactorY * UINT16_MAX + bboxMinY; }
  inline float GetBBoxMaxZ() const { return vertexFactorZ * UINT16_MAX + bboxMinZ; }
};

/// Groups necessary context data to decode XRVideo frames.
/// TODO: This used to contain the dav1d context as well.
///       Now that the dav1d context was moved out, should this be renamed / dissolved?
class XRVideoDecodingContext {
 public:
  bool Initialize();
  void Destroy();
  
  inline ZSTD_DCtx* GetZStdContext() const { return zstdCtx.get(); }
  
 private:
  shared_ptr<ZSTD_DCtx> zstdCtx;
};

/// Reads the given XRVideo frame's metadata.
///
/// After this, the texture and mesh sizes are known, such that the corresponding
/// GPU buffers can be allocated with the required sizes.
///
/// - `data` must be a pointer to a pointer to the compressed frame data.
///   This pointer is modified to point to after the metadata,
///   such that it can be used as input for the next read function.
/// - The struct pointed to by `metadata` will be filled with the read metadata.
///
/// Returns true on success, false otherwise.
bool XRVideoReadMetadata(
    const u8** data,
    usize dataSize,
    XRVideoFrameMetadata* metadata);

/// Decompresses the content (excluding the texture) of an XRVideo frame into user-provided buffers.
///
/// The `content` pointer must be the modified data pointer resulting from a successful call to XRVideoReadMetadata().
///
/// TODO: Texture decoding was removed from this function. The following is outdated:
/// There are two modes for texture output:
///
/// - If textureZeroCopy is non-null, outputs the texture within the pointed-to Dav1dPicture object.
///   The picture must be memset to zero before being passed in. When the function returns,
///   the picture's ownership is transferred to the caller, and it must be released as follows:
///
///   if (picture.ref) {
///     dav1d_picture_unref(&picture);
///   }
///
///   Notice that dav1d's default picture allocator uses a non-tight stride for the image allocations,
///   so it is not suitable if contiguous memory copies out of it are desired. Thus, to make this mode work well,
///   you probably need to pass your own implementation of Dav1dZeroCopy to XRVideoDecodingContext::Initialize().
///
///   One might be tempted to use the custom Dav1dZeroCopy's allocator to directly allocate driver memory,
///   but in that case, it should probably not be write-combined, as I guess that dav1d reads from the memory
///   to decode subsequent non-keyframes that depend on these frames (TODO: I did not confirm this however; try it out).
///
///   Also, notice that a comment in dav1d's internal dav1d_default_picture_alloc() claims that
///   using a non-tight stride is likely faster for texture widths that are a multiple of 1024.
///   Using the zero-copy mode with such a texture size and a tight stride might thus have a certain
///   performance penalty that needs to be outweighted by the lack of the copy to be beneficial overall.
///
///   TODO: For code paths that can directly access driver memory for texture transfers,
///         even if directly decoding to write-combined memory turns out to be slow,
///         would it still be beneficial overall to decode to non-write-combined driver memory
///         (if such memory exists) and directly use that as transfer source for the texture transfers?
///
///   TODO: We currently only use zero-copy mode for OpenGL, where we always decode into CPU memory
///         that is then passed to glTex[Sub]Image2D(). Look into using that mode for the other render paths
///         as well. Would it also be suitable for platforms with shared CPU-GPU memory to directly
///         decode into the final memory locations?
///
/// - If textureZeroCopy is null, then the texture will be output in outTextureLuma, outTextureChroma[U/V]
///   if those are non-null. This involves extra memory copies out of the internal Dav1dPicture object.
///
///   In this mode, prefer using a contiguous memory allocation for the whole texture data, placing outTextureChromaU right after
///   outTextureLuma, and outTextureChromaV right after outTextureChromaU. This is since if we start using WebCodecs,
///   it might decode the data only in this layout.
///
/// - outDuplicatedVertexSourceIndices is optional; if nullptr is passed, it is ignored.
///
/// Returns true on success, false otherwise.
///
/// TODO: The parameter count here is a bit high.
///       Refactor this to make it (and XRVideoAdvanceDecodingState()) member functions of XRVideoDecodingContext.
///       Then, we can refactor some of the current function parameters to configuration made via (optionally!) calling other member functions on that class.
///       Follow-up: Since now the zstd and dav1d decoding parts have been moved to separate threads, maybe this is not the right way to proceed anymore.
bool XRVideoDecompressContent(
    const u8* content,
    const XRVideoFrameMetadata& metadata,
    XRVideoDecodingContext* decodingContext,
    void* outVertices,
    u16* outIndices,
    float* outDeformationState,
    u16* outDuplicatedVertexSourceIndices,
    vector<u8>* outVertexAlpha,
    bool verboseDecoding);

/// Copies the YUV texture data out of the Dav1dPicture object to continuous storage.
/// The Y, U, and V parts follow each other in that order.
void XRVideoCopyTexture(
    const Dav1dPicture& picture,
    u8* outTexture,
    bool verboseDecoding);

/// Variant of XRVideoCopyTexture() that copies the luma and two chroma parts to different addresses.
/// Prefer the other variant because with that one it may be easier to avoid copies in the future.
void XRVideoCopyTexture(
    const Dav1dPicture& picture,
    u8* outTextureLuma,
    u8* outTextureChromaU,
    u8* outTextureChromaV,
    bool verboseDecoding);

}
