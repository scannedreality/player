#include "scan_studio/viewer_common/xrvideo/video_thread.hpp"

#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#ifdef HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif
#if defined(__FreeBSD__)
#define cpu_set_t cpuset_t
#endif

namespace scan_studio {

int dav1d_num_logical_processors_copy() {
#ifdef _WIN32
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
  GROUP_AFFINITY affinity;
  if (GetThreadGroupAffinity(GetCurrentThread(), &affinity)) {
    int num_processors = 1;
    while (affinity.Mask &= affinity.Mask - 1)
      num_processors++;
    return num_processors;
  }
#else
  SYSTEM_INFO system_info;
  GetNativeSystemInfo(&system_info);
  return system_info.dwNumberOfProcessors;
#endif
#elif defined(HAVE_PTHREAD_GETAFFINITY_NP) && defined(CPU_COUNT)
  cpu_set_t affinity;
  if (!pthread_getaffinity_np(pthread_self(), sizeof(affinity), &affinity))
    return CPU_COUNT(&affinity);
#elif defined(__APPLE__)
  int num_processors;
  size_t length = sizeof(num_processors);
  if (!sysctlbyname("hw.logicalcpu", &num_processors, &length, NULL, 0))
    return num_processors;
#elif defined(_SC_NPROCESSORS_ONLN)
  return (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif
  return 1;
}

}
