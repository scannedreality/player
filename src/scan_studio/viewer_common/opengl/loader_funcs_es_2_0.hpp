/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2020 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

// NOTE: The original function table from SDL (with the above license) at "../src/render/opengles2/SDL_gles2funcs.h" seemed to be incomplete,
//       so I manually added some additional functions that I used here in this version.

GLES_PROC_VOID(glActiveTexture, (GLenum a), (a))
GLES_PROC_VOID(glAttachShader, (GLuint a, GLuint b), (a, b))
GLES_PROC_VOID(glBindAttribLocation, (GLuint a, GLuint b, const char * c), (a, b, c))
GLES_PROC_VOID(glBindTexture, (GLenum a, GLuint b), (a, b))
GLES_PROC_VOID(glBlendEquationSeparate, (GLenum a, GLenum b), (a, b))
GLES_PROC_VOID(glBlendFunc, (GLenum a, GLenum b), (a, b))
GLES_PROC_VOID(glBlendFuncSeparate, (GLenum a, GLenum b, GLenum c, GLenum d), (a, b, c, d))
GLES_PROC_VOID(glClear, (GLbitfield a), (a))
GLES_PROC_VOID(glClearColor, (GLclampf a, GLclampf b, GLclampf c, GLclampf d), (a, b, c, d))
GLES_PROC_VOID(glCompileShader, (GLuint a), (a))
GLES_PROC(GLuint, glCreateProgram, (void), ())
GLES_PROC(GLuint, glCreateShader, (GLenum a), (a))
GLES_PROC_VOID(glCullFace, (GLenum a), (a))
GLES_PROC_VOID(glDeleteProgram, (GLuint a), (a))
GLES_PROC_VOID(glDeleteShader, (GLuint a), (a))
GLES_PROC_VOID(glDeleteTextures, (GLsizei a, const GLuint * b), (a, b))
GLES_PROC_VOID(glDepthFunc, (GLenum a), (a))
GLES_PROC_VOID(glDepthMask, (GLboolean a), (a))
GLES_PROC_VOID(glDepthRangef, (GLfloat a, GLfloat b), (a, b))
GLES_PROC_VOID(glDetachShader, (GLuint a, GLuint b), (a, b))
GLES_PROC_VOID(glDisable, (GLenum a), (a))
GLES_PROC_VOID(glDisableVertexAttribArray, (GLuint a), (a))
GLES_PROC_VOID(glDrawArrays, (GLenum a, GLint b, GLsizei c), (a, b, c))
GLES_PROC_VOID(glDrawElements, (GLenum a, GLsizei b, GLenum c, const void * d), (a, b, c, d))
GLES_PROC_VOID(glEnable, (GLenum a), (a))
GLES_PROC_VOID(glEnableVertexAttribArray, (GLuint a), (a))
GLES_PROC_VOID(glFlush, (void), ())
GLES_PROC_VOID(glFinish, (void), ())
GLES_PROC_VOID(glFrontFace, (GLenum a), (a))
GLES_PROC_VOID(glGenFramebuffers, (GLsizei a, GLuint * b), (a, b))
GLES_PROC_VOID(glGenTextures, (GLsizei a, GLuint * b), (a, b))
GLES_PROC_VOID(glGetBooleanv, (GLenum a, GLboolean * b), (a, b))
GLES_PROC_VOID(glGetBufferParameteriv, (GLenum a, GLenum b, GLint * c), (a, b, c))
GLES_PROC(const GLubyte *, glGetString, (GLenum a), (a))
GLES_PROC(GLenum, glGetError, (void), ())
GLES_PROC_VOID(glGetIntegerv, (GLenum a, GLint * b), (a, b))
GLES_PROC_VOID(glGetProgramiv, (GLuint a, GLenum b, GLint * c), (a, b, c))
GLES_PROC_VOID(glGetShaderInfoLog, (GLuint a, GLsizei b, GLsizei * c, char * d), (a, b, c, d))
GLES_PROC_VOID(glGetShaderiv, (GLuint a, GLenum b, GLint * c), (a, b, c))
GLES_PROC(GLint, glGetUniformLocation, (GLuint a, const char * b), (a, b))
GLES_PROC_VOID(glLinkProgram, (GLuint a), (a))
GLES_PROC_VOID(glPixelStorei, (GLenum a, GLint b), (a, b))
GLES_PROC_VOID(glReadPixels, (GLint a, GLint b, GLsizei c, GLsizei d, GLenum e, GLenum f, GLvoid* g), (a, b, c, d, e, f, g))
GLES_PROC_VOID(glRenderbufferStorage, (GLenum a, GLenum b, GLsizei c, GLsizei d), (a, b, c, d))
GLES_PROC_VOID(glGetRenderbufferParameteriv, (GLenum a, GLenum b, GLint* c), (a, b, c))
GLES_PROC_VOID(glScissor, (GLint a, GLint b, GLsizei c, GLsizei d), (a, b, c, d))
GLES_PROC_VOID(glShaderBinary, (GLsizei a, const GLuint * b, GLenum c, const void * d, GLsizei e), (a, b, c, d, e))
#if __NACL__
GLES_PROC_VOID(glShaderSource, (GLuint a, GLsizei b, const GLchar ** c, const GLint * d), (a, b, c, d))
#else
GLES_PROC_VOID(glShaderSource, (GLuint a, GLsizei b, const GLchar* const* c, const GLint * d), (a, b, c, d))
#endif
GLES_PROC_VOID(glTexImage2D, (GLenum a, GLint b, GLint c, GLsizei d, GLsizei e, GLint f, GLenum g, GLenum h, const void * i), (a, b, c, d, e, f, g, h, i))
GLES_PROC_VOID(glTexParameteri, (GLenum a, GLenum b, GLint c), (a, b, c))
GLES_PROC_VOID(glTexSubImage2D, (GLenum a, GLint b, GLint c, GLint d, GLsizei e, GLsizei f, GLenum g, GLenum h, const GLvoid * i), (a, b, c, d, e, f, g, h, i))
GLES_PROC_VOID(glUniform1f, (GLint a, GLfloat b), (a, b))
GLES_PROC_VOID(glUniform2f, (GLint a, GLfloat b, GLfloat c), (a, b, c))
GLES_PROC_VOID(glUniform3f, (GLint a, GLfloat b, GLfloat c, GLfloat d), (a, b, c, d))
GLES_PROC_VOID(glUniform1i, (GLint a, GLint b), (a, b))
GLES_PROC_VOID(glUniform4f, (GLint a, GLfloat b, GLfloat c, GLfloat d, GLfloat e), (a, b, c, d, e))
GLES_PROC_VOID(glUniformMatrix2fv, (GLint a, GLsizei b, GLboolean c, const GLfloat * d), (a, b, c, d))
GLES_PROC_VOID(glUniformMatrix4fv, (GLint a, GLsizei b, GLboolean c, const GLfloat * d), (a, b, c, d))
GLES_PROC_VOID(glUseProgram, (GLuint a), (a))
GLES_PROC_VOID(glVertexAttribPointer, (GLuint a, GLint b, GLenum c, GLboolean d, GLsizei e, const void * f), (a, b, c, d, e, f))
GLES_PROC_VOID(glViewport, (GLint a, GLint b, GLsizei c, GLsizei d), (a, b, c, d))
GLES_PROC_VOID(glBindFramebuffer, (GLenum a, GLuint b), (a, b))
GLES_PROC_VOID(glFramebufferTexture2D, (GLenum a, GLenum b, GLenum c, GLuint d, GLint e), (a, b, c, d, e))
GLES_PROC(GLenum, glCheckFramebufferStatus, (GLenum a), (a))
GLES_PROC_VOID(glDeleteFramebuffers, (GLsizei a, const GLuint * b), (a, b))
GLES_PROC(GLint, glGetAttribLocation, (GLuint a, const GLchar * b), (a, b))
GLES_PROC_VOID(glGetProgramInfoLog, (GLuint a, GLsizei b, GLsizei* c, GLchar* d), (a, b, c, d))
GLES_PROC_VOID(glGenBuffers, (GLsizei a, GLuint * b), (a, b))
GLES_PROC_VOID(glDeleteBuffers, (GLsizei a, const GLuint * b), (a, b))
GLES_PROC_VOID(glBindBuffer, (GLenum a, GLuint b), (a, b))
GLES_PROC_VOID(glBufferData, (GLenum a, GLsizeiptr b, const GLvoid * c, GLenum d), (a, b, c, d))
GLES_PROC_VOID(glBufferSubData, (GLenum a, GLintptr b, GLsizeiptr c, const GLvoid * d), (a, b, c, d))
