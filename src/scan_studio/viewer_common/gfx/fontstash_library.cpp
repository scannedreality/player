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

#include "scan_studio/viewer_common/gfx/fontstash_library.hpp"

#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOCOMM
  #include <Windows.h>  // for MAX_PATH
#endif

#define FONS_NOTUSED(v)  (void)sizeof(v)

#ifdef FONS_USE_FREETYPE

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H
#include <math.h>

namespace scan_studio {

struct FONSttFontImpl {
  FT_Face font;
};

static FT_Library ftLibrary;

static int fons__tt_init() {
  FT_Error ftError;
  FONS_NOTUSED(context);
  ftError = FT_Init_FreeType(&ftLibrary);
  return ftError == 0;
}

static int fons__tt_loadFont(FONScontext *context, FONSttFontImpl *font, unsigned char *data, int dataSize) {
  FT_Error ftError;
  FONS_NOTUSED(context);
  
  //font->font.userdata = stash;
  ftError = FT_New_Memory_Face(ftLibrary, (const FT_Byte*)data, dataSize, 0, &font->font);
  return ftError == 0;
}

static void fons__tt_getFontVMetrics(FONSttFontImpl *font, int *ascent, int *descent, int *lineGap) {
  *ascent = font->font->ascender;
  *descent = font->font->descender;
  *lineGap = font->font->height - (*ascent - *descent);
}

static float fons__tt_getPixelHeightScale(FONSttFontImpl *font, float size) {
  return size / (font->font->ascender - font->font->descender);
}

static int fons__tt_getGlyphIndex(FONSttFontImpl *font, int codepoint) {
  return FT_Get_Char_Index(font->font, codepoint);
}

static int fons__tt_buildGlyphBitmap(
    FONSttFontImpl *font, int glyph, float size, float scale,
    int *advance, int *lsb, int *x0, int *y0, int *x1, int *y1) {
  FT_Error ftError;
  FT_GlyphSlot ftGlyph;
  FT_Fixed advFixed;
  FONS_NOTUSED(scale);
  
  ftError = FT_Set_Pixel_Sizes(font->font, 0, (FT_UInt)(size * (float)font->font->units_per_EM / (float)(font->font->ascender - font->font->descender)));
  if (ftError) return 0;
  ftError = FT_Load_Glyph(font->font, glyph, FT_LOAD_RENDER);
  if (ftError) return 0;
  ftError = FT_Get_Advance(font->font, glyph, FT_LOAD_NO_SCALE, &advFixed);
  if (ftError) return 0;
  ftGlyph = font->font->glyph;
  *advance = (int)advFixed;
  *lsb = (int)ftGlyph->metrics.horiBearingX;
  *x0 = ftGlyph->bitmap_left;
  *x1 = *x0 + ftGlyph->bitmap.width;
  *y0 = -ftGlyph->bitmap_top;
  *y1 = *y0 + ftGlyph->bitmap.rows;
  return 1;
}

static void fons__tt_renderGlyphBitmap(
    FONSttFontImpl *font, unsigned char *output, int outWidth, int outHeight, int outStride,
    float scaleX, float scaleY, int glyph) {
  FT_GlyphSlot ftGlyph = font->font->glyph;
  int ftGlyphOffset = 0;
  int x, y;
  FONS_NOTUSED(outWidth);
  FONS_NOTUSED(outHeight);
  FONS_NOTUSED(scaleX);
  FONS_NOTUSED(scaleY);
  FONS_NOTUSED(glyph);  // glyph has already been loaded by fons__tt_buildGlyphBitmap
  
  for ( y = 0; y < ftGlyph->bitmap.rows; y++ ) {
    for ( x = 0; x < ftGlyph->bitmap.width; x++ ) {
      output[(y * outStride) + x] = ftGlyph->bitmap.buffer[ftGlyphOffset++];
    }
  }
}

static int fons__tt_getGlyphKernAdvance(FONSttFontImpl *font, int glyph1, int glyph2) {
  FT_Vector ftKerning;
  FT_Get_Kerning(font->font, glyph1, glyph2, FT_KERNING_DEFAULT, &ftKerning);
  return (int)((ftKerning.x + 32) >> 6);  // Round up and convert to integer
}

}

#else

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
static void* fons__tmpalloc(size_t size, void* up);
static void fons__tmpfree(void* ptr, void* up);
#define STBTT_malloc(x,u)    fons__tmpalloc(x,u)
#define STBTT_free(x,u)      fons__tmpfree(x,u)

#pragma GCC diagnostic push
#pragma clang diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-function"
#include "stb/stb_truetype.h"
#pragma GCC diagnostic pop
#pragma clang diagnostic pop

namespace scan_studio {

struct FONSttFontImpl {
  stbtt_fontinfo font;
};

static int fons__tt_init(FONScontext *context) {
  FONS_NOTUSED(context);
  return 1;
}

static int fons__tt_loadFont(FONScontext *context, FONSttFontImpl *font, unsigned char *data, int dataSize) {
  int stbError;
  FONS_NOTUSED(dataSize);
  
  font->font.userdata = context;
  stbError = stbtt_InitFont(&font->font, data, 0);
  return stbError;
}

static void fons__tt_getFontVMetrics(FONSttFontImpl *font, int *ascent, int *descent, int *lineGap) {
  stbtt_GetFontVMetrics(&font->font, ascent, descent, lineGap);
}

static float fons__tt_getPixelHeightScale(FONSttFontImpl *font, float size) {
  return stbtt_ScaleForPixelHeight(&font->font, size);
}

static int fons__tt_getGlyphIndex(FONSttFontImpl *font, int codepoint) {
  return stbtt_FindGlyphIndex(&font->font, codepoint);
}

static int fons__tt_buildGlyphBitmap(
    FONSttFontImpl *font, int glyph, float size, float scale,
    int *advance, int *lsb, int *x0, int *y0, int *x1, int *y1) {
  FONS_NOTUSED(size);
  stbtt_GetGlyphHMetrics(&font->font, glyph, advance, lsb);
  stbtt_GetGlyphBitmapBox(&font->font, glyph, scale, scale, x0, y0, x1, y1);
  return 1;
}

static void fons__tt_renderGlyphBitmap(
    FONSttFontImpl *font, unsigned char *output, int outWidth, int outHeight, int outStride,
    float scaleX, float scaleY, int glyph) {
  stbtt_MakeGlyphBitmap(&font->font, output, outWidth, outHeight, outStride, scaleX, scaleY, glyph);
}

static int fons__tt_getGlyphKernAdvance(FONSttFontImpl *font, int glyph1, int glyph2) {
  return stbtt_GetGlyphKernAdvance(&font->font, glyph1, glyph2);
}

}

#endif

namespace scan_studio {

constexpr int FONS_SCRATCH_BUF_SIZE = 64000;
constexpr int FONS_HASH_LUT_SIZE = 256;
constexpr int FONS_INIT_FONTS = 4;
constexpr int FONS_INIT_GLYPHS = 256;
constexpr int FONS_INIT_ATLAS_NODES = 256;
constexpr int FONS_MAX_STATES = 20;

static unsigned int fons__hashint(unsigned int a) {
  a += ~(a<<15);
  a ^=  (a>>10);
  a +=  (a<<3);
  a ^=  (a>>6);
  a += ~(a<<11);
  a ^=  (a>>16);
  return a;
}

struct FONSglyph {
  unsigned int codepoint;
  int index;
  int next;
  short size, blur;
  short x0,y0,x1,y1;
  short xadv,xoff,yoff;
};

struct FONSfont {
  FONSttFontImpl font;
  string name;
  vector<unsigned char> data;
  float ascender;
  float descender;
  float lineh;
  vector<FONSglyph> glyphs;
  int lut[FONS_HASH_LUT_SIZE];
  vector<int> fallbacks;
};

struct FONSstate {
  int font;
  int align;
  float size;
  unsigned int color;
  float blur;
  float spacing;
  
  /// Scaling applied to the font size when emitting rendered characters.
  /// The original font size setting specifies the rasterization size of the characters in pixels.
  /// With the scaling on top of this size, we can then specify the render size of the characters.
  /// This is important when the text is rendered in 3D, where a 1:1 mapping between
  /// font rasterization pixels and rendered pixels is not possible in general.
  /// The scaling thus represents the value "meters per font rasterization pixel" for 3D rendering
  /// if the 3D coordinates are measured in meters.
  float scaling = 1;
  bool roundToInt = true;
};

struct FONSatlasNode {
  inline FONSatlasNode(s16 x, s16 y, s16 width)
      : x(x), y(y), width(width) {}
  
