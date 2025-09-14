# ScannedReality Web Player

This is the source code of the web player for [ScannedReality](https://scanned-reality.com/)'s XRVideo volumetric video file format.

![Screenshot](https://github.com/scannedreality/player/blob/main/dist/scannedreality-player-library-web/app/screenshot.png)


## Building

Building has been tested on Ubuntu 22.04. It should work similarly on other systems. The main requirement is the [emscripten toolchain](https://emscripten.org/) (see its [installation instructions](https://emscripten.org/docs/getting_started/downloads.html)). Building has been verified to work with emscripten 4.0.14.

This repository uses submodules. Before building the project, make sure that you have checked them out, for example by using the `--recurse-submodules` flag for `git clone`, or for an existing clone by running:
```
git submodule update --init --recursive
```

Create and enter the build directory:
```
mkdir -p build/emscripten
cd build/emscripten
```

Configure the build using emscripten's `emcmake`:
```
emcmake cmake ../..
```
If you have installed CMake as an Ubuntu Snap package, then you may want to use `/snap/bin/cmake` for the CMake path above.

Finally, build all the web player targets:
```
emmake make -j scannedreality-player scannedreality-player-simd scannedreality-player-app scannedreality-player-app-simd
```

These build targets are:

* **scannedreality-player**: WASM module for the JavaScript player module. Deployed to dist/scannedreality-player-library-web/lib. Compiled without WASM SIMD.
* **scannedreality-player-simd**: Variant of the above, compiled with WASM SIMD.

* **scannedreality-player-app**: WASM module for the web viewer app (that contains basic playback UI). Deployed to dist/scannedreality-player-library-web/app. Compiled without WASM SIMD.
* **scannedreality-player-app-simd**: Variant of the above, compiled with WASM SIMD.

After a successful build, the binaries (.js, .wasm, and .worker.js files) are deployed into the dist/scannedreality-player-library-web/app and dist/scannedreality-player-library-web/lib subfolders. The folder dist/scannedreality-player-library-web corresponds to the final JavaScript player module that is published on https://scanned-reality.com/downloads.


## Running and deploying

For using the built JavaScript player module, please refer to the separate Readme file in the module's subfolder [dist/scannedreality-player-library-web](https://github.com/scannedreality/player/tree/main/dist/scannedreality-player-library-web).


## License

The ScannedReality Player sources are provided under the three-clause BSD license given below. Please note that the player also uses third-party code, some of which is packaged in this repository, which may have different (permissive) licenses.

```
Copyright (c) ScannedReality GmbH.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
