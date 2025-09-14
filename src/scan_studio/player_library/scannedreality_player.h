#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


// --- Prerequisites ---

// Since C does not have a bool type, define our own
typedef uint32_t SRBool32;
#define SRV_TRUE  1U
#define SRV_FALSE 0U

// Define SCANNEDREALITY_VIEWER_API for exporting / importing symbols
#if defined(_WIN32)
  // The project-level compilation options define SCANNEDREALITY_VIEWER_EXPORTS when compiling the library, which will export the functions.
  // This define must not be present when using the library header from client code, such that the import variant will be used in that case.
  #ifdef SCANNEDREALITY_VIEWER_EXPORTS
    #define SCANNEDREALITY_VIEWER_API __declspec(dllexport)
  #else
    #define SCANNEDREALITY_VIEWER_API __declspec(dllimport)
  #endif
#elif defined(__MACH__) || defined(__ANDROID__) || defined(__linux__)
  #define SCANNEDREALITY_VIEWER_API __attribute__((visibility ("default")))
#else
  #define SCANNEDREALITY_VIEWER_API
#endif


// --- Logging ---

/**
 * Function pointer type for a log callback from the ScannedReality player library to the user code.
 * This may be used to output log messages from the ScannedReality player library in a user-defined way
 * (for example, as an on-screen message, or in a custom log window).
 *
 * The verbosity levels are those of the "loguru" library.
 * In particular, level 0 means INFO, -1 means WARNING, -2 means ERROR, and -3 means FATAL,
 * while level 1 and higher are verbose log messages.
 */
typedef void (* SRPlayer_LogCallback)(
    int32_t verbosity,
    const char* message,
    const char* filename,
    uint32_t line,
    void* userData);

/**
 * Initializes the library's logging. Should be called after loading the library.
 *
 * @param logToStdErr If set to SRV_TRUE, the library will log messages to stderr (in addition to any logging via a provided logCallback).
 */
SCANNEDREALITY_VIEWER_API
void SRPlayer_InitializeLogging(SRBool32 logToStdErr);

/** Opaque type for added log callbacks */
typedef struct SRPlayer_LogCallbackHandle SRPlayer_LogCallbackHandle;

/**
 * Adds a custom log callback and returns a handle to the callback.
 *
 * @param logCallback Callback that will be called for each log message that is output by the library.
 * @param logCallbackUserData User data pointer that will be passed to each call of logCallback.
 * @returns A handle to the added log callback that may later be used with SRPlayer_RemoveLogCallback().
 */
SCANNEDREALITY_VIEWER_API
SRPlayer_LogCallbackHandle* SRPlayer_AddLogCallback(SRPlayer_LogCallback logCallback, void* logCallbackUserData);

/**
 * Removes the custom log callback with the given handle.
 *
 * @param callbackHandle The handle to the callback to remove, as returned by SRPlayer_AddLogCallback().
 * @returns SRV_TRUE if the callback was found and removed successfully, SRV_FALSE on failure.
 */
SCANNEDREALITY_VIEWER_API
SRBool32 SRPlayer_RemoveLogCallback(SRPlayer_LogCallbackHandle* callbackHandle);


// --- XRVideo ---

/**
 * Must read `size` bytes, or as many bytes as possible in case there are not enough bytes left in the input,
 * store them at the `data` pointer, and advance the read cursor by the number of bytes read.
 * Must return the actual number of bytes read.
 */
typedef uint64_t (* SRPlayer_InputCallbacks_Read)(void* data, uint64_t size, void* userData);

/**
 * Must set the read cursor to the given offset (in bytes) from the start of the input.
 * Must return true if successful, false in case of an error.
 */
typedef SRBool32 (* SRPlayer_InputCallbacks_Seek)(uint64_t offsetFromStart, void* userData);

/** Must return the size of the whole available input in bytes (regardless of the position of the current read cursor) */
typedef uint64_t (* SRPlayer_InputCallbacks_SizeInBytes)(void* userData);

/** Must close the input stream */
typedef void (* SRPlayer_InputCallbacks_Close)(void* userData);

