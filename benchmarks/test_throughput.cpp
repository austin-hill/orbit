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

struct example_struct
{
  int v1 = 1;
  int v2 = 100;
  int v3 = -10000;
  int v4 = 1000000;
  // std::string s1 = "12345";
  // std::string s2 = "6789";
  // std::vector<int> v = {1, 2, 3};
  // std::array<int64_t, 10> a = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
};

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

template <size_t PS = 3, size_t PL = 40>
class test
{
public:
  void speed_test(int32_t num_producers, int32_t num_consumers, int32_t num_values)
  {
    int32_t num_values_per_producer = num_values / num_producers;

    std::vector<std::thread> producers;
    std::vector<std::future<int32_t>> consumers;

    timer tm;
    tm.start();

    // Start all threads
    for (int32_t consumer = 0; consumer < num_consumers; ++consumer)
    {
      consumers.emplace_back(std::async(std::launch::async, &test::pop_thread, this));
    }

    for (int32_t producer = 0; producer < num_producers; ++producer)
    {
      producers.emplace_back(&test::push_thread, this, producer * num_values_per_producer, num_values_per_producer);
    }

    // Wait for producers to finish
    for (auto& producer : producers)
    {
      producer.join();
    }

    _cancel_threads.store(true, std::memory_order_relaxed);

    // Get and store all results
    int32_t total_count = 0;
    for (auto& consumer : consumers)
    {
      total_count += consumer.get();
    }

    tm.stop();
    std::ofstream file("benchmarks/data/pause_lengths/throughput_data.csv", std::ios::app);
    std::println(file, "{}, {}-{}, {}, {}, {}, {}, {}", QUEUE_SIZE, PS, PL, "int32_t", num_producers, num_consumers, num_values, tm.get_ms());
    file.close();

    if (total_count != num_values_per_producer * num_producers)
    {
      throw std::runtime_error(std::format("Failed test: expected {} values, received {}", num_values_per_producer * num_producers, total_count));
    }
  };

private:
  void push_thread(int32_t start_index, int32_t num_values)
  {
    for (int32_t value = start_index + 1; value < start_index + num_values + 1; ++value)
    {
      _queue.push(value);
    }
  }

  int32_t pop_thread()
  {
    int32_t value;
    int32_t count = 0;
    while (true)
    {
      if (_queue.try_pop(value))
      {
        ++count;
        continue;
      }

      if (_cancel_threads.load() && _queue.was_empty())
      {
        return count;
      }
    }
  }

private:
  static constexpr size_t QUEUE_SIZE = 128;
  lockfree::circular_queue<int32_t, QUEUE_SIZE, false, false, PS, PL> _queue;

  std::atomic<bool> _cancel_threads = false;
};

void compare_pause_options()
{
  constexpr std::array<size_t, 4> num_options = {1, 2, 4, 6};
  const int32_t num_values = 1000000;
  const size_t num_test_runs = 10;

  constexpr std::array<size_t, 3> short_pause_options = {2, 5, 20};
  constexpr std::array<size_t, 5> long_pause_options = {20, 40, 60, 80, 100};

  for (size_t test_count = 0; test_count < num_test_runs; ++test_count)
  {
    for (auto num_producers : num_options)
    {
      for (auto num_consumers : num_options)
      {
        constexpr_for_val<0, short_pause_options.size()>([&](auto i) {
          constexpr_for_val<0, long_pause_options.size()>([&](auto j) {
            test<short_pause_options[i], long_pause_options[j]> t;
            t.speed_test(num_producers, num_consumers, num_values);
          });
        });
      }
    }
    std::println("Finished test run {}/{} with {} values.", test_count + 1, num_test_runs, num_values);
  }
}

/*
void compare_step_options()
{
  constexpr std::array<size_t, 3> num_options = {1, 2, 4};
  const int32_t num_values = 1000000;
  const size_t num_test_runs = 10;

  constexpr std::array<size_t, 64> step_options = {127, 125, 123, 121, 119, 117, 115, 113, 111, 109, 107, 105, 103, 101, 99, 97, 95, 93, 91, 89, 87, 85, 83, 81, 79, 77, 75, 73, 71, 69, 67, 65,
                                                   63,  61,  59,  57,  55,  53,  51,  49,  47,  45,  43,  41,  39,  37,  35, 33, 31, 29, 27, 25, 23, 21, 19, 17, 15, 13, 11, 9,  7,  5,  3,  1};

  for (size_t test_count = 0; test_count < num_test_runs; ++test_count)
  {
    constexpr_for_val<0, num_options.size()>([&](auto num_producers_idx) {
      constexpr_for_val<0, num_options.size()>([&](auto num_consumers_idx) {
        constexpr_for_val<0, step_options.size()>([&](auto i) {
          test<3, 6, 40, step_options[i]> t;
          t.speed_test(num_options[num_producers_idx], num_options[num_consumers_idx], num_values);
        });
      });
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