  short x, y, width;
};

struct FONSatlas {
  int width, height;
  vector<FONSatlasNode> nodes;
};

struct FONScontext {
  FONSparams params;
  
  float itw,ith;
  vector<u8> texData;
  int dirtyRect[4];
  vector<FONSfont*> fonts;
  FONSatlas* atlas;
  /// Index starting from zero that is sequentially increased every time the atlas gets reset.
  int currentTextureIndex = 0;
  
  vector<u8> scratch;
  int nscratch;
  
  FONSstate states[FONS_MAX_STATES];
  int nstates;
  
  /// Pairs of (textureIndex, vector of batched vertices)
  vector<pair<int, vector<FONSVertex>>> batchedVertices;
  
  void (*handleError)(void* userPtr, int error, int val);
  void* errorUserPtr;
};

}

#ifdef STB_TRUETYPE_IMPLEMENTATION
static void* fons__tmpalloc(size_t size, void* up) {
  unsigned char* ptr;
  scan_studio::FONScontext* stash = (scan_studio::FONScontext*)up;

  // 16-byte align the returned pointer
  size = (size + 0xf) & ~0xf;

  if (stash->nscratch + (int)size > scan_studio::FONS_SCRATCH_BUF_SIZE) {
    if (stash->handleError)
      stash->handleError(stash->errorUserPtr, scan_studio::FONS_SCRATCH_FULL, stash->nscratch+(int)size);
    return NULL;
  }
  ptr = stash->scratch.data() + stash->nscratch;
  stash->nscratch += (int)size;
  return ptr;
}

static void fons__tmpfree(void* ptr, void* up) {
  (void)ptr;
  (void)up;
  // empty
}
#endif

namespace scan_studio {

// Copyright (c) 2008-2010 Bjoern Hoehrmann <bjoern@hoehrmann.de>
// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.

#define FONS_UTF8_ACCEPT 0
#define FONS_UTF8_REJECT 12

static unsigned int fons__decutf8(unsigned int* state, unsigned int* codep, unsigned int byte) {
  static const unsigned char utf8d[] = {
    // The first part of the table maps bytes to character classes that
    // to reduce the size of the transition table and create bitmasks.
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

    // The second part is a transition table that maps a combination
    // of a state of the automaton and a character class to a state.
    0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
    12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
    12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
    12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
    12,36,12,12,12,12,12,12,12,12,12,12,
  };

  unsigned int type = utf8d[byte];

  *codep = (*state != FONS_UTF8_ACCEPT) ?
    (byte & 0x3fu) | (*codep << 6) :
    (0xff >> type) & (byte);

  *state = utf8d[256 + *state + type];
  return *state;
}

// Atlas based on Skyline Bin Packer by Jukka Jylänki

static void fons__deleteAtlas(FONSatlas* atlas) {
  delete atlas;
}

static FONSatlas* fons__allocAtlas(int w, int h, int nnodes) {
  // Allocate memory for the font stash
  FONSatlas* atlas = new FONSatlas();
  atlas->width = w;
  atlas->height = h;
  
  // Allocate space for skyline nodes
  atlas->nodes.reserve(nnodes);
  
  // Init root node
  atlas->nodes.emplace_back(0, 0, w);
  
  return atlas;
}

static int fons__atlasInsertNode(FONSatlas* atlas, int idx, int x, int y, int w) {
  atlas->nodes.emplace(atlas->nodes.begin() + idx, x, y, w);
  return 1;
}

static void fons__atlasRemoveNode(FONSatlas* atlas, int idx) {
  if (idx < atlas->nodes.size()) {
    atlas->nodes.erase(atlas->nodes.begin() + idx);
  }
}

static void fons__atlasExpand(FONSatlas* atlas, int w, int h) {
  // Insert node for empty space
  if (w > atlas->width)
    fons__atlasInsertNode(atlas, atlas->nodes.size(), atlas->width, 0, w - atlas->width);
  atlas->width = w;
  atlas->height = h;
}

static void fons__atlasReset(FONSatlas* atlas, int w, int h) {
  atlas->width = w;
  atlas->height = h;
  atlas->nodes.clear();

  // Init root node
  atlas->nodes.emplace_back(0, 0, w);
}

static int fons__atlasAddSkylineLevel(FONSatlas* atlas, int idx, int x, int y, int w, int h) {
  // Insert new node
  if (fons__atlasInsertNode(atlas, idx, x, y+h, w) == 0)
    return 0;

  // Delete skyline segments that fall under the shadow of the new segment.
  for (int i = idx + 1; i < atlas->nodes.size(); i++) {
    if (atlas->nodes[i].x < atlas->nodes[i-1].x + atlas->nodes[i-1].width) {
      int shrink = atlas->nodes[i-1].x + atlas->nodes[i-1].width - atlas->nodes[i].x;
      atlas->nodes[i].x += (short)shrink;
      atlas->nodes[i].width -= (short)shrink;
      if (atlas->nodes[i].width <= 0) {
        fons__atlasRemoveNode(atlas, i);
        i--;
      } else {
        break;
      }
    } else {
      break;
    }
  }

  // Merge same height skyline segments that are next to each other.
  for (int i = 0; i < atlas->nodes.size() - 1; i++) {
    if (atlas->nodes[i].y == atlas->nodes[i+1].y) {
      atlas->nodes[i].width += atlas->nodes[i+1].width;
      fons__atlasRemoveNode(atlas, i+1);
      i--;
    }
  }

  return 1;
}

static int fons__atlasRectFits(FONSatlas* atlas, int i, int w, int h) {
  // Checks if there is enough space at the location of skyline span 'i',
  // and return the max height of all skyline spans under that at that location,
  // (think tetris block being dropped at that position). Or -1 if no space found.
  int x = atlas->nodes[i].x;
  int y = atlas->nodes[i].y;
  int spaceLeft;
  if (x + w > atlas->width) {
    return -1;
  }
  spaceLeft = w;
  int nnodes = atlas->nodes.size();
  while (spaceLeft > 0) {
    if (i == nnodes) return -1;
    y = max<int>(y, atlas->nodes[i].y);
    if (y + h > atlas->height) return -1;
    spaceLeft -= atlas->nodes[i].width;
    ++i;
  }
  return y;
}

static int fons__atlasAddRect(FONSatlas* atlas, int rw, int rh, int* rx, int* ry) {
  int besth = atlas->height, bestw = atlas->width, besti = -1;
  int bestx = -1, besty = -1;

  // Bottom left fit heuristic.
  for (int i = 0, size = atlas->nodes.size(); i < size; i++) {
    int y = fons__atlasRectFits(atlas, i, rw, rh);
    if (y != -1) {
      if (y + rh < besth || (y + rh == besth && atlas->nodes[i].width < bestw)) {
        besti = i;
        bestw = atlas->nodes[i].width;
        besth = y + rh;
        bestx = atlas->nodes[i].x;
        besty = y;
      }
    }
  }

  if (besti == -1)
    return 0;

  // Perform the actual packing.
  if (fons__atlasAddSkylineLevel(atlas, besti, bestx, besty, rw, rh) == 0)
    return 0;

  *rx = bestx;
  *ry = besty;

  return 1;
}

static void fons__addWhiteRect(FONScontext* stash, int w, int h) {
  int x, y, gx, gy;
  unsigned char* dst;
  if (fons__atlasAddRect(stash->atlas, w, h, &gx, &gy) == 0)
    return;

  // Rasterize
  dst = &stash->texData[gx + gy * stash->params.width];
  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++)
      dst[x] = 0xff;
    dst += stash->params.width;
  }

  stash->dirtyRect[0] = min(stash->dirtyRect[0], gx);
  stash->dirtyRect[1] = min(stash->dirtyRect[1], gy);
  stash->dirtyRect[2] = max(stash->dirtyRect[2], gx+w);
  stash->dirtyRect[3] = max(stash->dirtyRect[3], gy+h);
}

