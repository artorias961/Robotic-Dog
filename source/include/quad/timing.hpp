#pragma once
// Precise periodic sleep for the 100 Hz control loop.
//
// On Windows, std::this_thread::sleep_until follows the 15.6 ms system timer:
// measured on the dev PC, 187 of 299 ticks came ~15 ms late and 107 fired
// back-to-back, and timeBeginPeriod(1) did not fix it. A high-resolution
// waitable timer (Windows 10 1803+) measured 9.5-10.9 ms on every tick.
#include <chrono>
#include <thread>

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX  // keep std::min/std::max usable
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN  // no legacy winsock.h clash with winsock2.h
  #endif
  #include <windows.h>
  #ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
    #define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
  #endif
#endif

namespace quad {

class PeriodicSleeper {
 public:
  PeriodicSleeper() {
#ifdef _WIN32
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                    TIMER_ALL_ACCESS);
#endif
  }
  ~PeriodicSleeper() {
#ifdef _WIN32
    if (timer_) CloseHandle(timer_);
#endif
  }
  PeriodicSleeper(const PeriodicSleeper&) = delete;
  PeriodicSleeper& operator=(const PeriodicSleeper&) = delete;

  void sleep_until(std::chrono::steady_clock::time_point t) {
#ifdef _WIN32
    if (timer_) {
      const auto rem = t - std::chrono::steady_clock::now();
      if (rem <= std::chrono::nanoseconds::zero()) return;
      LARGE_INTEGER due;  // negative = relative, in 100 ns units
      due.QuadPart = -static_cast<LONGLONG>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(rem).count() / 100);
      if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
        WaitForSingleObject(timer_, INFINITE);
        return;
      }
    }
#endif
    std::this_thread::sleep_until(t);  // Linux, or Windows without HR timers
  }

 private:
#ifdef _WIN32
  HANDLE timer_ = nullptr;
#endif
};

}  // namespace quad