/**
 * May be used to substitute the functions that are used by the library to read video files,
 * for example, in order to read video files that are stored within a custom archive format.
 *
 * Notice that the library currently never caches the data that it reads in this way,
 * so the performance of these read functions should be suitable for streaming video frames.
 */
typedef struct SRPlayer_InputCallbacks {
  SRPlayer_InputCallbacks_Read readCallback;
  SRPlayer_InputCallbacks_Seek seekCallback;
  SRPlayer_InputCallbacks_SizeInBytes sizeInBytesCallback;
  SRPlayer_InputCallbacks_Close closeCallback;
  
  /** User-provided pointer that gets passed to all of the above callbacks */
  void* userData;
} SRPlayer_InputCallbacks;

typedef struct SRPlayer_XRVideo_Frame_Metadata {
  /**
   * Start timestamp of the frame, measured in nanoseconds, with an unspecified origin,
   * i.e., the first frame in the video may start at a different timestamp than zero!
   *
   * For keyframes, at the start timestamp, the identity deformation applies.
   * For non-keyframes, at the start timestamp, the end deformation of their previous frame applies.
   * For both types of frames, when proceeding towards `endTimestampNanoseconds`,
   * the initial deformation as specified above is blended towards the end deformation,
   * which is given by the frame's own deformation state.
   */
  int64_t startTimestampNanoseconds;
  
  /** End timestamp of the frame. See `startTimestampNanoseconds`. */
  int64_t endTimestampNanoseconds;
  
  /**
   * Is this frame a keyframe?
   * If yes, the frame contains vertex and index data, otherwise, it does not.
   */
  SRBool32 isKeyframe;
  
  /**
   * Width of the luma part of the texture.
   * The width of the chroma parts of the texture is (textureWidth / 2).
   */
  uint32_t textureWidth;
  
  /**
   * Height of the luma part of the texture.
   * The height of the chroma parts of the texture is (textureHeight / 2).
   */
  uint32_t textureHeight;
  
  /** Number of unique vertices in the mesh, i.e., excluding vertices duplicated for texturing (for keyframes only) */
  uint32_t uniqueVertexCount;
  
  /** Size in bytes of the decoded, ready-to-render vertex data (for keyframes only). */
  uint32_t renderableVertexDataSize;
  
  /** Size in bytes of the index data (for keyframes only). */
  uint32_t indexDataSize;
  
  /** Size in bytes of the deformation data. */
  uint32_t deformationDataSize;
  
  /**
   * Mesh bounding box, required to decode the vertex positions (for keyframes only).
   * Each dimension is decoded as follows:
   * vertexFactorX * decodedValueX + bboxMinX
   */
  float bboxMinX, bboxMinY, bboxMinZ;
  float vertexFactorX, vertexFactorY, vertexFactorZ;
} SRPlayer_XRVideo_Frame_Metadata;

/**
 * This callback is called by the thread that calls the New... / Load... functions on the XRVideo.
 * It is called for each new XRVideo frame when it gets allocated (like a C++ constructor).
 *
 * The called function may allocate any necessary external data, and return a user-data pointer.
 * The returned pointer will subsequently be passed to all other callbacks for that XRVideo frame.
 *
 * @param videoUserData The user-provided pointer with the same name from the SRPlayer_XRVideo_External_Config struct.
 * @return A user-defined pointer that will be passed as `frameUserData` to all other callbacks for the newly constructed XRVideo frame.
 */
typedef void* (* SRPlayer_XRVideo_External_ConstructFrameCallback)(void* videoUserData);

/**
 * This callback is called by the thread that calls the Destroy function on the XRVideo.
 * It is called for each XRVideo frame when it is destructed (like a C++ destructor).
 *
 * The called function may deallocate any potential externally made allocations.
 *
 * @param videoUserData The user-provided pointer with the same name from the SRPlayer_XRVideo_External_Config struct.
 * @param frameUserData The user-provided pointer that was returned by the SRPlayer_XRVideo_External_ConstructFrameCallback callback when the frame was allocated.
 */