FONScontext* fonsCreateInternal(FONSparams* params) {
  // Allocate memory for the font stash.
  FONScontext* stash = new FONScontext();
  if (stash == NULL) goto error;
  
  stash->params = *params;
  
  // Allocate scratch buffer.
  stash->scratch.resize(FONS_SCRATCH_BUF_SIZE);
  
  // Initialize implementation library
  if (!fons__tt_init(stash)) goto error;
  
  if (stash->params.renderCreate != NULL) {
    if (stash->params.renderCreate(stash->params.userPtr, stash->params.width, stash->params.height, stash->currentTextureIndex) == 0)
      goto error;
  }
  
  stash->atlas = fons__allocAtlas(stash->params.width, stash->params.height, FONS_INIT_ATLAS_NODES);
  if (stash->atlas == NULL) goto error;
  
  // Allocate space for fonts.
  stash->fonts.reserve(FONS_INIT_FONTS);
  
  // Create texture for the cache.
  stash->itw = 1.0f/stash->params.width;
  stash->ith = 1.0f/stash->params.height;
  stash->texData.resize(stash->params.width * stash->params.height);
  memset(stash->texData.data(), 0, stash->params.width * stash->params.height);
  
  stash->dirtyRect[0] = stash->params.width;
  stash->dirtyRect[1] = stash->params.height;
  stash->dirtyRect[2] = 0;
  stash->dirtyRect[3] = 0;
  
  // Add white rect at 0,0 for debug drawing.
  fons__addWhiteRect(stash, 2,2);
  
  fonsPushState(stash);
  fonsClearState(stash);
  
  return stash;
  
error:
  fonsDeleteInternal(stash);
  return NULL;
}

static FONSstate* fons__getState(FONScontext* stash) {
  return &stash->states[stash->nstates-1];
}

int fonsAddFallbackFont(FONScontext* stash, int base, int fallback) {
  FONSfont* baseFont = stash->fonts[base];
  baseFont->fallbacks.emplace_back(fallback);
  return 1;
}

void fonsSetSize(FONScontext* stash, float size) {
  fons__getState(stash)->size = size;
}

void fonsSetColor(FONScontext* stash, unsigned int color) {
  fons__getState(stash)->color = color;
}

void fonsSetSpacing(FONScontext* stash, float spacing) {
  fons__getState(stash)->spacing = spacing;
}

void fonsSetBlur(FONScontext* stash, float blur) {
  fons__getState(stash)->blur = blur;
}

void fonsSetAlign(FONScontext* stash, int align) {
  fons__getState(stash)->align = align;
}

void fonsSetFont(FONScontext* stash, int font) {
  fons__getState(stash)->font = font;
}

void fonsSetRoundingAndScaling(FONScontext* stash, bool roundToInt, float scaling) {
  fons__getState(stash)->roundToInt = roundToInt;
  fons__getState(stash)->scaling = scaling;
}

void fonsPushState(FONScontext* stash) {
  if (stash->nstates >= FONS_MAX_STATES) {
    if (stash->handleError)
      stash->handleError(stash->errorUserPtr, FONS_STATES_OVERFLOW, 0);
    return;
  }
  if (stash->nstates > 0)
    memcpy(&stash->states[stash->nstates], &stash->states[stash->nstates-1], sizeof(FONSstate));
  stash->nstates++;
}

void fonsPopState(FONScontext* stash) {
  if (stash->nstates <= 1) {
    if (stash->handleError)
      stash->handleError(stash->errorUserPtr, FONS_STATES_UNDERFLOW, 0);
    return;
  }
  stash->nstates--;
}

void fonsClearState(FONScontext* stash) {
  FONSstate* state = fons__getState(stash);
  state->size = 12.0f;
  state->color = 0xffffffff;
  state->font = 0;
  state->blur = 0;
  state->spacing = 0;
  state->align = FONS_ALIGN_LEFT | FONS_ALIGN_BASELINE;
}

static void fons__freeFont(FONSfont* font) {
  delete font;
}

static int fons__allocFont(FONScontext* stash) {
  FONSfont* font = new FONSfont();
  font->glyphs.reserve(FONS_INIT_GLYPHS);
  
  stash->fonts.emplace_back(font);
  return stash->fonts.size() - 1;
}

int fonsAddFont(FONScontext* stash, const char* name, const fs::path& path) {
  ifstream file(path, ios::in | ios::binary | ios::ate);
  if (!file.is_open()) { return FONS_INVALID; }
  
  const usize dataSize = file.tellg();
  file.seekg(0, std::ios::beg);
  
  vector<u8> data(dataSize);
  file.read(reinterpret_cast<char*>(data.data()), dataSize);
  if (file.gcount() != dataSize) { return FONS_INVALID; }
  
  file.close();
  
  return fonsAddFontMem(stash, name, std::move(data));
}

int fonsAddFontMem(FONScontext* stash, const char* name, vector<u8>&& data) {
  int i, ascent, descent, fh, lineGap;
  FONSfont* font;
  
  int idx = fons__allocFont(stash);
  if (idx == FONS_INVALID) {
    return FONS_INVALID;
  }
  
  font = stash->fonts[idx];
  
  font->name = name;
  
  // Init hash lookup.
  for (i = 0; i < FONS_HASH_LUT_SIZE; ++i)
    font->lut[i] = -1;
  
  // Read in the font data.
  font->data = std::move(data);
  
  // Init font
  stash->nscratch = 0;
  if (!fons__tt_loadFont(stash, &font->font, font->data.data(), font->data.size())) {
    fons__freeFont(font);
    stash->fonts.pop_back();
    return FONS_INVALID;
  }
  
  // Store normalized line height. The real line height is got
  // by multiplying the lineh by font size.
  fons__tt_getFontVMetrics( &font->font, &ascent, &descent, &lineGap);
  fh = ascent - descent;
  font->ascender = (float)ascent / (float)fh;
  font->descender = (float)descent / (float)fh;
  font->lineh = (float)(fh + lineGap) / (float)fh;
  
  return idx;
}

int fonsGetFontByName(FONScontext* s, const char* name) {
  for (int i = 0; i < s->fonts.size(); i++) {
    if (s->fonts[i]->name == name) {
      return i;
    }
  }
  return FONS_INVALID;
}


static FONSglyph* fons__allocGlyph(FONSfont* font) {
  return &font->glyphs.emplace_back();
}


// Based on Exponential blur, Jani Huhtanen, 2006

#define APREC 16
#define ZPREC 7

static void fons__blurCols(unsigned char* dst, int w, int h, int dstStride, int alpha) {
  int x, y;
  for (y = 0; y < h; y++) {
    int z = 0; // force zero border
    for (x = 1; x < w; x++) {
      z += (alpha * (((int)(dst[x]) << ZPREC) - z)) >> APREC;
      dst[x] = (unsigned char)(z >> ZPREC);
    }
    dst[w-1] = 0; // force zero border
    z = 0;
    for (x = w-2; x >= 0; x--) {
      z += (alpha * (((int)(dst[x]) << ZPREC) - z)) >> APREC;
      dst[x] = (unsigned char)(z >> ZPREC);
    }
    dst[0] = 0; // force zero border
    dst += dstStride;
  }
}

static void fons__blurRows(unsigned char* dst, int w, int h, int dstStride, int alpha) {
  int x, y;
  for (x = 0; x < w; x++) {
    int z = 0; // force zero border
    for (y = dstStride; y < h*dstStride; y += dstStride) {
      z += (alpha * (((int)(dst[y]) << ZPREC) - z)) >> APREC;
      dst[y] = (unsigned char)(z >> ZPREC);
    }
    dst[(h-1)*dstStride] = 0; // force zero border
    z = 0;
    for (y = (h-2)*dstStride; y >= 0; y -= dstStride) {
      z += (alpha * (((int)(dst[y]) << ZPREC) - z)) >> APREC;
      dst[y] = (unsigned char)(z >> ZPREC);
    }
    dst[0] = 0; // force zero border
    dst++;
  }
}


static void fons__blur(FONScontext* stash, unsigned char* dst, int w, int h, int dstStride, int blur) {
  int alpha;
  float sigma;
  (void)stash;

  if (blur < 1)
    return;
  // Calculate the alpha such that 90% of the kernel is within the radius. (Kernel extends to infinity)
  sigma = (float)blur * 0.57735f; // 1 / sqrt(3)
  alpha = (int)((1<<APREC) * (1.0f - expf(-2.3f / (sigma+1.0f))));
  fons__blurRows(dst, w, h, dstStride, alpha);
  fons__blurCols(dst, w, h, dstStride, alpha);
  fons__blurRows(dst, w, h, dstStride, alpha);
  fons__blurCols(dst, w, h, dstStride, alpha);
//  fons__blurrows(dst, w, h, dstStride, alpha);
//  fons__blurcols(dst, w, h, dstStride, alpha);
}

