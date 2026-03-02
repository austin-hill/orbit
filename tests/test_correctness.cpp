#include <algorithm>
#include <atomic>
#include <format>
#include <future>
#include <print>
#include <ranges>
#include <set>
#include <thread>
#include <vector>

#include "circular_queue.h"
#include "timer.h"

class test
{
public:
  bool producer_consumer(int num_producers, int num_consumers, int num_values)
  {
    int num_values_per_producer = num_values / num_producers;

    std::set<int> results;

    std::vector<std::thread> producers;
    std::vector<std::future<std::set<int>>> consumers;

    // Start all threads
    for (int consumer = 0; consumer < num_consumers; ++consumer)
    {
      consumers.emplace_back(std::async(std::launch::async, &test::pop_thread, this));
    }

    for (int producer = 0; producer < num_producers; ++producer)
    {
      producers.emplace_back(&test::push_thread, this, producer * num_values_per_producer, num_values_per_producer);
    }

    // Wait for producers to finish
    for (auto& producer : producers)
    {
      producer.join();
    }

    _cancel_threads.store(1, std::memory_order_relaxed);

    // Get and store all results
    for (auto& consumer : consumers)
    {
      std::set<int> consumer_results = consumer.get();
      std::merge(results.begin(), results.end(), consumer_results.begin(), consumer_results.end(), std::inserter(results, results.end()));
    }

    if (results.size() != num_values_per_producer * num_producers)
    {
      throw std::runtime_error(std::format("Failed test: expected {} values, received {}", num_values_per_producer * num_producers, results.size()));
    }

    for (const auto& [correct_value, actual_value] : std::views::zip(std::views::iota(0), results))
    {
      if (correct_value != actual_value)
      {
        std::println("Fail: expected {}, received {}", correct_value, actual_value);
        throw std::runtime_error("Failed tests");
      }
    }

    return true; // Have got all ints in range as expected
  }

private:
  void push_thread(int start_index, int num_values)
  {
    for (int value = start_index; value < start_index + num_values; ++value)
    {
      _queue.push(value);
    }
  }

  std::set<int> pop_thread()
  {
    std::vector<int> results;
    int i;
    while (true)
    {
      if (_queue.try_pop(i))
      {
        results.push_back(i);
        continue;
      }

      if (_cancel_threads.load() && _queue.was_empty())
      {
        return std::set<int>(results.begin(), results.end());
      }
    }
  }

  lockfree::circular_queue<int, 128, true, false> _queue;

  std::atomic<bool> _cancel_threads = false;
};

int main()
{

  const std::vector<size_t> num_options = {1, 2, 4, 6};
  const int num_values = 1000000;
  const int num_test_runs = 50;

  for (size_t test_count = 0; test_count < num_test_runs; ++test_count)
  {
    for (size_t num_producers : num_options)
    {
      for (size_t num_consumers : num_options)
      {
        test t;
        t.producer_consumer(num_producers, num_consumers, num_values);
      }
    }
    std::println("Passed test run {}/{} with {} values.", test_count + 1, num_test_runs, num_values);
  }

  std::println("Passed all tests!");
  return 0;
}