typedef void (* SRPlayer_XRVideo_External_DestructFrameCallback)(void* videoUserData, void* frameUserData);

/**
 * This callback is called by the `decoding thread` (an internal thread allocated by the ScannedRealityPlayer library)
 * after reading a frame's metadata, but before decoding the frame's full content.
 *
 * The called function must set the out-parameters to the addresses to which the video frame data shall be decoded.
 * It must ensure that sufficient space to write the data is available at these addresses (based on frameMetadata).
 *
 * TODO: Document the format of the decoded data.
 *
 * @param videoUserData The user-provided pointer with the same name from the SRPlayer_XRVideo_External_Config struct.
 * @param frameUserData The user-provided pointer that was returned by the SRPlayer_XRVideo_External_ConstructFrameCallback callback when the frame was allocated.
 * @param frameMetadata The metadata for the frame that is being decoded.
 * @param outVertices Pointer to a pointer that must be set to the address to which the vertex data shall be decoded (for keyframes only, ignored otherwise).
 * @param outIndices Pointer to a pointer that must be set to the address to which the index data shall be decoded (for keyframes only, ignored otherwise).
 * @param outDeformation Pointer to a pointer that must be set to the address to which the deformation data shall be decoded.
 * @param outTexture Pointer to a pointer that must be set to the address to which the texture data shall be decoded.
 * @param outDuplicatedVertexSourceIndices Pointer to a pointer that may be set to the address to which the duplicated vertex source index array
 *                                         will be copied (optional, and for keyframes only, ignored otherwise or if set to nullptr).
 * @return SRV_TRUE on success, SRV_FALSE on failure.
 */
typedef SRBool32 (* SRPlayer_XRVideo_External_DecodingThread_PrepareDecodeFrameCallback)(
    void* videoUserData,
    void* frameUserData,
    const SRPlayer_XRVideo_Frame_Metadata* frameMetadata,
    void** outVertices,
    void** outIndices,
    void** outDeformation,
    void** outTexture,
    void** outDuplicatedVertexSourceIndices);

/**
 * This callback is called by the `decoding thread` (an internal thread allocated by the ScannedRealityPlayer library)
 * after decoding the frame's full content.
 *
 * The called function may start transferring the frame's data to the GPU (if the data shall be displayed).
 * Important: This callback should only start the transfers, but it must not wait for them to complete!
 *            Waiting would stall the decoding pipeline unnecessarily, reducing decoding throughput.
 *            If you are not sure, prefer to handle all stages of the transfers in the transfer thread callback instead.
 *
 * @param videoUserData The user-provided pointer with the same name from the SRPlayer_XRVideo_External_Config struct.
 * @param frameUserData The user-provided pointer that was returned by the SRPlayer_XRVideo_External_ConstructFrameCallback callback when the frame was allocated.
 * @param frameMetadata The metadata for the frame that is being decoded.
 * @param vertexAlphaSize Number of vertex alpha elements for the current frame.
 * @param vertexAlpha Pointer to the per-frame vertex alpha data.
 * @return SRV_TRUE on success, SRV_FALSE on failure.
 */
typedef SRBool32 (* SRPlayer_XRVideo_External_DecodingThread_AfterDecodeFrameCallback)(
    void* videoUserData,
    void* frameUserData,
    const SRPlayer_XRVideo_Frame_Metadata* frameMetadata,
    uint32_t vertexAlphaSize,
    uint8_t* vertexAlpha);

/**
 * This callback is called by the `transfer thread` (an internal thread allocated by the ScannedRealityPlayer library)
 * after it received the frame from the `decoding thread`.
 * 
 * If the implementation of AfterDecodeFrameCallback already started the CPU-to-GPU data transfers on the decoding thread,
 * then the implementation of this callback only needs to wait for their completion.
 * Otherwise, it must start those transfers and also wait for them to complete.
 *
 * @param videoUserData The user-provided pointer with the same name from the SRPlayer_XRVideo_External_Config struct.
 * @param frameUserData The user-provided pointer that was returned by the SRPlayer_XRVideo_External_ConstructFrameCallback callback when the frame was allocated.
 * @param frameMetadata The metadata for the frame that is being decoded.
 */