static FONSglyph* fons__getGlyph(FONScontext* stash, FONSfont* font, unsigned int codepoint, short isize, short iblur) {
  int g, advance, lsb, x0, y0, x1, y1, gw, gh, gx, gy, x, y;
  FONSglyph* glyph = NULL;
  unsigned int h;
  float size = isize/10.0f;
  int pad, added;
  unsigned char* bdst;
  unsigned char* dst;
  FONSfont* renderFont = font;
  
  if (isize < 2) return NULL;
  if (iblur > 20) iblur = 20;
  pad = iblur+2;
  
  // Reset allocator.
  stash->nscratch = 0;
  
  // Find code point and size.
  h = fons__hashint(codepoint) & (FONS_HASH_LUT_SIZE-1);
  int l = font->lut[h];
  while (l != -1) {
    if (font->glyphs[l].codepoint == codepoint && font->glyphs[l].size == isize && font->glyphs[l].blur == iblur) {
      return &font->glyphs[l];
    }
    l = font->glyphs[l].next;
  }
  
  // Could not find glyph, create it.
  g = fons__tt_getGlyphIndex(&font->font, codepoint);
  // Try to find the glyph in fallback fonts.
  if (g == 0) {
    for (int i = 0, fallbacksSize = font->fallbacks.size(); i < fallbacksSize; ++i) {
      FONSfont* fallbackFont = stash->fonts[font->fallbacks[i]];
      int fallbackIndex = fons__tt_getGlyphIndex(&fallbackFont->font, codepoint);
      if (fallbackIndex != 0) {
        g = fallbackIndex;
        renderFont = fallbackFont;
        break;
      }
    }
    // It is possible that we did not find a fallback glyph.
    // In that case the glyph index 'g' is 0, and we'll proceed below and cache empty glyph.
  }
  const float scale = fons__tt_getPixelHeightScale(&renderFont->font, size);
  fons__tt_buildGlyphBitmap(&renderFont->font, g, size, scale, &advance, &lsb, &x0, &y0, &x1, &y1);
  gw = x1-x0 + pad*2;
  gh = y1-y0 + pad*2;
  
  // Find free spot for the rect in the atlas
  added = fons__atlasAddRect(stash->atlas, gw, gh, &gx, &gy);
  if (added == 0 && stash->handleError != NULL) {
    // Atlas is full, let the user to resize the atlas (or not), and try again.
    stash->handleError(stash->errorUserPtr, FONS_ATLAS_FULL, 0);
    added = fons__atlasAddRect(stash->atlas, gw, gh, &gx, &gy);
  }
  if (added == 0) return NULL;
  
  // Init glyph.
  glyph = fons__allocGlyph(font);
  glyph->codepoint = codepoint;
  glyph->size = isize;
  glyph->blur = iblur;
  glyph->index = g;
  glyph->x0 = (short)gx;
  glyph->y0 = (short)gy;
  glyph->x1 = (short)(glyph->x0+gw);
  glyph->y1 = (short)(glyph->y0+gh);
  glyph->xadv = (short)(scale * advance * 10.0f);
  glyph->xoff = (short)(x0 - pad);
  glyph->yoff = (short)(y0 - pad);
  glyph->next = 0;
  
  // Insert char to hash lookup.
  glyph->next = font->lut[h];
  font->lut[h] = font->glyphs.size() - 1;
  
  // Rasterize
  dst = &stash->texData[(glyph->x0+pad) + (glyph->y0+pad) * stash->params.width];
  fons__tt_renderGlyphBitmap(&renderFont->font, dst, gw-pad*2,gh-pad*2, stash->params.width, scale,scale, g);
  
  // Make sure there is one pixel empty border.
  dst = &stash->texData[glyph->x0 + glyph->y0 * stash->params.width];
  for (y = 0; y < gh; y++) {
    dst[y*stash->params.width] = 0;
    dst[gw-1 + y*stash->params.width] = 0;
  }
  for (x = 0; x < gw; x++) {
    dst[x] = 0;
    dst[x + (gh-1)*stash->params.width] = 0;
  }
  
  // Debug code to color the glyph background
/*  unsigned char* fdst = &stash->texData[glyph->x0 + glyph->y0 * stash->params.width];
  for (y = 0; y < gh; y++) {
    for (x = 0; x < gw; x++) {
      int a = (int)fdst[x+y*stash->params.width] + 20;
      if (a > 255) a = 255;
      fdst[x+y*stash->params.width] = a;
    }
  }*/
  
  // Blur
  if (iblur > 0) {
    stash->nscratch = 0;
    bdst = &stash->texData[glyph->x0 + glyph->y0 * stash->params.width];
    fons__blur(stash, bdst, gw,gh, stash->params.width, iblur);
  }
  
  stash->dirtyRect[0] = min<int>(stash->dirtyRect[0], glyph->x0);
  stash->dirtyRect[1] = min<int>(stash->dirtyRect[1], glyph->y0);
  stash->dirtyRect[2] = max<int>(stash->dirtyRect[2], glyph->x1);
  stash->dirtyRect[3] = max<int>(stash->dirtyRect[3], glyph->y1);
  
  return glyph;
}

static void fons__getQuadRounded(
    FONScontext* stash, FONSfont* font,
    int prevGlyphIndex, FONSglyph* glyph,
    float scale, float spacing, float* x, float* y, FONSquad* q) {
  if (prevGlyphIndex != -1) {
    const float adv = fons__tt_getGlyphKernAdvance(&font->font, prevGlyphIndex, glyph->index) * scale;
    *x += (int)(adv + spacing + 0.5f);
  }
  
  // Each glyph has 2px border to allow good interpolation,
  // one pixel to prevent leaking, and one to allow good interpolation for rendering.
  // Inset the texture region by one pixel for correct interpolation.
  const float xoff = glyph->xoff + 1;
  const float yoff = glyph->yoff + 1;
  const float x0 = glyph->x0 + 1;
  const float y0 = glyph->y0 + 1;
  const float x1 = glyph->x1 - 1;
  const float y1 = glyph->y1 - 1;
  
  if (stash->params.flags & FONS_ZERO_TOPLEFT) {
    const float rx = (float)(int)(*x + xoff);
    const float ry = (float)(int)(*y + yoff);
    
    q->x0 = rx;
    q->y0 = ry;
    q->x1 = rx + x1 - x0;
    q->y1 = ry + y1 - y0;
    
    q->s0 = x0 * stash->itw;
    q->t0 = y0 * stash->ith;
    q->s1 = x1 * stash->itw;
    q->t1 = y1 * stash->ith;
  } else {
    const float rx = (float)(int)(*x + xoff);
    const float ry = (float)(int)(*y - yoff);
    
    q->x0 = rx;
    q->y0 = ry;
    q->x1 = rx + x1 - x0;
    q->y1 = ry - y1 + y0;
    
    q->s0 = x0 * stash->itw;
    q->t0 = y0 * stash->ith;
    q->s1 = x1 * stash->itw;
    q->t1 = y1 * stash->ith;
  }
  
  *x += (int)(glyph->xadv / 10.0f + 0.5f);
}

