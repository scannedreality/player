/**
 * ScannedReality player library: JavaScript API.
 */
(function(root, factory) {  // eslint-disable-line
  if (typeof define === 'function' && define.amd) {
    // AMD. Register as an anonymous module.
    define([], factory);
  } else {
    // Browser globals
    root.scannedreality = factory();
  }
}(this, function() {
  "use strict";
  
  
  /**
   * Helper function to dynamically load a JavaScript file.
   * Returns a promise that resolves when the script is loaded and functions from it may be called.
   * @param {string} src Source attribute for the script tag to dynamically insert.
   */
  function loadScript(src) {
    return new Promise(function(resolve, reject) {
      let script = document.createElement('script');
      script.src = src;
      
      script.onload = () => resolve(script);
      script.onerror = () => reject(new Error(`Failed to load script ${src}`));
      
      document.head.append(script);
    });
  }
  
  
  /**
   * Initializes the ScannedReality player module.
   * @param {string} libUri
   *     URI of the directory containing the ScannedReality library files.
   *     If non-empty, must end with a slash '/'.
   * @param {Element} renderCanvas The HTML canvas element to use for rendering.
   * @return {Promise} A promise resolving to the initialized module.
   */
  function initialize(libUri, renderCanvas) {
    if (!crossOriginIsolated) {
      return new Promise((resolve, reject) => {
        throw new Error("Cross-origin isolation is not activated");
      });
    }
    
    return loadScript(libUri + "wasm-feature-detect-umd.js")
      .then((script) => {
        return Promise.all([wasmFeatureDetect.threads(), wasmFeatureDetect.simd()]);
      })
      .then((res) => {
        const threadsSupported = res[0];
        const simdSupported = res[1];
        
        if (!threadsSupported) {
          throw new Error("Browser is missing support for WASM threads");
        }
        
        const libFile = simdSupported ? "scannedreality-player-simd.js" : "scannedreality-player.js";
        return loadScript(libUri + libFile);
      })
      .then((script) => {
        // Call a factory function (created with emscripten's MODULARIZE option) from the newly
        // loaded script to create an instance of our WASM module.
        // See: https://emscripten.org/docs/getting_started/FAQ.html#how-can-i-tell-when-the-page-is-fully-loaded-and-it-is-safe-to-call-compiled-functions
        return createScannedRealityViewerModule({ canvas: renderCanvas });
      })
      .then((module) => {
        // Here, we can use the functions from our WASM module
        const canvasSelector = '#' + renderCanvas.id;
        let canvasSelectorPtr = module.allocateUTF8(canvasSelector);
        if (module._ScannedReality_initialize(canvasSelectorPtr) == 0) {
          module._free(canvasSelectorPtr);
          throw new Error("Failed to initialize the library");
        }
        module._free(canvasSelectorPtr);
        
        return module;
      });
  }
  
  
  /**
   * Stores common resources, such as shaders, that are required to display XRVideos.
   */
  class XRVideoCommonResources {
    /**
     * Attempts to allocate a common resources object.
     * See newVideo() for documentation on the parameter(s).
     * Call isInitialized() to check whether initialization was successful.
     */
    constructor(module) {
      this.module = module;
      this.cObject = module._XRVideoCommonResources_constructor();
    }
    
    /**
     * Checks whether the resources were allocated successfully.
     * @return {boolean} true if the resources were allocated successfully, false otherwise.
     */
    isInitialized() {
      return this.cObject !== null;
    }
    
    /**
     * Releases the allocated resources.
     */
    destroy() {
      this.module._XRVideoCommonResources_destroy(this.cObject);
    }
  }
  
  /**
   * Allocates common resources, such as shaders, that are required to play back ScannedReality videos.
   * @param module The module returned by a successful call to initialize().
   * @return {XRVideoCommonResources}
   *     Allocated XRVideoCommonResources object; call isInitialized() on it to check for success.
   */
  function newVideoCommonResources(module) {
    return new XRVideoCommonResources(module);
  }
  
  
  /**
   * Defines the looping behavior of video playback.
   */
  const XRVideoPlaybackMode = Object.freeze({
    /**
     * Plays the video once, then stops at the end.
     */
    SingleShot : 0,
    
    /**
     * Plays the video in a loop.
     */
    Loop : 1,
    
    /**
     * First plays the video normally, then continues playing it backwards when reaching the end,
     * then starts to play forward again, etc.
     */
    BackAndForth : 2});
  
  /**
   * Defines the state of asynchronous XRVideo loading.
   */
  const XRVideoAsyncLoadState = Object.freeze({
    /**
     * Asynchronous loading is in progress.
     * In this state, the file metadata, index, and playback state *must not* be accessed by user code.
     */
    Loading : 0,
    
    /**
     * There was an error during asynchronous loading.
     * The video thus cannot be displayed.
     */
    Error : 1,
    
    /**
     * Asynchronous loading has finished.
     * Please note that even if a video is in this state, it still may not display a frame,
     * since another asynchronous process (frame decoding) also has to run for this.
     */
    Ready : 2});
  
  /**
   * Represents an XRVideo, handling video decoding and rendering.
   */
  class XRVideo {
    /**
     * Attempts to allocate a video object.
     * See newVideo() for documentation on the parameters.
     * Call isInitialized() to check whether initialization was successful.
     */
    constructor(cachedDecodedFrameCount, verboseDecoding, videoCommonResources) {
      this.module = videoCommonResources.module;
      this.loaded = false;
      this.cachedDecodedFrameCount = cachedDecodedFrameCount;
      this.cObject =
          this.module._XRVideo_constructor(cachedDecodedFrameCount, verboseDecoding, videoCommonResources.cObject);
      this.videoBufferOnHeap = null;
      this.oldVideoBufferOnHeap = null;
    }
    
    /**
     * Checks whether the video object was allocated successfully.
     * @return {boolean} true if the video object was allocated successfully, false otherwise.
     */
    isInitialized() {
      return this.cObject !== null;
    }
    
    /**
     * Releases the allocated resources.
     * TODO: This does not work yet in the JavaScript API due to complications with blocking
     *       in the main thread of browser pages. If you require this functionality,
     *       then please contact us to let us know to invest work there.
     *       Note that loading a new video may be accomplished by calling load() again.
     *       This is more efficient than destructing the old video and allocating a new one,
     *       since some resources can remain allocated this way.
     */
    /*destroy() {
      this.module._XRVideo_destroy(this.cObject);
      if (this.videoBufferOnHeap !== null) {
        this.module._free(this.videoBufferOnHeap)
      }
    }*/
    
    /**
     * Loads a video from an ArrayBuffer.
     *
     * This function may be called more than once. Calls after the initial one will make the video object
     * switch from its current to the provided new video. However, before making a repeated call, it must
     * be ensured that switchedToMostRecentVideo() returns true. Otherwise, background threads may still
     * be accessing a buffer that would get deleted. load() thus won't switch the video in this case.
     *
     * @param {ArrayBuffer} videoBuffer An ArrayBuffer containing the .xrv file content of the video to load.
     * @param {number} initialPlaybackMode
     *     The initial playback mode for the video, one of:
     *     XRVideoPlaybackMode.SingleShot, XRVideoPlaybackMode.Loop, or XRVideoPlaybackMode.BackAndForth.
     *     Note that using BackAndForth is only recommended when caching all video frames,
     *     otherwise playback may be very choppy.
    */
    load(videoBuffer, initialPlaybackMode) {
      const cacheAllFrames = this.cachedDecodedFrameCount == 0;
      
      if (this.loaded && !this.switchedToMostRecentVideo()) {
        // Cannot switch yet, see the comment above
        return;
      }
      
      // Copy the video buffer to the WASM heap.
      // It must remain accessible while the WASM side may be loading frames in the background.
      // (Direct access of the WASM side to JavaScript-allocated memory does not seem to be
      //  possible at the time of writing.)
      this.oldVideoBufferOnHeap = this.videoBufferOnHeap;
      this.videoBufferOnHeap = this.module._malloc(videoBuffer.byteLength);
      // TODO: Verify that accessing HEAPU8 here (and in the two instances below) is safe. With pthreads, one might need to check
      //       for whether the heap has grown (see the last item in: https://emscripten.org/docs/porting/pthreads.html#special-considerations).
      //       But GROWABLE_HEAP is not accessible anymore, and neither is the function to check for growth.
      //       Maybe it is fine since we do the relevant malloc() just above, in the same thread as accessing the heap.
      //       Should also become irrelevant once growable array buffers are turned on by default: https://github.com/emscripten-core/emscripten/pull/24684
      this.module.HEAPU8.set(new Uint8Array(videoBuffer), this.videoBufferOnHeap);
      
      this.module._XRVideo_load(
          this.cObject, this.videoBufferOnHeap, videoBuffer.byteLength, cacheAllFrames, initialPlaybackMode);
      
      this.loaded = true;
    }
    
    /**
     * When a video is loaded, the video attributes will be loaded asynchronously.
     * getAsyncLoadState() must be used to determine whether this asynchronous loading
     * process is still in progress, whether it is finished, or whether it resulted in an error.
     * 
     * @return {number}
     *     The current asynchronous loading state of the video, one of:
     *     XRVideoAsyncLoadState.Loading, XRVideoAsyncLoadState.Error, or XRVideoAsyncLoadState.Ready.
     */
    getAsyncLoadState() {
      return this.module._XRVideo_getAsyncLoadState(this.cObject);
    }
    
    /**
     * If load() is called a second or further time on a given video object, the object will
     * switch to the new video in the background. The function switchedToMostRecentVideo()
     * may be used to query whether this switch has completed, and a frame from the new
     * video can be displayed.
     * 
     * This function will also clean up old memory in this case as a side effect.
     * Thus, it may be helpful to call this function regularly if switching between videos.
     *
     * @return {boolean} true if the video object has switched to the video given with the
     *                   most recent call to load(), and a frame from it has been decoded
     *                   and can be displayed; false otherwise.
     */
    switchedToMostRecentVideo() {
      const switchComplete = this.module._XRVideo_switchedToMostRecentVideo(this.cObject);
      
      // Take this opportunity for cleanup
      if (switchComplete && this.oldVideoBufferOnHeap !== null) {
        this.module._free(this.oldVideoBufferOnHeap);
        this.oldVideoBufferOnHeap = null;
      }
      
      return switchComplete;
    }
    
    /**
     * Returns whether load() has been called.
     * @return {boolean} true if load() has been called already on this video object, false otherwise.
     */
    isLoaded() {
      return this.loaded;
    }
    
    /**
     * Advances the video playback by the given elapsed time in seconds,
     * unless the video is currently in the "buffering" state
     * (meaning that not enough video frames have been decoded in the background yet
     * to allow for starting playback). In that case, discards the update.
     * The buffering state may be queried with isBuffering().
     * @param {number} elapsedSeconds The duration by which to advance the video playback, in seconds.
     */
    update(elapsedSeconds) {
      this.module._XRVideo_update(this.cObject, elapsedSeconds);
    }
    
    /**
     * Computes the animation state for the current playback timestamp,
     * and if a frame is ready for display, returns a render lock that is required to render the video.
     * After rendering, you must delete the render lock with destroyRenderLock().
     *
     * prepareRenderLock() may do offscreen rendering to perform computations on the GPU, therefore the
     * OpenGL render target states (framebuffer, viewport, ...) must be reset after calling this!
     * 
     * @return {XRVideoRenderLock} A render lock if a frame is ready to be rendered, null otherwise.
     */
    prepareRenderLock() {
      return this.module._XRVideo_prepareRenderLock(this.cObject);
    }
    
    /**
     * Renders the current video frame.
     * @param {Float32Array} modelViewMatrix
     *     The desired model-view matrix (of size 4 x 4) for the rendering, as an array of 16 floats
     *     in column-major storage order.
     * @param {Float32Array} modelViewProjectionMatrix
     *     The desired model-view-projection matrix (of size 4 x 4) for the rendering, as an array
     *     of 16 floats in column-major storage order.
     * @param {boolean} useSurfaceNormalShading
     *     Whether to display the mesh in surface-normal-based false color shading.
     * @param {XRVideoRenderLock} renderLock
     *     The render lock created by the last call to prepareRenderLock().
     */
    render(modelViewMatrix, modelViewProjectionMatrix, useSurfaceNormalShading, renderLock) {
      let modelViewPtr = this.module._malloc(modelViewMatrix.byteLength);
      this.module.HEAPU8.set(new Uint8Array(modelViewMatrix.buffer, 0, 64), modelViewPtr);
      
      let modelViewProjectionPtr = this.module._malloc(modelViewProjectionMatrix.byteLength);
      this.module.HEAPU8.set(new Uint8Array(modelViewProjectionMatrix.buffer, 0, 64), modelViewProjectionPtr);
      
      this.module._XRVideo_render(modelViewPtr, modelViewProjectionPtr, useSurfaceNormalShading, renderLock);
      
      this.module._free(modelViewProjectionPtr);
      this.module._free(modelViewPtr);
    }
    
    /**
     * Destroys the given render lock.
     * @param {XRVideoRenderLock} renderLock
     *     The render lock to destroy.
     */
    destroyRenderLock(renderLock) {
      this.module._XRVideo_destroyRenderLock(renderLock);
    }
    
    /**
     * Sets the video's playback mode.
     * @param {number} playbackMode
     *     The playback mode for the video, one of:
     *     XRVideoPlaybackMode.SingleShot, XRVideoPlaybackMode.Loop, or XRVideoPlaybackMode.BackAndForth.
     *     - Note that using BackAndForth is only recommended when caching all video frames, otherwise
     *       playback may be very choppy.
     *     - Note that changing the playback mode to SingleShot or Loop while the video was playing backwards
     *       in mode BackAndForth will make video playback stop at the start of the video. To prevent this,
     *       also call seek() with the direction set to forward.
     */
    setPlaybackMode(playbackMode) {
      this.module._XRVideo_setPlaybackMode(this.cObject, playbackMode);
    }
    
    /**
     * Returns the start timestamp of the video.
     * Notice that this may differ from zero!
     * This function must only be called after getAsyncLoadState() has returned AsyncLoadState.Ready.
     * @return {number} The video start timestamp, measured in seconds.
     */
    getStartTimestamp() {
      return this.module._XRVideo_getStartTimestamp(this.cObject);
    }
    
    /**
     * Returns the end timestamp of the video.
     * This function must only be called after getAsyncLoadState() has returned AsyncLoadState.Ready.
     * @return {number} The video end timestamp, measured in seconds.
     */
    getEndTimestamp() {
      return this.module._XRVideo_getEndTimestamp(this.cObject);
    }
    
    /**
     * Returns the current playback timestamp of the video.
     * This function must only be called after getAsyncLoadState() has returned AsyncLoadState.Ready.
     * @return {number} The current playback timestamp, measured in seconds.
     */
    getPlaybackTimestamp() {
      return this.module._XRVideo_getPlaybackTimestamp(this.cObject);
    }
    
    /**
     * Seeks the video to the given timestamp.
     * Note that the video may not immediately display the seeked-to frame because that frame may
     * first need to be decoded (which happens in the background).
     * This function must only be called after getAsyncLoadState() has returned AsyncLoadState.Ready.
     * @param {number} timestamp The timestamp to seek to, in seconds.
     * @param {boolean} forward
     *     Specifies whether the video will play forward or backward starting from the seeked-to timestamp.
     */
    seek(timestamp, forward) {
      this.module._XRVideo_seek(this.cObject, timestamp, forward);
    }
    
    /**
     * Returns whether the video is currently in the "buffering" state,
     * meaning that not enough future video frames are cached to be able to play the video.
     * In this state, playback will remain at the same timestamp, even when calling update() with
     * a positive number of elapsed seconds, to allow for video frame decoding to catch up.
     * @return {boolean} True if the video is currently buffering, false if not.
     */
    isBuffering() {
      return this.module._XRVideo_isBuffering(this.cObject);
    }
    
    /**
     * If isBuffering() returns true, returns a (very) rough estimate of the buffering progress, in percent.
     * @return {number} A rough estimate of the buffering progress, in percent.
     */
    getBufferingProgressPercent() {
      return this.module._XRVideo_getBufferingProgressPercent(this.cObject);
    }
  }
  
  /**
   * Allocates a new ScannedReality video.
   * @param {number} cachedDecodedFrameCount
   *     The number of video frames to decode in advance and keep cached:
   *     - We suggest a value between 30 and 60 as a good 'default' value. A lower number will
   *       reduce memory consumption, but increase the chance of playback stuttering.
   *       A higher number will increase memory consumption, but reduce the chance of playback stuttering.
   *     - If cachedDecodedFrameCount is set to 0, all video frames will be cached; this is only
   *       recommended for very short video clips of less than 3 seconds because of the resulting
   *       large memory requirements, but may be helpful to make the whole video clip play smoothly
   *       on slow devices (after an initial loading time).
   *     - Note that for iOS / Safari, we limit the maximum WebAssembly memory used to 1GB,
   *       as larger maximum sizes do not seem to be supported by these.
   *       Thus, please make sure not to use too much memory when targeting these.
   *       See: https://github.com/ffmpegwasm/ffmpeg.wasm/issues/299#issuecomment-1121507264
   * @param {boolean} verboseDecoding
   *     If true, verbose log messages including timings will be output during video decoding.
   * @param {XRVideoCommonResources} videoCommonResources
   *     A 'common resources' object obtained from newVideoCommonResources().
   * @return {XRVideo} Allocated XRVideo object; call isInitialized() on it to check for success.
   */
  function newVideo(cachedDecodedFrameCount, verboseDecoding, videoCommonResources) {
    return new XRVideo(cachedDecodedFrameCount, verboseDecoding, videoCommonResources);
  }
  
  
  /**
   * Define the publicly exported members
   */
  return {
    initialize: initialize,
    newVideoCommonResources: newVideoCommonResources,
    XRVideoPlaybackMode: XRVideoPlaybackMode,
    XRVideoAsyncLoadState: XRVideoAsyncLoadState,
    newVideo: newVideo,
  };

}));