typedef void (* SRPlayer_XRVideo_External_TransferThread_TransferFrameCallback)(
    void* videoUserData,
    void* frameUserData,
    const SRPlayer_XRVideo_Frame_Metadata* frameMetadata);

/** Groups together the callback configuration that is specific for EXTERNAL XRVideos */
typedef struct SRPlayer_XRVideo_External_Config {
  /**
   * Frame constructor and destructor callbacks
   * (these are called by the threads that call the XRVideo API functions)
   */
  SRPlayer_XRVideo_External_ConstructFrameCallback constructFrameCallback;
  SRPlayer_XRVideo_External_DestructFrameCallback destructFrameCallback;
  
  /** Decoding thread callbacks */
  SRPlayer_XRVideo_External_DecodingThread_PrepareDecodeFrameCallback decodingThread_prepareDecodeFrameCallback;
  SRPlayer_XRVideo_External_DecodingThread_AfterDecodeFrameCallback decodingThread_afterDecodeFrameCallback;
  
  /** Transfer thread callback */
  SRPlayer_XRVideo_External_TransferThread_TransferFrameCallback transferThread_transferFrameCallback;
  
  /**
   * User-provided pointer that gets passed to all of the above callbacks.
   *
   * This may be used to access globally relevant data, for example,
   * shader GPU objects that are used for rendering XRVideo frames,
   * or shader GPU objects for interpolating the deformation state data on the GPU.
   */
  void* videoUserData;
} SRPlayer_XRVideo_External_Config;

/** Opaque type for an allocated XRVideo */
typedef struct SRPlayer_XRVideo SRPlayer_XRVideo;

/** Opaque type for a render lock obtained from an XRVideo */
typedef struct SRPlayer_XRVideoRenderLock SRPlayer_XRVideoRenderLock;

/** The asynchronous load state of a video */
enum SRPlayer_AsyncLoadState {
  /**
   * Asynchronous loading is in progress.
   * In this state, the file metadata, index, and playback state *must not* be accessed by user code.
   */
  Loading = 0,
  
  /**
   * There was an error during asynchronous loading.
   * The video thus cannot be displayed.
   */
  Error = 1,
  
  /**
   * Asynchronous loading has finished.
   *  Please note that even if a video is in this state, it still may not display a frame,
   *  since another asynchronous process (frame decoding) also has to run for this.
   */
  Ready = 2
};

/** Defines the looping behavior for video playback */
enum SRPlayer_PlaybackMode {
  /** Plays the video once, then stops at the end */
  SingleShot = 0,
  
  /** Plays the video in a loop */
  Loop = 1,
  
  /**
   * First plays the video normally, then continues playing it backwards when reaching the end, then starts to play forward again, etc.
   * Note that using BackAndForth is only recommended when caching all video frames, otherwise playback may be very choppy.
   */
  BackAndForth = 2
};

/**
 * Allocates a new XRVideo in EXTERNAL mode.
 *
 * EXTERNAL mode means that the library will not create any GPU objects itself for display of the video.
 * Instead, it will only provide the (decoded) raw data in CPU memory in callbacks, and it is the user's task to display it (or use it in any other way).
 *
 * @param cachedDecodedFrameCount Number of decoded video frames to keep in the internal cache, or 0 to cache all decoded frames of the video
 *                                (which is only recommended for very short clips since this uses a large amount of memory).
 *                                Lowering this number reduces memory consumption, but increases the chance of playback stuttering.
 *                                Notice that a certain minimum value is required for decoding to work properly, but this minimum value may change
 *                                when the implementation changes; at the moment, we thus recommend to never set this to less than 20,
 *                                and generally recommend to leave it at the suggested default value of 30.
 * @param config Pointer to a filled configuration structure, providing the callbacks that will be called by
 *               the library while decoding the video data. It is not necessary to keep the config struct
 *               around after the function call returns.
 * @returns On success, returns an opaque handle to the allocated video. On failure, returns null.
 */