static void fons__getQuadScaled(
    FONScontext* stash, FONSfont* font,
    int prevGlyphIndex, FONSglyph* glyph,
    float scale, float spacing, float scaling, float* x, float* y, FONSquad* q) {
  if (prevGlyphIndex != -1) {
    const float adv = fons__tt_getGlyphKernAdvance(&font->font, prevGlyphIndex, glyph->index) * scale;
    *x += scaling * (adv + spacing);
  }
  
  // Each glyph has 2px border to allow good interpolation,
  // one pixel to prevent leaking, and one to allow good interpolation for rendering.
  // Inset the texture region by one pixel for correct interpolation.
  const float xoff = scaling * (glyph->xoff + 1);
  const float yoff = scaling * (glyph->yoff + 1);
  const float x0 = glyph->x0 + 1;
  const float y0 = glyph->y0 + 1;
  const float x1 = glyph->x1 - 1;
  const float y1 = glyph->y1 - 1;
  
  if (stash->params.flags & FONS_ZERO_TOPLEFT) {
    const float rx = *x + xoff;
    const float ry = *y + yoff;
    
    q->x0 = rx;
    q->y0 = ry;
    q->x1 = rx + scaling * (x1 - x0);
    q->y1 = ry + scaling * (y1 - y0);
    
    q->s0 = x0 * stash->itw;
    q->t0 = y0 * stash->ith;
    q->s1 = x1 * stash->itw;
    q->t1 = y1 * stash->ith;
  } else {
    const float rx = *x + xoff;
    const float ry = *y - yoff;
    
    q->x0 = rx;
    q->y0 = ry;
    q->x1 = rx + scaling * (x1 - x0);
    q->y1 = ry - scaling * (y1 + y0);
    
    q->s0 = x0 * stash->itw;
    q->t0 = y0 * stash->ith;
    q->s1 = x1 * stash->itw;
    q->t1 = y1 * stash->ith;
  }
  
  *x += scaling * (glyph->xadv / 10.0f);
}

static inline void fons__getQuad(
    FONScontext* stash, FONSfont* font,
    int prevGlyphIndex, FONSglyph* glyph,
    float scale, float spacing, bool roundToInt, float scaling, float* x, float* y, FONSquad* q) {
  if (roundToInt) {
    fons__getQuadRounded(stash, font, prevGlyphIndex, glyph, scale, spacing, x, y, q);
  } else {
    fons__getQuadScaled(stash, font, prevGlyphIndex, glyph, scale, spacing, scaling, x, y, q);
  }
}

static void fons__flush(FONScontext* stash) {
  // Flush texture
  if (stash->dirtyRect[0] < stash->dirtyRect[2] && stash->dirtyRect[1] < stash->dirtyRect[3]) {
    if (stash->params.renderUpdate != NULL)
      stash->params.renderUpdate(stash->params.userPtr, stash->dirtyRect, stash->texData.data());
    // Reset dirty rect
    stash->dirtyRect[0] = stash->params.width;
    stash->dirtyRect[1] = stash->params.height;
    stash->dirtyRect[2] = 0;
    stash->dirtyRect[3] = 0;
  }
}

static __inline void fons__vertex(FONScontext* /*stash*/, float x, float y, float s, float t, unsigned int c, FONSText* text) {
  auto& vertices = text->vertices;
  
  vertices.emplace_back();
  auto& v = vertices.back();
  
  v.x = x;
  v.y = y;
  v.tx = s;
  v.ty = t;
  v.rgba = c;
}

static float fons__getVertAlign(FONScontext* stash, FONSfont* font, int align, short isize) {
  FONSstate* state = fons__getState(stash);
  const float scaling = state->roundToInt ? 1 : state->scaling;
  
  if (stash->params.flags & FONS_ZERO_TOPLEFT) {
    if (align & FONS_ALIGN_TOP) {
      return scaling * (font->ascender * (float)isize/10.0f);
    } else if (align & FONS_ALIGN_MIDDLE) {
      return scaling * ((font->ascender + font->descender) / 2.0f * (float)isize/10.0f);
    } else if (align & FONS_ALIGN_BASELINE) {
      return 0.0f;
    } else if (align & FONS_ALIGN_BOTTOM) {
      return scaling * (font->descender * (float)isize/10.0f);
    }
  } else {
    if (align & FONS_ALIGN_TOP) {
      return scaling * (-font->ascender * (float)isize/10.0f);
    } else if (align & FONS_ALIGN_MIDDLE) {
      return scaling * (-(font->ascender + font->descender) / 2.0f * (float)isize/10.0f);
    } else if (align & FONS_ALIGN_BASELINE) {
      return 0.0f;
    } else if (align & FONS_ALIGN_BOTTOM) {
      return scaling * (-font->descender * (float)isize/10.0f);
    }
  }
  return 0.0;
}

float fonsCreateText(FONScontext* s, float x, float y, const char* str, const char* end, FONSText* text) {
  text->vertices.clear();
  text->batches.clear();
  
  if (s == nullptr) { return x; }
  FONSstate* state = fons__getState(s);
  if (state->font < 0 || state->font >= s->fonts.size()) { return x; }
  FONSfont* font = s->fonts[state->font];
  if (font->data.empty()) { return x; }
  
  const short isize = (short)(state->size*10.0f);
  const short iblur = (short)state->blur;
  const float scale = fons__tt_getPixelHeightScale(&font->font, (float)isize / 10.0f);
  
  if (end == nullptr) {
    end = str + strlen(str);
  }
  
  // Align horizontally
  if (state->align & FONS_ALIGN_LEFT) {
    // empty
  } else if (state->align & FONS_ALIGN_RIGHT) {
    const float width = fonsTextBounds(s, x, y, str, end, nullptr);
    x -= width;
  } else if (state->align & FONS_ALIGN_CENTER) {
    const float width = fonsTextBounds(s, x, y, str, end, nullptr);
    x -= width * 0.5f;
  }
  // Align vertically.
  y += fons__getVertAlign(s, font, state->align, isize);
  
  text->vertices.reserve(6 * (end - str));
  
  // Create first batch start marker
  text->batches.emplace_back(/*firstVertex*/ 0, s->currentTextureIndex);
  int lastTextureIndex = s->currentTextureIndex;
  
  int prevGlyphIndex = -1;
  unsigned int utf8state = 0;
  unsigned int codepoint;
  FONSquad q;
  
  for (; str != end; ++str) {
    if (fons__decutf8(&utf8state, &codepoint, *(const unsigned char*)str)) { continue; }
    
    FONSglyph* glyph = fons__getGlyph(s, font, codepoint, isize, iblur);
    // If getting the glyph created a new texture, insert new batch start marker
    if (s->currentTextureIndex != lastTextureIndex) {
      text->batches.emplace_back(/*firstVertex*/ text->vertices.size(), s->currentTextureIndex);
      lastTextureIndex = s->currentTextureIndex;
    }
    
    if (glyph != nullptr) {
      fons__getQuad(s, font, prevGlyphIndex, glyph, scale, state->spacing, state->roundToInt, state->scaling, &x, &y, &q);
      
      fons__vertex(s, q.x0, q.y0, q.s0, q.t0, state->color, text);
      fons__vertex(s, q.x1, q.y1, q.s1, q.t1, state->color, text);
      fons__vertex(s, q.x1, q.y0, q.s1, q.t0, state->color, text);

      fons__vertex(s, q.x0, q.y0, q.s0, q.t0, state->color, text);
      fons__vertex(s, q.x0, q.y1, q.s0, q.t1, state->color, text);
      fons__vertex(s, q.x1, q.y1, q.s1, q.t1, state->color, text);
    }
    prevGlyphIndex = glyph != nullptr ? glyph->index : -1;
  }
  fons__flush(s);
  
  return x;
}

