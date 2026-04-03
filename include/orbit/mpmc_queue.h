#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <new>
#include <type_traits>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

#if defined(__MSC_VER)
#include <intrin.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define ORBIT_FORCE_INLINE inline __attribute__((always_inline))
#else
#define ORBIT_FORCE_INLINE __forceinline
#endif

namespace orbit::detail
{
template <auto Start, auto End, typename F>
ORBIT_FORCE_INLINE constexpr void constexpr_for(F&& f) noexcept
{
  if constexpr (Start < End)
  {
    f();
    constexpr_for<Start + 1, End>(f);
  }
}

ORBIT_FORCE_INLINE void do_single_pause() noexcept
{
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)

  _mm_pause();

#elif defined(__aarch64__) || defined(_M_ARM64) || ((defined(__arm__) || defined(_M_ARM)) && defined(__ARM_ARCH) && __ARM_ARCH >= 7)

#if defined(__GNUC__) || defined(__clang__)
  asm volatile("isb" ::: "memory");
#else
  __isb(_ARM64_BARRIER_SY);
#endif

#elif defined(__arm__) || defined(_M_ARM)

#if defined(__GNUC__) || defined(__clang__)
  asm volatile("yield" ::: "memory");
#else
  __yield();
#endif

#else
#warning "Unknown CPU architecture - using nop for spin loop pause."

#if defined(__GNUC__) || defined(__clang__)
  asm volatile("nop");
#else
  __nop();
#endif

#endif
}
} // namespace orbit::detail

namespace orbit
{
template <size_t NUM_PAUSES = 3>
ORBIT_FORCE_INLINE void spin_pause() noexcept
{
  orbit::detail::constexpr_for<0, NUM_PAUSES>(detail::do_single_pause);
}

/*
Disabling these warnings for the same reason as e.g. Folly - the vast majority of people are not building different parts of their application with different platform
flags and then linking them later. If you are doing this for some reason (for example, using -mcpu or -march=native for only part of your build), then you should
hardcode this to 64/128 as appropriate.
*/
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winterference-size"
#endif
constexpr size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

template <typename T>
concept copyable = std::is_trivially_copyable_v<T> && (sizeof(T) <= 16); // Copy types that can be passed in two registers, otherwise move

template <typename T>
concept copyable_or_nothrow_move_assignable = copyable<T> || std::is_nothrow_move_assignable_v<T>; // Copy types that can be passed in two registers, otherwise move

template <size_t SIZE>
concept power_of_two = std::has_single_bit(SIZE);

/*
@tparam T Data type
@tparam SIZE Number of elements in the circular buffer (must be a power of two)
@tparam MINIMISE_LATENCY When true, optimise for minimum latency, else optimise for maximum throughput
@tparam NONBLOCKING When true, queue is truly lock-free. Can be set to false for even lower latency by removing a CAS operation.
@tparam PAUSE_SHORT Number of pause instructions between each spin loop
@tparam PAUSE_LONG Number of pause instructions between each spin loop after failed CAS in throughput mode only

To get the best possible performance, benchmarks can be run to determine the best possible pause lengths.
Based on testing, sensible choices for x86 processors might be the following:
  - PAUSE_SHORT = 180 / NUM_CYCLES_PER_PAUSE
  - PAUSE_LONG = 3000 / NUM_CYCLES_PER_PAUSE (only used when maximising throughput)
Where NUM_CYCLES_PER_PAUSE is dependent on the target CPU - on modern AMD/Intel CPUs it is likely around the range of 30-150 clock cycles, but may be shorter on some older models.

For non x86 users:
If your CPU is ARMv7 or above, NUM_CYCLES_PER_PAUSE is instead the number of cycles a single ISB instruction takes, i.e. how long a pipeline refill takes.
If your CPU is ARM and very old, or some other architecture, these parameters may not help much, as they encode either YIELD or NOP instructions in this case.
*/
template <typename T, size_t SIZE, bool MINIMISE_LATENCY = true, bool NONBLOCKING = true, size_t PAUSE_SHORT = 3, size_t PAUSE_LONG = 40>
  requires power_of_two<SIZE> && std::is_default_constructible_v<T> && copyable_or_nothrow_move_assignable<T>
class mpmc_queue
{
public:
  mpmc_queue() noexcept(std::is_nothrow_default_constructible_v<T>)
  {
    _front.store(0, std::memory_order_relaxed);
    _back.store(0, std::memory_order_relaxed);

    for (size_t i = 0; i < SIZE; ++i)
    {
      _states[i].store(slot_state::EMPTY, std::memory_order_relaxed);
    }
  }