SCANNEDREALITY_VIEWER_API
SRPlayer_XRVideo* SRPlayer_XRVideo_NewExternal(uint32_t cachedDecodedFrameCount, SRPlayer_XRVideo_External_Config* config);

/**
 * Deallocates the given XRVideo.
 *
 * Notice that all render locks of a video must be released before destroying the video.
 *
 * @param video The XRVideo to destroy.
 */
SCANNEDREALITY_VIEWER_API
void SRPlayer_XRVideo_Destroy(SRPlayer_XRVideo* video);

/**
 * Loads a video from the XRV file at the given absolute path.
 *
 * This function may be called more than once on a given XRVideo. Calls made when a video file is already loaded
 * will make the video object switch from its current to the new video.
 *
 * @param video The allocated XRVideo object for which to load the video.
 * @param path Path to the XRV video file to load.
 * @param cacheAllFrames Whether to cache all decoded frames of the video
 *                       (such that the video only needs to be decoded once, even if playing it multiple times after each other).
 *                       This is only recommended for very short clips since it uses a large amount of memory.
 * @param playbackMode Initial playback mode for the video, from the SRPlayer_PlaybackMode enum.
 * @return SRV_TRUE on success, SRV_FALSE on failure.
 */
SCANNEDREALITY_VIEWER_API
SRBool32 SRPlayer_XRVideo_LoadFile(SRPlayer_XRVideo* video, const char* path, SRBool32 cacheAllFrames, uint32_t playbackMode);

/**
 * Variant of SRPlayer_XRVideo_LoadFile() that loads the file data using custom user-provided input callbacks instead.
 * This for example allows to read video files from custom archive formats.
 *
 * See the SRPlayer_InputCallbacks struct for which callbacks need to be provided.
 *
 * @param video The allocated XRVideo object for which to load the video.
 * @param input Pointer to a struct filled with the user-provided input callbacks.
 *              It is not necessary to keep this struct around after the function call returns.
 * @param cacheAllFrames See the equivalent parameter of SRPlayer_XRVideo_LoadFile().
 * @param playbackMode See the equivalent parameter of SRPlayer_XRVideo_LoadFile().
 * @return On success, returns SRV_TRUE and takes ownership of the input handle in the sense that the library ensures that the Close callback will be called when the XRVideo is destructed.
 *         On failure, returns SRV_FALSE and does NOT take ownership of the input handle - in this case, the user code is responsible for closing the input once the code is done using it!
 */
SCANNEDREALITY_VIEWER_API
SRBool32 SRPlayer_XRVideo_LoadCustom(SRPlayer_XRVideo* video, SRPlayer_InputCallbacks* input, SRBool32 cacheAllFrames, uint32_t playbackMode);

/**
 * When a video is loaded, the video attributes will be loaded asynchronously.
 * SRPlayer_XRVideo_GetAsyncLoadState() must be used to determine whether this asynchronous loading process is still in progress,
 * whether it is finished, or whether it resulted in an error.
 *
 * @param video The XRVideo object for which to query the async load state.
 * @return The current asynchronous loading state of the video.
 */
SCANNEDREALITY_VIEWER_API
SRPlayer_AsyncLoadState SRPlayer_XRVideo_GetAsyncLoadState(SRPlayer_XRVideo* video);

/**
 * If an XRVideo_Load function is called on a given video object while a video is already loaded,
 * the object will switch to the new video in the background.
 * SwitchedToMostRecentVideo may be used to query whether this switch has completed,
 * and a frame from the new video can be displayed.
 *
 * @param video The XRVideo to query.
 * @return SRV_TRUE if the video object has switched to the video given with the
 *         most recent call to a XRVideo_Load function, and a frame from it has been decoded
 *         and can be displayed; SRV_FALSE otherwise.
 */
SCANNEDREALITY_VIEWER_API
SRBool32 SRPlayer_XRVideo_SwitchedToMostRecentVideo(SRPlayer_XRVideo* video);