void fonsCreateTextBox(FONScontext* s, float minX, float minY, float maxX, float maxY, bool wordWrap, const char* str, const char* end, FONSText* text) {
  text->vertices.clear();
  text->batches.clear();
  
  if (s == nullptr) { return; }
  FONSstate* state = fons__getState(s);
  if (state->font < 0 || state->font >= s->fonts.size()) { return; }
  FONSfont* font = s->fonts[state->font];
  if (font->data.empty()) { return; }
  
  const short isize = (short)(state->size*10.0f);
  const short iblur = (short)state->blur;
  const float scale = fons__tt_getPixelHeightScale(&font->font, (float)isize / 10.0f);
  
  if (end == nullptr) {
    end = str + strlen(str);
  }
  
  const float boxWidth = maxX - minX;
  const float boxHeight = maxY - minY;
  
  const float scaling = state->roundToInt ? 1 : state->scaling;
  const float lineHeight = scaling * (font->lineh*isize/10.0f);
  
  // Get all character quads and group them into lines (but don't create vertices for the quads yet)
  int prevGlyphIndex = -1;
  unsigned int utf8state = 0;
  unsigned int codepoint;
  
  struct QuadData {
    int textureIdx;   // index of the texture to use for rendering the character
    float width = 0;  // width of the quad, including the advance to the next character; the advance can be computed as: width - quad.x1;
    FONSquad quad;    // quad for rendering the character, with the top-left position at (0, 0)
  };
  vector<QuadData> quads;  // pairs of (texture index, quad).
  quads.reserve(end - str);
  
  float currentLineWidth = 0;
  vector<float> lineWidths;
  vector<int> lineStartQuadIndices = {0};  // including one entry beyond the last line
  int lastWordStartQuadIndex = 0;
  bool removableWhitespaceBeforeLastWord = false;
  
  for (; str != end; ++str) {
    if (*str == '\n') {
      lineWidths.emplace_back(currentLineWidth);
      currentLineWidth = 0;
      lastWordStartQuadIndex = quads.size();
      removableWhitespaceBeforeLastWord = false;
      lineStartQuadIndices.emplace_back(quads.size());
      prevGlyphIndex = -1;
      utf8state = 0;
      continue;
    } else if (*str == ' ' || *str == '\t') {
      // Notice that whitespace will also produce a glyph.
      lastWordStartQuadIndex = quads.size() + 1;
      removableWhitespaceBeforeLastWord = true;
    }
    
    // Decode UTF-8
    if (fons__decutf8(&utf8state, &codepoint, *(const unsigned char*)str)) { continue; }
    
    // Get the glyph (if any), drawing the glyph to the texture if necessary
    FONSglyph* glyph = fons__getGlyph(s, font, codepoint, isize, iblur);
    if (glyph == nullptr) {
      prevGlyphIndex = -1;
      continue;
    }
    
    // Get the quad for the glyph
    float zero = 0;
    auto& newQuad = quads.emplace_back();
    newQuad.textureIdx = s->currentTextureIndex;
    fons__getQuad(s, font, prevGlyphIndex, glyph, scale, state->spacing, state->roundToInt, state->scaling, &newQuad.width, &zero, &newQuad.quad);
    prevGlyphIndex = glyph->index;
    const float lastGlyphAdvance = newQuad.width - newQuad.quad.x1;
    
    // Handle clamping to the text box
    currentLineWidth += newQuad.width;
    if (currentLineWidth - lastGlyphAdvance > boxWidth) {
      if (wordWrap && (lineStartQuadIndices.size() + 1) * lineHeight <= boxHeight) {
        if (lastWordStartQuadIndex <= lineStartQuadIndices.back()) {
          // The line that is too long consists only of a single word.
          // Break the word, moving only the last character to the next line.
          lastWordStartQuadIndex = quads.size() - 1;
          removableWhitespaceBeforeLastWord = false;
        }
        
        // Remove the last word from the current line and place it on the next line.
        float lastWordWidth = 0;
        for (int i = lastWordStartQuadIndex; i < quads.size(); ++ i) {
          lastWordWidth += quads[i].width;
        }
        
        currentLineWidth -= lastWordWidth;
        
        // If there is a whitespace character before lastWordStartQuadIndex and it is in the same line as it,
        // remove it (because it gets replaced by the line break).
        if (removableWhitespaceBeforeLastWord) {
          currentLineWidth -= quads[lastWordStartQuadIndex - 1].width;
          quads.erase(quads.begin() + lastWordStartQuadIndex - 1);
          -- lastWordStartQuadIndex;
          removableWhitespaceBeforeLastWord = false;
        }
        
        lineWidths.emplace_back(currentLineWidth);
        currentLineWidth = 0;
        lineStartQuadIndices.emplace_back(lastWordStartQuadIndex);
        
        currentLineWidth += lastWordWidth;
      } else {
        // Elision: Remove the last characters of the last line until "..." (or as much as possible of that) fits at the end.
        // Then, stop processing any further text.
        utf8state = 0;
        if (fons__decutf8(&utf8state, &codepoint, '.') == 0) {
          glyph = fons__getGlyph(s, font, codepoint, isize, iblur);
          if (glyph != nullptr) {
            float zero = 0;
            float dotWidth = 0;
            FONSquad dotQuad;
            // NOTE: We currently do not take prevGlyphIndex into account here, kerning might be off as a result.
            fons__getQuad(s, font, /*prevGlyphIndex*/ -1, glyph, scale, state->spacing, state->roundToInt, state->scaling, &dotWidth, &zero, &dotQuad);
            const float dotAdvance = dotWidth - dotQuad.x1;
            
            const float tripleDotWidth = 3 * dotWidth - dotAdvance;
            
            while (currentLineWidth + tripleDotWidth > boxWidth &&
                   lineStartQuadIndices.back() < quads.size()) {
              currentLineWidth -= quads.back().width;
              quads.pop_back();
            }
            
            for (int i = 0; i < 3 && currentLineWidth + dotWidth - dotAdvance <= boxWidth; ++ i) {
              currentLineWidth += dotWidth;
              auto& newQuad = quads.emplace_back();
              newQuad.textureIdx = s->currentTextureIndex;
              newQuad.width = dotWidth;
              newQuad.quad = dotQuad;
            }
          }
        }
        break;
      }
    }
  }  // loop over input text characters
  
  if (quads.size() == 0) {
    return;
  }
  
  // Finish the last line
  lineWidths.emplace_back(currentLineWidth);
  lineStartQuadIndices.emplace_back(quads.size());
  
  // Align the text block vertically
  const float ascent = scaling * (font->ascender * (float)isize/10.0f);
  const float textBlockHeight =
      ascent +                                                // ascent of first line
      (lineWidths.size() - 1) * lineHeight +                  // line heights in-between
      scaling * (-1 * font->descender * (float)isize/10.0f);  // descent of last line
  float cursorY;
  if (state->align & FONS_ALIGN_MIDDLE) {
    cursorY = 0.5f * (minY + maxY) - 0.5f * textBlockHeight + ascent;
  } else if (state->align & FONS_ALIGN_BOTTOM) {
    cursorY = maxY - textBlockHeight + ascent;
  } else {  // state->align & FONS_ALIGN_TOP, or not specified
    cursorY = minY + ascent;
  }
  if (state->roundToInt) {
    cursorY = round(cursorY);
  }
  
  // Align each line horizontally, and create vertices for the quads in it
  bool haveLastTextureIndex = false;
  int lastTextureIndex;
  
  for (usize lineIdx = 0, lineCount = lineWidths.size(); lineIdx < lineCount; ++ lineIdx) {
    const auto& lastQuadInLine = quads[lineStartQuadIndices[lineIdx + 1] - 1];
    const float lastQuadInLineAdvance = lastQuadInLine.width - lastQuadInLine.quad.x1;
    const float lineWidth = lineWidths[lineIdx] - lastQuadInLineAdvance;
    
    // Align horizontally
    float cursorX;
    if (state->align & FONS_ALIGN_RIGHT) {
      cursorX = maxX - lineWidth;
    } else if (state->align & FONS_ALIGN_CENTER) {
      cursorX = 0.5f * (minX + maxX) - 0.5f * lineWidth;
    } else {  // state->align & FONS_ALIGN_LEFT, or not specified
      cursorX = minX;
    }
    if (state->roundToInt) {
      cursorX = round(cursorX);
    }
    
    for (int quadIdx = lineStartQuadIndices[lineIdx], endQuadIdx = lineStartQuadIndices[lineIdx + 1]; quadIdx < endQuadIdx; ++ quadIdx) {
      const auto& quad = quads[quadIdx];
      
      // Insert new batch start marker?
      if (!haveLastTextureIndex || quad.textureIdx != lastTextureIndex) {
        text->batches.emplace_back(/*firstVertex*/ text->vertices.size(), quad.textureIdx);
        lastTextureIndex = quad.textureIdx;
        haveLastTextureIndex = true;
      }
      
      // Insert the vertices at the cursor position
      const auto& q = quad.quad;
      
      fons__vertex(s, cursorX + q.x0, cursorY + q.y0, q.s0, q.t0, state->color, text);
      fons__vertex(s, cursorX + q.x1, cursorY + q.y1, q.s1, q.t1, state->color, text);
      fons__vertex(s, cursorX + q.x1, cursorY + q.y0, q.s1, q.t0, state->color, text);
  
      fons__vertex(s, cursorX + q.x0, cursorY + q.y0, q.s0, q.t0, state->color, text);
      fons__vertex(s, cursorX + q.x0, cursorY + q.y1, q.s0, q.t1, state->color, text);
      fons__vertex(s, cursorX + q.x1, cursorY + q.y1, q.s1, q.t1, state->color, text);
      
      cursorX += quad.width;
    }
    
    cursorY += lineHeight;
  }
  
  fons__flush(s);
}