  // On x86: May return false negative, but not false positive. On ARM: No formal guarantees
  bool was_empty() noexcept
  {
    uint64_t prev_front = _front.load(std::memory_order_acquire); // Ensure prev_back is loaded AFTER this
    uint64_t prev_back = _back.load(std::memory_order_acquire);

    return prev_back <= prev_front;
  }

  // On x86: May return false negative, but not false positive. On ARM: No formal guarantees
  bool was_full() noexcept
  {
    uint64_t prev_back = _back.load(std::memory_order_acquire); // Ensure prev_front is loaded AFTER this
    uint64_t prev_front = _front.load(std::memory_order_acquire);

    return prev_back >= prev_front + SIZE;
  }

  // May be useful for debug, but obviously not always accurate
  size_t was_size() noexcept
  {
    return std::clamp(static_cast<size_t>(_back.load(std::memory_order_relaxed) - _front.load(std::memory_order_relaxed)), static_cast<size_t>(0), SIZE);
  }

  /*
  @param element Pushes an element to queue (move only for non-trivially constructable types of size > 16 bytes). Busy waits if queue is full.
  */
  void push(std::conditional_t<copyable<T>, T, T&&> element) noexcept
  {
    do_push<false>(std::forward<T>(element));
  }

  /*
  @param element Tries to push an element to queue (move only for non-trivially constructable types of size > 16 bytes).
  @returns true if the queue was not full (and so the element was pushed), otherwise false.
  */
  bool try_push(std::conditional_t<copyable<T>, T, T&&> element) noexcept
  {
    return do_push<true>(std::forward<T>(element));
  }

  /*
  Pops an element from the queue. Busy waits if queue is empty.
  @returns the element.
  */
  T pop() noexcept
  {
    T value;
    do_pop<false>(value);
    return value;
  }

  /*
  @param result Tries pop an element from the queue into result.
  @returns true if the queue was not empty (and so the element was popped), otherwise false.
  */
  bool try_pop(T& result) noexcept
  {
    return do_pop<true>(result);
  };

private:
  /*
  Note on memory ordering:

  We need to ensure that the operations "set slot_state::READING -> read element -> set slot_state::EMPTY" and "set slot_state::WRITING -> write element -> set slot_state::FULL" happen in order.
  To do this, we use the standard acquire-release pattern.

  When reading/writing the front/back, we can use relaxed memory ordering as these are not used to synchronise any other reads/writes, they exist simply to guide the producers/consumers to the appropriate
  location in the array of states.
  */

  template <bool return_if_full>
  ORBIT_FORCE_INLINE bool do_push(std::conditional_t<copyable<T>, T, T&&> element) noexcept
  {
    while (true)
    {
      uint64_t prev_back = _back.load(std::memory_order_relaxed);

      uint64_t cycle = (prev_back >> POW_TWO) + 1;
      uint64_t expected_state = ((cycle - 1) << 2) | slot_state::EMPTY; // Expect empty slot from last cycle (most likely)
      uint64_t desired_state = (cycle << 2) | slot_state::WRITING;

      // Try to set to writing, ensuring the element write happens AFTER this if successful
      if ((!_states[(prev_back * STEP) & MODULO_MASK].compare_exchange_weak(expected_state, desired_state, std::memory_order_acquire, std::memory_order_relaxed)))
      {
        if constexpr (return_if_full || !MINIMISE_LATENCY)
        {
          if ((expected_state >> 2) == (cycle - 1)) [[unlikely]]
          {
            spin_pause<PAUSE_SHORT>(); // No need for a very long pause here even when optimising for throughput
            if constexpr (return_if_full)
            {
              return false; // Queue was full
            }
          }
          else
          {
            do_spin_pause();
          }
        }
        else
        {
          do_spin_pause();
        }

        if constexpr (NONBLOCKING)
        {
          if (expected_state == desired_state) // Check if another thread has already started writing here (only possible if multiple producers)
          {
            _back.compare_exchange_weak(prev_back, prev_back + 1, std::memory_order_relaxed, std::memory_order_relaxed); // Help move the back if no one else does
          }
        }
        continue;
      }

      if constexpr (NONBLOCKING)
      {
        uint64_t prev_back_copy = prev_back;
        _back.compare_exchange_weak(prev_back_copy, prev_back + 1, std::memory_order_relaxed, std::memory_order_relaxed); // Update back if nobody else did
      }
      else
      {
        _back.store(prev_back + 1, std::memory_order_relaxed); // Move back so other threads can proceed
      }

      // We have reserved the slot - no other thread can touch it now, so we can safely write before releasing it
      if constexpr (copyable<T>)
      {
        _elements[(prev_back * STEP) & MODULO_MASK] = element;
      }
      else
      {
        _elements[(prev_back * STEP) & MODULO_MASK] = std::move(element);
      }

      _states[(prev_back * STEP) & MODULO_MASK].store((cycle << 2) | slot_state::FULL, std::memory_order_release); // Ensure element is written before this store takes place

      return true;
    }
  }