/**
 * Sets the video's playback mode.
 *
 * @param video The XRVideo to operate on.
 * @param playbackMode The new playback mode for the video, from the SRPlayer_PlaybackMode enum.
 */
SCANNEDREALITY_VIEWER_API
void SRPlayer_XRVideo_SetPlaybackMode(SRPlayer_XRVideo* video, uint32_t playbackMode);

/**
 * Returns the start timestamp of the video.
 * Notice that this may differ from zero!
 *
 * This function must only be called after SRPlayer_XRVideo_GetAsyncLoadState() has returned SRPlayer_AsyncLoadState::Ready.
 *
 * @param video The XRVideo to query.
 * @return The video start timestamp, measured in nanoseconds.
 */
SCANNEDREALITY_VIEWER_API
int64_t SRPlayer_XRVideo_GetStartTimestampNanoseconds(SRPlayer_XRVideo* video);

/**
 * Returns the end timestamp of the video.
 *
 * This function must only be called after SRPlayer_XRVideo_GetAsyncLoadState() has returned SRPlayer_AsyncLoadState::Ready.
 *
 * @param video The XRVideo to query.
 * @return The video end timestamp, measured in nanoseconds.
 */
SCANNEDREALITY_VIEWER_API
int64_t SRPlayer_XRVideo_GetEndTimestampNanoseconds(SRPlayer_XRVideo* video);

/**
 * Returns the current playback timestamp of the video.
 *
 * This function must only be called after SRPlayer_XRVideo_GetAsyncLoadState() has returned SRPlayer_AsyncLoadState::Ready.
 *
 * @param video The XRVideo to query.
 * @return The current playback timestamp, measured in nanoseconds.
 */
SCANNEDREALITY_VIEWER_API
int64_t SRPlayer_XRVideo_GetPlaybackTimestampNanoseconds(SRPlayer_XRVideo* video);

/**
 * Seeks the video to the given timestamp.
 * Note that the video may not immediately display the seeked-to frame because that frame may
 * first need to be decoded (which happens in the background).
 *
 * This function must only be called after SRPlayer_XRVideo_GetAsyncLoadState() has returned SRPlayer_AsyncLoadState::Ready.
 *
 * @param video The XRVideo to operate on.
 * @param timestampNanoseconds The timestamp to seek to, in nanoseconds.
 * @param forward Specifies whether the video will play forward or backward starting from the seeked-to timestamp.
 */
SCANNEDREALITY_VIEWER_API
void SRPlayer_XRVideo_Seek(SRPlayer_XRVideo* video, int64_t timestampNanoseconds, SRBool32 forward);

/**
 * Returns whether the video is currently in the "buffering" state,
 * meaning that not enough future video frames are cached to be able to play the video.
 * In this state, playback will remain at the same timestamp (even when calling
 * SRPlayer_XRVideo_Update() with a positive number of elapsed nanoseconds),
 * to allow for video frame decoding to catch up.
 *
 * @param video The XRVideo to query.
 * @return SRV_TRUE if the video is currently buffering, SRV_FALSE if not.
 */
SCANNEDREALITY_VIEWER_API
SRBool32 SRPlayer_XRVideo_IsBuffering(SRPlayer_XRVideo* video);

/**
 * If SRPlayer_XRVideo_IsBuffering() returns true, returns a (very) rough estimate of the buffering progress, in percent.
 *
 * @param video The XRVideo to query.
 * @return A rough estimate of the buffering progress, in percent.
 */
SCANNEDREALITY_VIEWER_API
float SRPlayer_XRVideo_GetBufferingProgressPercent(SRPlayer_XRVideo* video);

/**
 * Advances the video playback by the given elapsed time in nanoseconds.
 * This should be called before each rendered frame, even if playback is paused, to allow for
 * asynchronous updates to be carried out; if the video is paused, pass zero for elapsedNanoseconds.
 *
 * Note that the update will be ignored if the video is currently in the "buffering" state
 * (meaning that not enough video frames have been decoded in the background yet
 * to allow for starting playback). In that case, discards the update.
 * The buffering state may be queried with SRPlayer_XRVideo_IsBuffering().
 *
 * @param video The XRVideo to operate on.
 * @param elapsedNanoseconds The number of nanoseconds by which to advance the playback.
 */