// void fonsBatchText(FONScontext* s, const FONSText& text) {
//   // Loop over all batches in the given text
//   int batchEndVertex = text.vertices.size();
//   
//   for (int b = static_cast<int>(text.batches.size()) - 1; b >= 0; -- b) {
//     const auto& textBatch = text.batches[b];
//     
//     // Find the stored batch for this texture, or create a new one if none exists yet
//     bool found = false;
//     
//     for (int i = 0, size = s->batchedVertices.size(); i < size; ++ i) {
//       auto& storedBatch = s->batchedVertices[i];
//       
//       if (textBatch.textureId == storedBatch.first) {
//         // Found an existing stored batch for this texture. Append the vertices there.
//         storedBatch.second.insert(storedBatch.second.end(), text.vertices.begin() + textBatch.firstVertex, text.vertices.begin() + batchEndVertex);
//         
//         found = true;
//         break;
//       }
//     }
//     
//     if (!found) {
//       // Create a new stored batch for this texture.
//       s->batchedVertices.emplace_back(make_pair(textBatch.textureId, vector<FONSVertex>(text.vertices.begin() + textBatch.firstVertex, text.vertices.begin() + batchEndVertex)));
//     }
//     
//     batchEndVertex = textBatch.firstVertex;
//   }
// }
// 
// void fonsFlushRender(FONScontext* stash) {
//   if (stash->params.renderDraw == nullptr) {
//     return;
//   }
//   
//   for (const auto& batch : stash->batchedVertices) {
//     if (batch.second.empty()) {
//       continue;
//     }
//     
//     stash->params.renderDraw(stash->params.userPtr, batch.second.data(), batch.second.size(), batch.first);
//   }
//   
//   stash->batchedVertices.clear();
// }

int fonsTextIterInit(FONScontext* stash, FONStextIter* iter, float x, float y, const char* str, const char* end) {
  FONSstate* state = fons__getState(stash);
  float width;

  memset(iter, 0, sizeof(*iter));

  if (stash == nullptr) return 0;
  if (state->font < 0 || state->font >= stash->fonts.size()) return 0;
  iter->font = stash->fonts[state->font];
  if (iter->font->data.empty()) return 0;

  iter->isize = (short)(state->size*10.0f);
  iter->iblur = (short)state->blur;
  iter->scale = fons__tt_getPixelHeightScale(&iter->font->font, (float)iter->isize/10.0f);

  // Align horizontally
  if (state->align & FONS_ALIGN_LEFT) {
    // empty
  } else if (state->align & FONS_ALIGN_RIGHT) {
    width = fonsTextBounds(stash, x,y, str, end, nullptr);
    x -= width;
  } else if (state->align & FONS_ALIGN_CENTER) {
    width = fonsTextBounds(stash, x,y, str, end, nullptr);
    x -= width * 0.5f;
  }
  // Align vertically.
  y += fons__getVertAlign(stash, iter->font, state->align, iter->isize);

  if (end == nullptr)
    end = str + strlen(str);

  iter->x = iter->nextx = x;
  iter->y = iter->nexty = y;
  iter->spacing = state->spacing;
  iter->roundToInt = state->roundToInt;
  iter->scaling = state->scaling;
  iter->str = str;
  iter->next = str;
  iter->end = end;
  iter->codepoint = 0;
  iter->prevGlyphIndex = -1;

  return 1;
}

int fonsTextIterNext(FONScontext* stash, FONStextIter* iter, FONSquad* quad) {
  FONSglyph* glyph = nullptr;
  const char* str = iter->next;
  iter->str = iter->next;
  
  if (str == iter->end)
    return 0;
  
  for (; str != iter->end; str++) {
    if (fons__decutf8(&iter->utf8state, &iter->codepoint, *(const unsigned char*)str))
      continue;
    str++;
    // Get glyph and quad
    iter->x = iter->nextx;
    iter->y = iter->nexty;
    glyph = fons__getGlyph(stash, iter->font, iter->codepoint, iter->isize, iter->iblur);
    if (glyph != nullptr) {
      fons__getQuad(stash, iter->font, iter->prevGlyphIndex, glyph, iter->scale, iter->spacing, iter->roundToInt, iter->scaling, &iter->nextx, &iter->nexty, quad);
    }
    iter->prevGlyphIndex = glyph != nullptr ? glyph->index : -1;
    break;
  }
  iter->next = str;
  
  return 1;
}

void fonsDrawDebug(FONScontext* stash, float x, float y, FONSText* text) {
  text->vertices.clear();
  text->batches.clear();
  
  int w = stash->params.width;
  int h = stash->params.height;
  float u = w == 0 ? 0 : (1.0f / w);
  float v = h == 0 ? 0 : (1.0f / h);
  
  text->vertices.reserve(6 + 6 + 6 * stash->atlas->nodes.size());
  
  // Draw background
  fons__vertex(stash, x+0, y+0, u, v, 0x0fffffff, text);
  fons__vertex(stash, x+w, y+h, u, v, 0x0fffffff, text);
  fons__vertex(stash, x+w, y+0, u, v, 0x0fffffff, text);
  
  fons__vertex(stash, x+0, y+0, u, v, 0x0fffffff, text);
  fons__vertex(stash, x+0, y+h, u, v, 0x0fffffff, text);
  fons__vertex(stash, x+w, y+h, u, v, 0x0fffffff, text);
  
  // Draw texture
  fons__vertex(stash, x+0, y+0, 0, 0, 0xffffffff, text);
  fons__vertex(stash, x+w, y+h, 1, 1, 0xffffffff, text);
  fons__vertex(stash, x+w, y+0, 1, 0, 0xffffffff, text);
  
  fons__vertex(stash, x+0, y+0, 0, 0, 0xffffffff, text);
  fons__vertex(stash, x+0, y+h, 0, 1, 0xffffffff, text);
  fons__vertex(stash, x+w, y+h, 1, 1, 0xffffffff, text);
  
  // Debug draw atlas
  for (usize i = 0, size = stash->atlas->nodes.size(); i < size; i++) {
    FONSatlasNode* n = &stash->atlas->nodes[i];
    
    fons__vertex(stash, x+n->x+0, y+n->y+0, u, v, 0xc00000ff, text);
    fons__vertex(stash, x+n->x+n->width, y+n->y+1, u, v, 0xc00000ff, text);
    fons__vertex(stash, x+n->x+n->width, y+n->y+0, u, v, 0xc00000ff, text);
    
    fons__vertex(stash, x+n->x+0, y+n->y+0, u, v, 0xc00000ff, text);
    fons__vertex(stash, x+n->x+0, y+n->y+1, u, v, 0xc00000ff, text);
    fons__vertex(stash, x+n->x+n->width, y+n->y+1, u, v, 0xc00000ff, text);
  }
  
  fons__flush(stash);
  
  // Define the batch
  text->batches.emplace_back(/*firstVertex*/ 0, stash->currentTextureIndex);
}

