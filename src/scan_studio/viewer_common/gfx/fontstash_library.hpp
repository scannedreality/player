// --------------------------------------------------------------------------
// THIS IS AN ALTERED SOURCE VERSION, NOT TO BE CONFUSED WITH THE ORIGINAL
// SOFTWARE, WHOSE LICENSE IS STATED BELOW:
// --------------------------------------------------------------------------
//
// Copyright (c) 2009-2013 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//  claim that you wrote the original software. If you use this software
//  in a product, an acknowledgment in the product documentation would be
//  appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//  misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//
// --------------------------------------------------------------------------
// Notice: This is an altered source version according to point 2 above.

#pragma once

#include <vector>

#include <libvis/io/filesystem.h>

#include <libvis/vulkan/libvis.h>

namespace scan_studio {
using namespace vis;

#define FONS_INVALID -1

enum FONSflags {
  FONS_ZERO_TOPLEFT = 1,
  FONS_ZERO_BOTTOMLEFT = 2,
};

enum FONSalign {
  // Horizontal align
  FONS_ALIGN_LEFT      = 1<<0,  // Default
  FONS_ALIGN_CENTER    = 1<<1,
  FONS_ALIGN_RIGHT     = 1<<2,
  // Vertical align
  FONS_ALIGN_TOP       = 1<<3,
  FONS_ALIGN_MIDDLE    = 1<<4,
  FONS_ALIGN_BOTTOM    = 1<<5,
  FONS_ALIGN_BASELINE  = 1<<6, // Default
};

enum FONSerrorCode {
  // Font atlas is full.
  FONS_ATLAS_FULL = 1,
  // Scratch memory used to render glyphs is full, requested size reported in 'val', you may need to bump up FONS_SCRATCH_BUF_SIZE.
  FONS_SCRATCH_FULL = 2,
  // Calls to fonsPushState has created too large stack, if you need deep state stack bump up FONS_MAX_STATES.
  FONS_STATES_OVERFLOW = 3,
  // Trying to pop too many states fonsPopState().
  FONS_STATES_UNDERFLOW = 4,
};

struct FONSVertex {
  float x, y;
  float tx, ty;
  u32 rgba;
};

struct FONSparams {
  int width, height;
  unsigned char flags;
  void* userPtr;
  int (*renderCreate)(void* uptr, int width, int height, int textureIndex);
  int (*renderResize)(void* uptr, int width, int height, int textureIndex);
  void (*renderUpdate)(void* uptr, int* rect, const unsigned char* data);
  // void (*renderDraw)(void* uptr, const FONSVertex* verts, int nverts, int textureIndex);
  void (*renderDelete)(void* uptr);
};

struct FONSquad {
  float x0,y0,s0,t0;
  float x1,y1,s1,t1;
};

struct FONStextIter {
  float x, y, nextx, nexty, scale, spacing, scaling;
  bool roundToInt;
  unsigned int codepoint;
  short isize, iblur;
  struct FONSfont* font;
  int prevGlyphIndex;
  const char* str;
  const char* next;
  const char* end;
  unsigned int utf8state;
};

struct FONSBatch {
  inline FONSBatch(int firstVertex, int textureId)
      : firstVertex(firstVertex),
        textureId(textureId) {}
  
  /// Index of the first vertex in this batch
  /// (the last vertex is defined as either the vertex before the next batch's first vertex,
  ///  or the last vertex in the vertices vector in case there is no following batch).
  int firstVertex;
  
  /// The ID of the texture that must be used to render this batch of vertices.
  int textureId;
};

/// A chunk of vertices, split into one or more batches.
struct FONSText {
  vector<FONSVertex> vertices;
  vector<FONSBatch> batches;
};

struct FONScontext;

// Contructor and destructor.
FONScontext* fonsCreateInternal(FONSparams* params);
void fonsDeleteInternal(FONScontext* s);

void fonsSetErrorCallback(FONScontext* s, void (*callback)(void* uptr, int error, int val), void* userPtr);
// Returns current atlas size.
void fonsGetAtlasSize(FONScontext* s, int* width, int* height);
// Expands the atlas size.
int fonsExpandAtlas(FONScontext* s, int width, int height);
// Resets the whole stash.
int fonsResetAtlas(FONScontext* stash, int width, int height);

// Add fonts
int fonsAddFont(FONScontext* s, const char* name, const fs::path& path);
int fonsAddFontMem(FONScontext* s, const char* name, vector<u8>&& data);
int fonsGetFontByName(FONScontext* s, const char* name);
int fonsAddFallbackFont(FONScontext* stash, int base, int fallback);

// State handling
void fonsPushState(FONScontext* s);
void fonsPopState(FONScontext* s);
void fonsClearState(FONScontext* s);

// State setting
void fonsSetSize(FONScontext* s, float size);
void fonsSetColor(FONScontext* s, unsigned int color);
void fonsSetSpacing(FONScontext* s, float spacing);
void fonsSetBlur(FONScontext* s, float blur);
void fonsSetAlign(FONScontext* s, int align);
void fonsSetFont(FONScontext* s, int font);
/// Scaling only takes effect if rounding is disabled.
void fonsSetRoundingAndScaling(FONScontext* s, bool roundToInt, float scaling);

// Draw text:
/// 1. Create the vertices for the given text
float fonsCreateText(FONScontext* s, float x, float y, const char* str, const char* end, FONSText* text);
void fonsCreateTextBox(FONScontext* s, float minX, float minY, float maxX, float maxY, bool wordWrap, const char* str, const char* end, FONSText* text);
// TODO: We don't use the batching mechanism below anymore but instead render the result of fonsCreateText() directly now.
//       I left the code here for now (commented) since batching could make sense in theory to reduce draw calls.
//       (The reason we don't use it anymore now is to be able to pass a separate descriptor with a (potentially differing)
//        model-view-projection matrix to every text object; howver, that matrix could often still be shared among many text
//        objects that are in the same plane, for example).
/// 2. Batch the created vertices together with other texts rendered at the same time
// void fonsBatchText(FONScontext* s, const FONSText& text);
/// 3. Send out the batched vertices for actual rendering
// void fonsFlushRender(FONScontext* s);

// Measure text
float fonsTextBounds(FONScontext* s, float x, float y, const char* string, const char* end, float* bounds);
void fonsLineBounds(FONScontext* s, float y, float* miny, float* maxy);
void fonsVertMetrics(FONScontext* s, float* ascender, float* descender, float* lineh);

// Text iterator
int fonsTextIterInit(FONScontext* stash, FONStextIter* iter, float x, float y, const char* str, const char* end);
int fonsTextIterNext(FONScontext* stash, FONStextIter* iter, struct FONSquad* quad);

// Pull texture changes
const unsigned char* fonsGetTextureData(FONScontext* stash, int* width, int* height);
int fonsValidateTexture(FONScontext* s, int* dirty);

// Draws the stash texture for debugging
void fonsDrawDebug(FONScontext* s, float x, float y, FONSText* text);

}