  template <bool return_if_empty>
  ORBIT_FORCE_INLINE bool do_pop(T& result) noexcept
  {
    while (true)
    {
      uint64_t prev_front = _front.load(std::memory_order_relaxed);

      uint64_t cycle = (prev_front >> POW_TWO) + 1;
      uint64_t expected_state = (cycle << 2) | slot_state::FULL; // Expect full slot from this cycle (most likely)
      uint64_t desired_state = (cycle << 2) | slot_state::READING;

      // Try to set to reading, ensuring the element read happens AFTER this if successful
      if ((!_states[(prev_front * STEP) & MODULO_MASK].compare_exchange_weak(expected_state, desired_state, std::memory_order_acquire, std::memory_order_relaxed)))
      {
        if constexpr (return_if_empty || !MINIMISE_LATENCY)
        {
          if ((expected_state >> 2) == (cycle - 1)) [[unlikely]]
          {
            spin_pause<PAUSE_SHORT>(); // No need for a very long pause here even when optimising for throughput
            if constexpr (return_if_empty)
            {
              return false; // Queue was empty
            }
          }
          else
          {
            do_spin_pause();
          }
        }
        else
        {
          do_spin_pause();
        }

        if constexpr (NONBLOCKING)
        {
          if (expected_state == desired_state) // Check if another thread is reading here
          {
            _front.compare_exchange_weak(prev_front, prev_front + 1, std::memory_order_relaxed, std::memory_order_relaxed); // Help move the front if no one else does
          }
        }
        continue;
      }

      if constexpr (NONBLOCKING)
      {
        uint64_t prev_front_copy = prev_front;
        _front.compare_exchange_weak(prev_front_copy, prev_front + 1, std::memory_order_relaxed, std::memory_order_relaxed); // Update front if nobody else did
      }
      else
      {
        _front.store(prev_front + 1, std::memory_order_relaxed); // Move front so other threads can proceed
      }

      // We have reserved the slot - no other thread can touch it now, so we can safely read before releasing it
      if constexpr (copyable<T>)
      {
        result = _elements[(prev_front * STEP) & MODULO_MASK];
      }
      else
      {
        result = std::move(_elements[(prev_front * STEP) & MODULO_MASK]);
      }

      _states[(prev_front * STEP) & MODULO_MASK].store((cycle << 2) | slot_state::EMPTY, std::memory_order_release); // Ensure element is read before this store takes place
      return true;
    }
  };

  ORBIT_FORCE_INLINE void do_spin_pause() noexcept
  {
    if constexpr (MINIMISE_LATENCY)
    {
      spin_pause<PAUSE_SHORT>();
    }
    else
    {
      spin_pause<PAUSE_LONG>();
    }
  }

  struct slot_state
  {
    static constexpr uint64_t EMPTY = 0;
    static constexpr uint64_t FULL = 1;
    static constexpr uint64_t READING = 2;
    static constexpr uint64_t WRITING = 3;
  };

  alignas(CACHE_LINE_SIZE) std::array<T, SIZE> _elements;
  alignas(CACHE_LINE_SIZE) std::array<std::atomic<uint64_t>, SIZE> _states;

  alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> _front;
  alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> _back;

  static constexpr uint64_t POW_TWO = static_cast<uint64_t>(std::countr_zero(SIZE)); // SIZE = 2^POW_TWO

  static constexpr uint64_t MODULO_MASK = SIZE - 1; // Given N, N & MODULO_MASK is equivalent to (and faster than) N % SIZE

  /*
  If possible, we want concurrent readers/writers to be on separate cache lines to eliminate false sharing as much as possible. An odd step size guarantees full period, i.e. we still cycle through each element of _states, just not in order.
  A step of (CACHE_LINE_SIZE / 8) + 1 ensures that each subsequent access of the _states vector lies on a different cache line. This is backed up as a good choice by benchmarks.
  The exception to this is when we are optimising for throughput, since in this case we minimise contention by use of spin pause instead. Here, the better locality for the currently writing thread outweighs the added contention.
  */
  static constexpr uint64_t STEP = (MINIMISE_LATENCY) ? (CACHE_LINE_SIZE / 8) + 1 : 1;
};

} // namespace orbit