float fonsTextBounds(FONScontext* stash, float x, float y, const char* str, const char* end, float* bounds) {
  if (stash == NULL) { return 0; }
  const FONSstate* state = fons__getState(stash);
  if (state->font < 0 || state->font >= stash->fonts.size()) { return 0; }
  FONSfont* font = stash->fonts[state->font];
  if (font->data.empty()) { return 0; }
  
  const short isize = (short)(state->size * 10.0f);
  const short iblur = (short)state->blur;
  
  const float scale = fons__tt_getPixelHeightScale(&font->font, (float)isize / 10.0f);
  
  const float scaling = state->roundToInt ? 1 : state->scaling;
  const float lineHeight = scaling * (font->lineh*isize/10.0f);
  
  // Align vertically.
  y += fons__getVertAlign(stash, font, state->align, isize);
  
  float minx = x;
  float maxx = x;
  float miny = y;
  float maxy = y;
  const float startx = x;
  
  if (end == NULL) {
    end = str + strlen(str);
  }
  
  int prevGlyphIndex = -1;
  unsigned int codepoint;
  unsigned int utf8state = 0;
  FONSquad q;
  
  for (; str != end; ++str) {
    if (*str == '\n') {
      x = startx;
      y += lineHeight;
      
      prevGlyphIndex = -1;
      utf8state = 0;
      continue;
    }
    
    if (fons__decutf8(&utf8state, &codepoint, *(const unsigned char*)str)) {
      continue;
    }
    
    FONSglyph* glyph = fons__getGlyph(stash, font, codepoint, isize, iblur);
    if (glyph != NULL) {
      fons__getQuad(stash, font, prevGlyphIndex, glyph, scale, state->spacing, state->roundToInt, state->scaling, &x, &y, &q);
      if (q.x0 < minx) minx = q.x0;
      if (q.x1 > maxx) maxx = q.x1;
      if (stash->params.flags & FONS_ZERO_TOPLEFT) {
        if (q.y0 < miny) miny = q.y0;
        if (q.y1 > maxy) maxy = q.y1;
      } else {
        if (q.y1 < miny) miny = q.y1;
        if (q.y0 > maxy) maxy = q.y0;
      }
    }
    prevGlyphIndex = glyph != NULL ? glyph->index : -1;
  }
  
  const float advance = x - startx;
  
  // Align horizontally
  if (state->align & FONS_ALIGN_LEFT) {
    // empty
  } else if (state->align & FONS_ALIGN_RIGHT) {
    minx -= advance;
    maxx -= advance;
  } else if (state->align & FONS_ALIGN_CENTER) {
    minx -= advance * 0.5f;
    maxx -= advance * 0.5f;
  }
  
  if (bounds) {
    bounds[0] = minx;
    bounds[1] = miny;
    bounds[2] = maxx;
    bounds[3] = maxy;
  }
  
  return advance;
}

void fonsVertMetrics(FONScontext* stash, float* ascender, float* descender, float* lineh) {
  FONSfont* font;
  FONSstate* state = fons__getState(stash);
  short isize;
 
  if (stash == NULL) return;
  if (state->font < 0 || state->font >= stash->fonts.size()) return;
  font = stash->fonts[state->font];
  isize = (short)(state->size*10.0f);
  if (font->data.empty()) return;
  
  const float scaling = state->roundToInt ? 1 : state->scaling;
  
  if (ascender)
    *ascender = scaling * (font->ascender*isize/10.0f);
  if (descender)
    *descender = scaling * (font->descender*isize/10.0f);
  if (lineh)
    *lineh = scaling * (font->lineh*isize/10.0f);
}

void fonsLineBounds(FONScontext* stash, float y, float* miny, float* maxy) {
  FONSfont* font;
  FONSstate* state = fons__getState(stash);
  short isize;
  
  if (stash == NULL) return;
  if (state->font < 0 || state->font >= stash->fonts.size()) return;
  font = stash->fonts[state->font];
  isize = (short)(state->size*10.0f);
  if (font->data.empty()) return;
  
  y += fons__getVertAlign(stash, font, state->align, isize);
  
  const float scaling = state->roundToInt ? 1 : state->scaling;
  
  if (stash->params.flags & FONS_ZERO_TOPLEFT) {
    *miny = y - scaling * (font->ascender * (float)isize/10.0f);
    *maxy = *miny + scaling * (font->lineh*isize/10.0f);
  } else {
    *maxy = y + scaling * (font->descender * (float)isize/10.0f);
    *miny = *maxy - scaling * (font->lineh*isize/10.0f);
  }
}

const unsigned char* fonsGetTextureData(FONScontext* stash, int* width, int* height) {
  if (width != NULL) {
    *width = stash->params.width;
  }
  if (height != NULL) {
    *height = stash->params.height;
  }
  return stash->texData.data();
}

int fonsValidateTexture(FONScontext* stash, int* dirty) {
  if (stash->dirtyRect[0] < stash->dirtyRect[2] && stash->dirtyRect[1] < stash->dirtyRect[3]) {
    dirty[0] = stash->dirtyRect[0];
    dirty[1] = stash->dirtyRect[1];
    dirty[2] = stash->dirtyRect[2];
    dirty[3] = stash->dirtyRect[3];
    // Reset dirty rect
    stash->dirtyRect[0] = stash->params.width;
    stash->dirtyRect[1] = stash->params.height;
    stash->dirtyRect[2] = 0;
    stash->dirtyRect[3] = 0;
    return 1;
  }
  return 0;
}

void fonsDeleteInternal(FONScontext* stash) {
  if (stash == NULL) { return; }
  
  if (stash->params.renderDelete) {
    stash->params.renderDelete(stash->params.userPtr);
  }
  
  for (int i = 0; i < stash->fonts.size(); ++i) {
    fons__freeFont(stash->fonts[i]);
  }

  if (stash->atlas) fons__deleteAtlas(stash->atlas);
  delete stash;
}

void fonsSetErrorCallback(FONScontext* stash, void (*callback)(void* uptr, int error, int val), void* userPtr) {
  if (stash == NULL) return;
  stash->handleError = callback;
  stash->errorUserPtr = userPtr;
}

void fonsGetAtlasSize(FONScontext* stash, int* width, int* height) {
  if (stash == NULL) return;
  *width = stash->params.width;
  *height = stash->params.height;
}

int fonsExpandAtlas(FONScontext* stash, int width, int height) {
  int maxy = 0;
  if (stash == NULL) return 0;
  
  width = max(width, stash->params.width);
  height = max(height, stash->params.height);
  
  if (width == stash->params.width && height == stash->params.height)
    return 1;
  
  // Flush pending glyphs.
  fons__flush(stash);
  
  // Create new texture
  ++ stash->currentTextureIndex;
  if (stash->params.renderResize != NULL) {
    if (stash->params.renderResize(stash->params.userPtr, width, height, stash->currentTextureIndex) == 0) {
      return 0;
    }
  }
  
  // Copy old texture data over.
  vector<u8> data(width * height);
  for (int i = 0; i < stash->params.height; i++) {
    unsigned char* dst = &data[i*width];
    unsigned char* src = &stash->texData[i*stash->params.width];
    memcpy(dst, src, stash->params.width);
    if (width > stash->params.width)
      memset(dst+stash->params.width, 0, width - stash->params.width);
  }
  if (height > stash->params.height)
    memset(&data[stash->params.height * width], 0, (height - stash->params.height) * width);
  
  stash->texData = std::move(data);
  
  // Increase atlas size
  fons__atlasExpand(stash->atlas, width, height);
  
  // Add existing data as dirty.
  for (usize i = 0, size = stash->atlas->nodes.size(); i < size; i++) {
    maxy = max<int>(maxy, stash->atlas->nodes[i].y);
  }
  stash->dirtyRect[0] = 0;
  stash->dirtyRect[1] = 0;
  stash->dirtyRect[2] = stash->params.width;
  stash->dirtyRect[3] = maxy;

  stash->params.width = width;
  stash->params.height = height;
  stash->itw = 1.0f/stash->params.width;
  stash->ith = 1.0f/stash->params.height;

  return 1;
}

int fonsResetAtlas(FONScontext* stash, int width, int height) {
  if (stash == NULL) { return 0; }
  
  // Flush pending glyphs.
  fons__flush(stash);
  
  // Create new texture
  ++ stash->currentTextureIndex;
  if (stash->params.renderResize != NULL) {
    if (stash->params.renderResize(stash->params.userPtr, width, height, stash->currentTextureIndex) == 0) {
      return 0;
    }
  }
  
  // Reset atlas
  fons__atlasReset(stash->atlas, width, height);
  
  // Clear texture data.
  stash->texData.resize(width * height);
  memset(stash->texData.data(), 0, width * height);

  // Reset dirty rect
  stash->dirtyRect[0] = width;
  stash->dirtyRect[1] = height;
  stash->dirtyRect[2] = 0;
  stash->dirtyRect[3] = 0;
  
  // Reset cached glyphs
  for (int i = 0; i < stash->fonts.size(); i++) {
    FONSfont* font = stash->fonts[i];
    font->glyphs.clear();
    for (int j = 0; j < FONS_HASH_LUT_SIZE; j++) {
      font->lut[j] = -1;
    }
  }
  
  stash->params.width = width;
  stash->params.height = height;
  stash->itw = 1.0f/stash->params.width;
  stash->ith = 1.0f/stash->params.height;
  
  // Add white rect at 0,0 for debug drawing.
  fons__addWhiteRect(stash, 2,2);
  
  return 1;
}

}
