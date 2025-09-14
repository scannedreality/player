#pragma once

#include <stdint.h>
#include <stdio.h>

#if defined(_WIN32)
  #include <io.h>
#endif

/// Portable version of ftell(), supporting large files.
/// On error, returns -1.
///
/// Motivation: The standard ftell() returns a long, which is 32-bit on Windows and thus fails for large files there.
/// See: https://stackoverflow.com/questions/30867831/portable-support-for-large-files
inline int64_t portable_ftell(FILE* file) {
  #if defined(_WIN32)
    // I have seen two reports that _ftelli64() returns erroneous values:
    // https://stackoverflow.com/a/30867832
    // https://stackoverflow.com/questions/4034227/on-windows-fseeki64-does-not-seek-to-seek-end-correctly-for-large-files
    // Thus, this can be used as alternative to _ftelli64():
    // return _telli64(_fileno(file));
    
    return _ftelli64(file);
  #else
    return ftell(file);
  #endif
}

/// Portable version of fseek(), supporting large files.
/// On success, returns 0.
///
/// Motivation: The standard fseek() uses a long offset, which is 32-bit on Windows and thus fails for large files there.
/// See: https://stackoverflow.com/questions/30867831/portable-support-for-large-files
inline int portable_fseek(FILE* stream, int64_t offset, int origin) {
  #if defined(_WIN32)
    // According to this, _fseeki64() might be broken:
    // https://stackoverflow.com/questions/4034227/on-windows-fseeki64-does-not-seek-to-seek-end-correctly-for-large-files
    // However, this does not seem to work as alternative to _fseeki64():
    // return (_lseeki64(_fileno(stream), offset, origin) == -1L) ? 1 : 0;
    
    return _fseeki64(stream, offset, origin);
  #else
    return fseek(stream, offset, origin);
  #endif
}
