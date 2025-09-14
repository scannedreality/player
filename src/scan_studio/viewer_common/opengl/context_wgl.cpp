#if defined(_WIN32)
#include "scan_studio/viewer_common/opengl/context_wgl.hpp"

#include <strsafe.h>

#include <loguru.hpp>

#include "scan_studio/viewer_common/opengl/loader.hpp"

namespace scan_studio {

// Function adapted from:
// https://learn.microsoft.com/en-us/windows/win32/debug/retrieving-the-last-error-code
static void LogLastError(const char* lpszFunction) {
  const DWORD err = GetLastError();
  
  LPVOID lpMsgBuf;
  FormatMessage(
      FORMAT_MESSAGE_ALLOCATE_BUFFER |
      FORMAT_MESSAGE_FROM_SYSTEM |
      FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL,
      err,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      (LPTSTR) &lpMsgBuf,
      0, NULL);
  
  LPVOID lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
      (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));
  
  LOG(ERROR) << lpszFunction << " failed with error " << err << ": " << lpMsgBuf;
  
  LocalFree(lpMsgBuf);
  LocalFree(lpDisplayBuf);
}

// Function taken from:
// https://www.khronos.org/opengl/wiki/Load_OpenGL_Functions
static void* GetWinGLFuncAddress(const char* name) {
  void* p = (void*)wglGetProcAddress(name);
  if (p == 0 ||
     (p == (void*)0x1) || (p == (void*)0x2) || (p == (void*)0x3) ||
     (p == (void*)-1)) {
    HMODULE module = LoadLibraryA("opengl32.dll");
    p = (void*)GetProcAddress(module, name);
  }
  return p;
}


GLContextWGL::~GLContextWGL() {
  Destroy();
}

bool GLContextWGL::InitializeWindowless(GLContextWGL* sharing_context) {
  if (!sharing_context) {
    LOG(ERROR) << "The WGL context implementation requires sharing_context to be present";
    return false;
  }
  
  // Query the wglGetExtensionsStringARB() function
  auto pwglGetExtensionsStringARB = reinterpret_cast<const char* (*)(HDC hdc)>(GetWinGLFuncAddress("wglGetExtensionsStringARB"));
  if (!pwglGetExtensionsStringARB) { LOG(ERROR) << "wglGetExtensionsStringARB() not found"; return false; }
  
  // Go through all supported WGL extensions to see whether WGL_ARB_create_context_profile is supported
  bool have_WGL_ARB_create_context_profile = false;
  
  auto foundExtension = [&have_WGL_ARB_create_context_profile](const string& extensionName) {
    // LOG(1) << "Found WGL extension: " << extensionName;
    
    if (extensionName == "WGL_ARB_create_context_profile") {
      have_WGL_ARB_create_context_profile = true;
    }
  };
  
  const string wglExtensions = pwglGetExtensionsStringARB(wglGetCurrentDC());
  string extensionName = "";
  for (usize i = 0; i < wglExtensions.size(); ++ i) {
    char c = wglExtensions[i];
    if (c == ' ') {
      if (!extensionName.empty()) {
        foundExtension(extensionName);
      }
      extensionName.clear();
    } else {
      extensionName += c;
    }
  }
  if (!extensionName.empty()) {
    foundExtension(extensionName);
  }
  
  if (!have_WGL_ARB_create_context_profile) {
    LOG(ERROR) << "WGL_ARB_create_context_profile not found";
    return false;
  }
  
  // Query the wglCreateContextAttribsARB() function from WGL_ARB_create_context_profile
  // See: https://registry.khronos.org/OpenGL/extensions/ARB/WGL_ARB_create_context.txt
  auto pwglCreateContextAttribsARB = reinterpret_cast<HGLRC (*)(HDC hDC, HGLRC hShareContext, const int* attribList)>(GetWinGLFuncAddress("wglCreateContextAttribsARB"));
  
  // Create the new context
  dc = wglGetCurrentDC();
  
  GLint majorVersion;
  gl.glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
  
  GLint minorVersion;
  gl.glGetIntegerv(GL_MINOR_VERSION, &minorVersion);
  
  GLint profileMask;
  gl.glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask);
  
  // LOG(1) << "GLContextWGL: Existing context has version " << majorVersion << "." << minorVersion << ", profile mask " << profileMask;
  
  const int attribs[] = {
      /*WGL_CONTEXT_MAJOR_VERSION_ARB*/ 0x2091, majorVersion,
      /*WGL_CONTEXT_MINOR_VERSION_ARB*/ 0x2092, minorVersion,
      /*WGL_CONTEXT_PROFILE_MASK_ARB*/ 0x9126, profileMask,
      0};
  context = pwglCreateContextAttribsARB(dc, sharing_context->context, attribs);
  if (context == nullptr) {
    LogLastError("wglCreateContextAttribsARB()");
    dc = nullptr;
    return false;
  }
  
  return true;
}

void GLContextWGL::AttachToCurrent() {
  Destroy();
  
  dc = wglGetCurrentDC();
  context = wglGetCurrentContext();
}

void GLContextWGL::Detach() {
  dc = nullptr;
  context = nullptr;
}

void GLContextWGL::Destroy() {
  if (context) {
    if (wglDeleteContext(context) == FALSE) {
      LogLastError("wglDeleteContext()");
    }
    context = nullptr;
  }
  
  dc = nullptr;
}

bool GLContextWGL::MakeCurrent() {
  if (wglMakeCurrent(dc, context) == FALSE) {
    LogLastError("wglMakeCurrent()");
    return false;
  }
  return true;
}

}

#endif
