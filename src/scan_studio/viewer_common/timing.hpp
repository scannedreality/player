#pragma once

#include <chrono>
#include <cmath>
#include <stddef.h>
#include <stdint.h>

namespace scan_studio {
using namespace std;

typedef chrono::steady_clock Clock;
typedef Clock::time_point TimePoint;

typedef chrono::duration<double> SecondsDuration;
typedef chrono::duration<double, milli> MillisecondsDuration;
typedef chrono::duration<int64_t, std::nano> NanosecondsDuration;

inline double SecondsFromTo(const TimePoint& from, const TimePoint& to) {
  return SecondsDuration(to - from).count();
}

inline double MillisecondsFromTo(const TimePoint& from, const TimePoint& to) {
  return MillisecondsDuration(to - from).count();
}

inline int64_t NanosecondsFromTo(const TimePoint& from, const TimePoint& to) {
  return NanosecondsDuration(to - from).count();
}

constexpr inline double NanosecondsToSeconds(int64_t nanoseconds) {
  return nanoseconds / (1000.0 * 1000.0 * 1000.0);
}

/*constexpr*/ inline int64_t SecondsToNanoseconds(double seconds) {
  return round(seconds * 1000.0 * 1000.0 * 1000.0);
}

}
