#include "libvis/io/globals.h"

fs::path gAppPath;

#ifdef __ANDROID__
AAssetManager* gAssetManager = nullptr;
#endif
