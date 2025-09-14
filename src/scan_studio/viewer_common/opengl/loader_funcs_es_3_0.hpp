GLES_PROC_VOID(glBindBufferBase, (GLenum a, GLuint b, GLuint c), (a, b, c))

GLES_PROC_VOID(glTransformFeedbackVaryings, (GLuint a, GLsizei b, const char * const* c, GLenum d), (a, b, c, d))
GLES_PROC_VOID(glBeginTransformFeedback, (GLenum a), (a))
GLES_PROC_VOID(glEndTransformFeedback, (void), ())

GLES_PROC_VOID(glDrawBuffers, (GLsizei a, const GLenum * b), (a, b))

GLES_PROC(GLuint, glGetUniformBlockIndex, (GLuint a, const GLchar * b), (a, b))
GLES_PROC_VOID(glGetActiveUniformBlockiv, (GLuint a, GLuint b, GLenum c, GLint * d), (a, b, c, d))
GLES_PROC_VOID(glUniformBlockBinding, (GLuint a, GLuint b, GLuint c), (a, b, c))

GLES_PROC_VOID(glVertexAttribIPointer, (GLuint a, GLint b, GLenum c, GLsizei d, const void * e), (a, b, c, d, e))

GLES_PROC(GLsync, glFenceSync, (GLenum a, GLbitfield b), (a, b))
GLES_PROC(GLenum, glClientWaitSync, (GLsync a, GLbitfield b, GLuint64 c), (a, b, c))
GLES_PROC_VOID(glWaitSync, (GLsync a, GLbitfield b, GLuint64 c), (a, b, c))
GLES_PROC_VOID(glGetSynciv, (GLsync a, GLenum b, GLsizei c, GLsizei * d, GLint * e), (a, b, c, d, e))
GLES_PROC_VOID(glDeleteSync, (GLsync a), (a))

GLES_PROC_VOID(glBindVertexArray, (GLuint a), (a));
GLES_PROC_VOID(glGenVertexArrays, (GLsizei a, GLuint * b), (a, b));

GLES_PROC_VOID(glTexStorage2D, (GLenum a, GLsizei b, GLenum c, GLsizei d, GLsizei e), (a, b, c, d, e));

#if !defined(__EMSCRIPTEN__)
  GLES_PROC(void*, glMapBufferRange, (GLenum a, GLintptr b, GLsizeiptr c, GLbitfield d), (a, b, c, d));
  GLES_PROC(GLboolean, glUnmapBuffer, (GLenum a), (a));
#endif