SCANNEDREALITY_VIEWER_API
int64_t SRPlayer_XRVideo_Update(SRPlayer_XRVideo* video, int64_t elapsedNanoseconds);

/**
 * Prepares for rendering the current timestamp of the video by creating a `render lock` for it.
 * A render lock encapsulates the renderable state that is required to render a given timestamp of the video.
 *
 * After creating a render lock, it may be passed on to a separate render thread such that updating the video
 * and rendering it may be handled by different threads.
 *
 * User code should minimize the time it holds on to a render lock, since holding on to too many render locks
 * could stall further decoding of the video.
 *
 * Each created render lock must be released with SRPlayer_XRVideoRenderLock_Release().
 *
 * The following restriction does not apply to the EXTERNAL XRVideos which are currently exposed by this library,
 * but may apply to other modes once they are exposed: For those modes, the use of render locks
 * must happen sequentially in the order of how the locks were created, and must not overlap.
 * For example, if you create three render locks named A, B, and C, in that order, then the code may first use
 * render lock A only, but not B or C. Once render lock A will not be used anymore, the code may use render lock B,
 * but not C. Once render lock B will not be used anymore, render lock C may be used. (However, release of the
 * render locks must be delayed until the GPU is done with using their content.) Please keep this potential
 * future restriction in mind when writing code that uses render locks that may be changed from EXTERNAL mode
 * to the built-in video display modes in the future.
 *
 * @param video The XRVideo to operate on.
 * @return On success, returns an opaque handle to the created render lock, on error, returns null.
 */
SCANNEDREALITY_VIEWER_API
SRPlayer_XRVideoRenderLock* SRPlayer_XRVideo_PrepareFrame(SRPlayer_XRVideo* video);

/**
 * Releases the given render lock.
 *
 * @param renderLock The render lock to release.
 */
SCANNEDREALITY_VIEWER_API
void SRPlayer_XRVideoRenderLock_Release(SRPlayer_XRVideoRenderLock* renderLock);

/** Groups together information queried from an EXTERNAL XRVideo render lock. */
typedef struct SRPlayer_XRVideoRenderLock_External_Data {
  /**
   * The frame user data of the current frame's keyframe.
   * If the current frame is a keyframe itself, then keyframeUserData is equal to currentFrameUserData.
   */
  void* keyframeUserData;
  
  /**
   * The frame user data of the previous frame.
   * This is only provided if the previous frame's deformation state is necessary to
   * compute the current frame's deformation, i.e., if the current frame is not a keyframe.
   * Otherwise, this is null.
   */
  void* previousFrameUserData;
  
  /**
   * The frame user data of the current frame.
   */
  void* currentFrameUserData;
  
  /**
   * The time at which the video was locked, in the range from 0 (inclusive) to 1 (exclusive),
   * where 0 corresponds to the start of the current frame,
   * and 1 would correspond to the start of the next frame.
   */
  float currentIntraFrameTime;
} SRPlayer_XRVideoRenderLock_External_Data;

/**
 * For render locks obtained from an XRVideo allocated in EXTERNAL mode,
 * gets the data contained in the lock, enabling the external code to display the frame.
 *
 * @param renderLock The render lock to query.
 * @param data Pointer to a struct that will be filled with the data from the queried render lock. The struct does not need to be pre-initialized in any way.
 * @return SRV_TRUE on success, SRV_FALSE if an invalid render lock was passed in.
 */
SCANNEDREALITY_VIEWER_API
SRBool32 SRPlayer_XRVideoRenderLock_External_GetData(SRPlayer_XRVideoRenderLock* renderLock, SRPlayer_XRVideoRenderLock_External_Data* data);

#ifdef __cplusplus
}
#endif
