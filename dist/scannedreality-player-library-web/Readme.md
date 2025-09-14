# ScannedReality Player Library for Javascript / WebGL #

This library enables displaying ScannedReality's videos on the web with WebGL 2.0,
via a JavaScript API.


## Examples and app ##

This package contains the following examples:

* **Interactive example** at `examples/interactive`:
  This example demonstrates the use of every function in the API,
  providing an interactive user interface to try it out in the browser.
* **Simple example** at `examples/simple`:
  This example demonstrates how to display a video with minimal code,
  providing a good base for starting to integrate the library into your own project.
* **WebXR example** at `examples/webXR`:
  This example demonstrates how to display a video in AR or VR using WebXR.
  *Please notice:* At the time of writing, browser support for WebXR was still very limited.
  The example was tested with Chrome on Android and with the Meta Quest Browser on Meta Quest 2.
  iOS did not support WebXR yet.
  See here for the current support status: https://caniuse.com/webxr

Furthermore, in the `app` folder, there is code for using our ready-made 'app' to
display a video. This is the same app that we at ScannedReality use to show our
demos at: https://scanned-reality.com/demo

This app comes with 3D navigation and video playback controls, as well as a license notice
display for the third-party licenses used in the player. However, it currently does not
allow for customization, except for exchanging the video file (see the "Preparing videos for
the ready-made app" section below) and video title (see `ENV.VIDEO_TITLE` in `app/app.html`).


### Running the examples ###

Directly opening the example HTML files in a browser will not work.
This is because web browsers permit the use of certain features for websites only if the
website is served in a secure way. The ScannedReality Player Library uses such features
in order to achieve good playback performance. Thus, for viewing the examples, it is
required to serve the example HTML pages via a suitably configured (local) web server.

For testing purposes, we provide a small Python script, `run_example.py`, which starts
a local web server with suitable configuration. To use it, first make sure that Python 3
is installed on your PC. Open a terminal respectively command prompt and change to the
directory which contains the script file. Then, run the script:
```
python3 run_example.py
```
Depending on your system, the Python executable may also be named `python` instead
of `python3`. The server may be exited by pressing Ctrl-C in the terminal.

By default, the server will listen on port 8000. Thus, to view the examples, open a web
browser and copy the following into its address bar while the local web server is running:

For the interactive example: `http://localhost:8000/examples/interactive/example.html`<br/>
For the simple example: `http://localhost:8000/examples/simple/example.html`<br/>
For the app: `http://localhost:8000/app/app.html`

#### Access from a different device ####

Accessing the examples from another device, for example from a mobile device,
may require additional steps. This is because browsers treat the address `http://localhost`
as a special case and may require an HTTPS connection in order to grant the same features
to websites on other hosts.

Because of this, we also include an option to use HTTPS in our `run_example.py` script.
To use this option, first create a self-signed TLS certificate.
For example, follow these instructions:
https://web.dev/how-to-use-local-https

This will result in a certificate file and a key file. Move them into the same directory
as the `run_example.py` script, and name the certificate file "cert.pem" and the
key file "key.pem". If you start the script from this directory now, it should use
HTTPS, as indicated by the output "Using HTTPS (using cert.pem and key.pem)".
You may now access the examples via https, which should also work when used from other devices.

Note that a warning about the use of self-signed certificates will be shown on other devices,
unless you add the trusted authority for the self-signed certificate there.
For testing purposes, this warning may be ignored.


### Using the library - Technical details ###

#### Configuring your web server ####

As mentioned above, certain features may only be used in web browsers if a website
is served in a secure way. In particular, the ScannedReality Player Library requires
the website to be **cross-origin isolated** to be able to use WASM multithreading
via SharedArrayBuffer.

If you are interested in further details about this, see the note on top of this page:
https://emscripten.org/docs/porting/pthreads.html.

Activating cross-origin isolation requires the web server to send appropriate HTTP headers.
Essentially, the following HTTP headers must be sent:
```
Cross-Origin-Embedder-Policy: require-corp
Cross-Origin-Opener-Policy: same-origin
```
In addition, the site needs to be served securely via HTTPS, unless it is served
from http://localhost.

For further details on what cross-origin isolation does, see for example:
https://web.dev/coop-coep/

#### Deploying the files ####

To use the library, deploy **all** files from the `lib` directory to a directory on your
web server. In your call to `initialize()`, set `libUri` to the URI of this
directory, ending with a slash, or leave it empty if the files are in the same directory
as the script which calls them.

There is currently a version of the library with SIMD instructions (for Chrome and Firefox),
and a version without SIMD instructions (for older versions of iOS / Safari, which do not
support WASM SIMD instructions). The library will automatically load the best suitable file
for the user's browser.


### Preparing videos for the ready-made app ###

The app provided in the `app` directory loads the videos from data files that may be
packaged using emscripten's `file_packager` tool. To do so, first copy the desired xrv
video file into the directory `app/asset_dir`, and name the video file `video.xrv`.
Then, run the following from within the `app` directory, inserting your
emscripten installation directory:

```
<...>/upstream/emscripten/tools/file_packager file.data --js-output=file.js --preload asset_dir@/
```

This creates the two files `file.data` and `file.js` within the `app` directory,
which will contain both the video file and the required DroidSans font resource file.


## Documentation ##

The HTML files of the provided examples contain many verbose comments, explaining
how to use the API. Furthermore, there are documentation comments for the JavaScript API
in `lib/scannedreality-player-api.js`. If you have any questions that are not answered by
this, feel free to contact us at ScannedReality at contact@scanned-reality.com - we will
try our best to help!


## Requirements ##

The library requires the following notable browser features to work:

* WebGL 2.0: https://caniuse.com/webgl2
* WebAssembly (WASM): https://caniuse.com/wasm
* WASM Threads via SharedArrayBuffer: https://caniuse.com/sharedarraybuffer
  * Requires cross-origin isolation: https://caniuse.com/mdn-api_crossoriginisolated

All commonly used browsers (Chrome, Firefox, Safari) support all of these features.


## Video compatibility ##

This library is compatible with all .xrv files except those created with lossless RGB
texture data. Due to their file size, these are impractical to deploy on the web,
which is why we believe that this limitation is irrelevant in practice.
If you have a use case for this and want us to work on removing this restriction,
then please let us know at: contact@scanned-reality.com


## Files Overview ##

The following gives an overview of what the individual files in this package do:

* **app**
  * **asset_dir**
    * **resources**
      * **fonts**
        * **droid-sans**
          * **Apache License.txt**: License file for the DroidSans font file.
          * **DroidSans.ttf**: Font file used by the player app.
  * **app.html**: Markup file for the player app.
  * **file.data**: Example data file for the player app, generated by the emscripten `file_packager` tool.
  * **file.js**: Example data runtime file for the player app, generated by the emscripten `file_packager` tool.
  * **scannedreality-player-app-simd.js**: Viewer app file generated by emscripten. Variant with SIMD instructions.
  * **scannedreality-player-app-simd.wasm**: Viewer app file generated by emscripten. Variant with SIMD instructions.
  * **scannedreality-player-app-simd.worker.js**: Viewer app file generated by emscripten. Variant with SIMD instructions.
  * **scannedreality-player-app.js**: Viewer app file generated by emscripten.
  * **scannedreality-player-app.wasm**: Viewer app file generated by emscripten.
  * **scannedreality-player-app.worker.js**: Viewer app file generated by emscripten.
  * **screenshot.png**: A screenshot showing how the app looks like.
* **example_videos**
  * **example1.xrv**: Example video clip 1 in ScannedReality's custom xrv (XRVideo) format.
  * **example2.xrv**: Example video clip 2 in ScannedReality's custom xrv (XRVideo) format.
* **examples**
  * **interactive**
    * **example.html**: Markup and code for the interactive example.
    * **Readme.md**: Description of the interactive example.
    * **screenshot.png**: A screenshot showing how the example looks like.
  * **simple**
    * **example.html**: Markup and code for the simple example.
    * **Readme.md**: Description of the simple example.
    * **screenshot.png**: A screenshot showing how the example looks like.
  * **webXR**
    * **example.html**: Markup and code for the WebXR example.
    * **Readme.md**: Description of the WebXR example.
* **lib**
  * **scannedreality-player-api.js**: Provides the JavaScript API of the player library. Include this file in your code to use the library. This file contains documentation comments for the API.
  * **scannedreality-player-simd.js**: File generated by emscripten. Variant with SIMD instructions.
  * **scannedreality-player-simd.wasm**: File generated by emscripten. Variant with SIMD instructions.
  * **scannedreality-player-simd.worker.js**: File generated by emscripten. Variant with SIMD instructions.
  * **scannedreality-player.js**: File generated by emscripten. Provides the web runtime for the part of the player library that is compiled from C++. Not to be included directly.
  * **scannedreality-player.wasm**: File generated by emscripten. Contains the core player library code as WASM, compiled from C++. Not to be included directly.
  * **scannedreality-player.worker.js**: File generated by emscripten. Provides the runtime for WASM threads. Not to be included directly.
  * **wasm-feature-detect-umd.js**: Third-party "wasm-feature-detect" library that is used by the player library, in UMD module format. The official repository for this library is at: https://github.com/GoogleChromeLabs/wasm-feature-detect
* **licenses**
  * **dav1d.txt**: License for the third-party component "dav1d" (https://code.videolan.org/videolan/dav1d).
  * **eigen.txt**: License for the third-party component "Eigen" (https://eigen.tuxfamily.org/index.php?title=Main_Page).
  * **filesystem.txt**: License for the third-party component "filesystem" (https://github.com/gulrak/filesystem).
  * **loguru.txt**: License for the third-party component "loguru" (https://github.com/emilk/loguru).
  * **sdl.txt**: License for the third-party component "SDL" (http://www.libsdl.org/).
  * **sophus.txt**: License for the third-party component "Sophus" (https://github.com/strasdat/Sophus).
  * **utf8decoder.txt**: License for the third-party component "Flexible and Economical UTF-8 Decoder" (https://bjoern.hoehrmann.de/utf-8/decoder/dfa/).
  * **wasm-feature-detect.txt**: License for the third-party component "wasm-feature-detect" (https://github.com/GoogleChromeLabs/wasm-feature-detect).
  * **zstd.txt**: License for the third-party component "zstd" (https://github.com/facebook/zstd).
* **util**
  * **m4.js**: Third-party library with helper functions for matrices used in WebGL rendering. Used by the examples only - not required for the player library itself. Taken from: https://github.com/gfxfundamentals/webgl-fundamentals
* **Readme.md**: The Readme file that you are reading at the moment.
* **run_example.py**: Helper script to run a local webserver for running the examples and app.


## Licenses ##

This library uses a number of third-party open source components, having permissive licenses.
If you use and/or deploy the library, you are responsible for citing their licenses as required
in the appropriate places. Please see the `licenses` folder for these license texts.
