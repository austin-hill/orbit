#pragma once

#include <chrono>
#include <cstdint>
#include <print>

class timer
{
public:
  timer() : _time_ns(0)
  {
  }

  inline void start()
  {
    _start = std::chrono::high_resolution_clock::now();
  }
  inline void restart()
  {
    _time_ns = 0;
    _start = std::chrono::high_resolution_clock::now();
  }
  inline void stop()
  {
    _time_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - _start).count();
  }
  uint64_t get_ns()
  {
    return _time_ns;
  }
  uint64_t get_us()
  {
    return _time_ns / 1e3;
  }
  uint64_t get_ms()
  {
    return _time_ns / 1e6;
  }
  void print_ns()
  {
    std::println("Time elapsed: {}ns", _time_ns);
  }
  void print_us()
  {
    std::println("Time elapsed: {:.2f}us", _time_ns / 1e3);
  }
  void print_ms()
  {
    std::println("Time elapsed: {:.2f}ms", _time_ns / 1e6);
  }

private:
  std::chrono::high_resolution_clock::time_point _start;
  uint64_t _time_ns;
};
