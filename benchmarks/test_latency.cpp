#include <algorithm>
#include <atomic>
#include <format>
#include <fstream>
#include <future>
#include <print>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "circular_queue.h"
#include "timer.h"

namespace
{
template <auto Start, auto End, typename F>
constexpr void constexpr_for_val(F&& f)
{
  if constexpr (Start < End)
  {
    f(std::integral_constant<decltype(Start), Start>());
    constexpr_for_val<Start + 1, End>(f);
  }
}
} // namespace

struct example_struct
{
  int a = 100;
  int b = -100;
  int c = 1000;
  int d = -1000;
};

template <size_t QUEUE_SIZE = 128, size_t PS = 3, size_t PL = 40>
class latency_test
{
public:
  void run_benchmark(int32_t num_values)
  {
    timer tm;
    tm.start();

    // Start threads
    std::thread t1(&latency_test::bounce_thread1, this, num_values);
    std::thread t2(&latency_test::bounce_thread2, this, num_values);

    // std::thread t3(&latency_test::bounce_thread1, this, num_values);
    // std::thread t4(&latency_test::bounce_thread2, this, num_values);

    // Wait for threads to finish
    t1.join();
    t2.join();

    // t3.join();
    // t4.join();

    // Get and store all results
    tm.stop();
    std::ofstream file("benchmarks/data/pause_lengths/latency_data.csv", std::ios::app);
    std::println(file, "{}, nocas{}-{}, {}, {}, {}", QUEUE_SIZE, PS, PL, "int32_t", num_values, tm.get_ms());
    file.close();
  };

private:
  // Each loop in these threads, the element has to be both pushed and popped from both queues
  void bounce_thread1(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _queue1.push(value);
      _queue2.pop();
      // while (!_queue2.try_pop(value))
      //   ;
    }
  }

  void bounce_thread2(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _queue2.push(value);
      _queue1.pop();
      // while (!_queue1.try_pop(value))
      //   ;
    }
  }

private:
  lockfree::circular_queue<int32_t, QUEUE_SIZE, true, false, PS, PL> _queue1;
  lockfree::circular_queue<int32_t, QUEUE_SIZE, true, false, PS, PL> _queue2;
};

void compare_pause_options()
{
  const int32_t num_values = 1000000;
  const size_t num_test_runs = 50;

  constexpr std::array<size_t, 7> short_pause_options = {0, 1, 2, 3, 4, 5, 6};
  constexpr std::array<size_t, 1> long_pause_options = {40};

  for (size_t test_count = 0; test_count < num_test_runs; ++test_count)
  {
    constexpr_for_val<0, short_pause_options.size()>([&](auto i) {
      constexpr_for_val<0, long_pause_options.size()>([&](auto j) {
        latency_test<128, short_pause_options[i], long_pause_options[j]> t;
        t.run_benchmark(num_values);
      });
    });

    std::println("Finished test run {}/{} with {} values.", test_count + 1, num_test_runs, num_values);
  }
}

/*
void compare_step_options()
{
  const int32_t num_values = 1000000;
  const size_t num_test_runs = 500;

  constexpr std::array<size_t, 64> step_options = {127, 125, 123, 121, 119, 117, 115, 113, 111, 109, 107, 105, 103, 101, 99, 97, 95, 93, 91, 89, 87, 85, 83, 81, 79, 77, 75, 73, 71, 69, 67, 65,
                                                   63,  61,  59,  57,  55,  53,  51,  49,  47,  45,  43,  41,  39,  37,  35, 33, 31, 29, 27, 25, 23, 21, 19, 17, 15, 13, 11, 9,  7,  5,  3,  1};

  for (size_t test_count = 0; test_count < num_test_runs; ++test_count)
  {
    constexpr_for_val<0, step_options.size()>([&](auto i) {
      latency_test<128, 3, 6, 40, step_options[i]> t;
      t.run_benchmark(num_values);
    });

    std::println("Finished test run {}/{} with {} values.", test_count + 1, num_test_runs, num_values);
  }
}
*/

int main()
{
  compare_pause_options();

  return 0;
}