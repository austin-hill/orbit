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

#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/spsc_queue.hpp>

#include <atomic_queue/include/atomic_queue/atomic_queue.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <xenium/michael_scott_queue.hpp>
#include <xenium/nikolaev_bounded_queue.hpp>
#include <xenium/ramalhete_queue.hpp>
#include <xenium/reclamation/generic_epoch_based.hpp>
#include <xenium/vyukov_bounded_queue.hpp>
#pragma GCC diagnostic pop

#include <concurrentqueue.h>
#include <readerwritercircularbuffer.h>
#include <readerwriterqueue.h>

#include "circular_queue.h"
#include "timer.h"

template <size_t QUEUE_SIZE = 2048, size_t PS = 3, size_t PL = 40>
class test
{
public:
  test() : _bdd_spsc_moodycamel_queue(QUEUE_SIZE)
  {
  }

  void speed_test(int32_t num_producers, int32_t num_consumers, int32_t num_values)
  {
    do_speed_test("lat_circular_queue", _lat_queue, &test::circular_queue_push_thread, &test::circular_queue_pop_thread, num_producers, num_consumers, num_values);
    do_speed_test("lat_blocking_circular_queue", _lat_blocking_queue, &test::circular_queue_push_thread, &test::circular_queue_pop_thread, num_producers, num_consumers, num_values);
    do_speed_test("tp_circular_queue", _tp_queue, &test::circular_queue_push_thread, &test::circular_queue_pop_thread, num_producers, num_consumers, num_values);
    do_speed_test("tp_blocking_circular_queue", _tp_blocking_queue, &test::circular_queue_push_thread, &test::circular_queue_pop_thread, num_producers, num_consumers, num_values);

    do_speed_test("atomic_queue", _atomic_queue, &test::atomic_queue_push_thread, &test::atomic_queue_pop_thread, num_producers, num_consumers, num_values);
    do_speed_test("atomic_queue2", _atomic_queue2, &test::atomic_queue_push_thread, &test::atomic_queue_pop_thread, num_producers, num_consumers, num_values);
    if (num_producers == 1 && num_consumers == 1)
    {
      do_speed_test("spsc_atomic_queue", _spsc_atomic_queue, &test::atomic_queue_push_thread, &test::atomic_queue_pop_thread, num_producers, num_consumers, num_values);
      do_speed_test("spsc_atomic_queue2", _spsc_atomic_queue2, &test::atomic_queue_push_thread, &test::atomic_queue_pop_thread, num_producers, num_consumers, num_values);
    }

    do_speed_test("boost_queue", _boost_queue, &test::boost_queue_push_thread, &test::boost_queue_pop_thread, num_producers, num_consumers, num_values);
    if (num_producers == 1 && num_consumers == 1)
    {
      do_speed_test("spsc_boost_queue", _spsc_boost_queue, &test::boost_queue_push_thread, &test::boost_queue_pop_thread, num_producers, num_consumers, num_values);
    }

    do_speed_test("xenium_ramalhete_queue", _ramalhete_queue, &test::ramalhete_queue_push_thread, &test::ramalhete_queue_pop_thread, num_producers, num_consumers, num_values);

    do_speed_test("moodycamel_concurrentqueue", _moodycamel_queue, &test::moodycamel_push_thread, &test::moodycamel_pop_thread, num_producers, num_consumers, num_values);
    if (num_producers == 1 && num_consumers == 1)
    {
      do_speed_test("moodycamel_readerwriterqueue", _spsc_moodycamel_queue, &test::moodycamel_push_thread, &test::moodycamel_pop_thread, num_producers, num_consumers, num_values);
    }
  };

private:
  template <typename T>
  void do_speed_test(const std::string& queue_name, T& q, void (test::*prod_func)(T&, int32_t, int32_t), int32_t (test::*cons_func)(T&), int32_t num_producers, int32_t num_consumers, int32_t num_values)
  {
    _cancel_threads.store(false);
    int32_t num_values_per_producer = num_values / num_producers;

    std::vector<std::thread> producers;
    std::vector<std::future<int32_t>> consumers;

    timer tm;
    tm.start();

    for (int32_t consumer = 0; consumer < num_consumers; ++consumer)
    {
      consumers.emplace_back(std::async(std::launch::async, [this, cons_func, &q]() { return (this->*cons_func)(q); }));
    }

    for (int32_t producer = 0; producer < num_producers; ++producer)
    {
      int32_t start = producer * num_values_per_producer;
      producers.emplace_back([this, prod_func, &q, start, num_values_per_producer]() { (this->*prod_func)(q, start, num_values_per_producer); });
    }

    // Wait for producers to finish
    for (auto& producer : producers)
    {
      producer.join();
    }

    _cancel_threads.store(true);

    // Get and store all results
    int32_t total_count = 0;
    for (auto& consumer : consumers)
    {
      total_count += consumer.get();
    }

    tm.stop();
    std::ofstream file("benchmarks/data/comparison/throughput_data.csv", std::ios::app);
    std::println(file, "{}, {}, {}, {}, {}, {}, {}", queue_name, QUEUE_SIZE, "int32_t", num_producers, num_consumers, num_values, tm.get_ms());
    file.close();

    if (total_count != num_values_per_producer * num_producers)
    {
      throw std::runtime_error(std::format("Failed test: expected {} values, received {}", num_values_per_producer * num_producers, total_count));
    }
  };

private:
  template <typename T>
  void circular_queue_push_thread(T& q, int32_t start_index, int32_t num_values)
  {
    for (int32_t value = start_index + 1; value < start_index + num_values + 1; ++value)
    {
      q.push(value);
    }
  }

  template <typename T>
  int32_t circular_queue_pop_thread(T& q)
  {
    int32_t value;
    int32_t count = 0;
    while (true)
    {
      if (q.try_pop(value))
      {
        ++count;
        continue;
      }

      if (_cancel_threads.load() && q.was_empty())
      {
        return count;
      }
    }
  }

  template <typename T>
  void atomic_queue_push_thread(T& q, int32_t start_index, int32_t num_values)
  {
    for (int32_t value = start_index + 1; value < start_index + num_values + 1; ++value)
    {
      q.push(value);
    }
  }

  template <typename T>
  int32_t atomic_queue_pop_thread(T& q)
  {
    int32_t value;
    int32_t count = 0;
    while (true)
    {
      if (q.try_pop(value))
      {
        ++count;
        continue;
      }
      lockfree::spin_pause<1>();

      if (_cancel_threads.load() && q.was_empty())
      {
        return count;
      }
    }
  }

  template <typename T>
  void ramalhete_queue_push_thread(T& q, int32_t start_index, int32_t num_values)
  {
    for (int32_t value = start_index + 1; value < start_index + num_values + 1; ++value)
    {
      q.push(value);
    }
  }

  template <typename T>
  int32_t ramalhete_queue_pop_thread(T& q)
  {
    int32_t value;
    int32_t count = 0;
    while (true)
    {
      if (q.try_pop(value))
      {
        ++count;
        continue;
      }
      lockfree::spin_pause<1>();

      if (_cancel_threads.load())
      {
        bool finished = true;

        for (size_t i = 0; i < 10; ++i)
        {
          if (q.try_pop(value))
          {
            ++count;
            finished = false;
            break;
          }
        }

        if (finished)
        {
          return count;
        }
      }
    }
  }

  template <typename T>
  void moodycamel_push_thread(T& q, int32_t start_index, int32_t num_values)
  {
    for (int32_t value = start_index + 1; value < start_index + num_values + 1; ++value)
    {
      q.enqueue(value);
    }
  }

  template <typename T>
  int32_t moodycamel_pop_thread(T& q)
  {
    int32_t value;
    int32_t count = 0;
    while (true)
    {
      if (q.try_dequeue(value))
      {
        ++count;
        continue;
      }
      lockfree::spin_pause<1>();

      if (_cancel_threads.load() && q.size_approx() == 0)
      {
        return count;
      }
    }
  }

  template <typename T>
  void boost_queue_push_thread(T& q, int32_t start_index, int32_t num_values)
  {
    for (int32_t value = start_index + 1; value < start_index + num_values + 1; ++value)
    {
      while (!q.push(value))
      {
        lockfree::spin_pause<1>();
      }
    }
  }

  template <typename T>
  int32_t boost_queue_pop_thread(T& q)
  {
    int32_t value;
    int32_t count = 0;
    while (true)
    {
      if (q.pop(value))
      {
        ++count;
        continue;
      }
      lockfree::spin_pause<1>();

      if (_cancel_threads.load() && q.empty())
      {
        return count;
      }
    }
  }

private:
  lockfree::circular_queue<int32_t, QUEUE_SIZE, true, false, PS, PL> _lat_blocking_queue;
  lockfree::circular_queue<int32_t, QUEUE_SIZE, true, true, 3, PL> _lat_queue;
  lockfree::circular_queue<int32_t, QUEUE_SIZE, false, false, PS, PL> _tp_blocking_queue;
  lockfree::circular_queue<int32_t, QUEUE_SIZE, false, false, 3, PL> _tp_queue;

  atomic_queue::AtomicQueue<int32_t, QUEUE_SIZE, 0, true, true, false, false> _atomic_queue;
  atomic_queue::AtomicQueue2<int32_t, QUEUE_SIZE, true, true, false, false> _atomic_queue2;
  atomic_queue::AtomicQueue<int32_t, QUEUE_SIZE, 0, true, true, false, true> _spsc_atomic_queue;
  atomic_queue::AtomicQueue2<int32_t, QUEUE_SIZE, true, true, false, true> _spsc_atomic_queue2;

  xenium::ramalhete_queue<int32_t, xenium::policy::reclaimer<xenium::reclamation::generic_epoch_based<>>, xenium::policy::entries_per_node<QUEUE_SIZE>> _ramalhete_queue;

  moodycamel::ConcurrentQueue<int32_t> _moodycamel_queue;
  moodycamel::ReaderWriterQueue<int32_t> _spsc_moodycamel_queue;
  moodycamel::BlockingReaderWriterCircularBuffer<int32_t> _bdd_spsc_moodycamel_queue;

  boost::lockfree::queue<int32_t, boost::lockfree::capacity<QUEUE_SIZE>> _boost_queue;
  boost::lockfree::spsc_queue<int32_t, boost::lockfree::capacity<QUEUE_SIZE>> _spsc_boost_queue;

  std::atomic<bool> _cancel_threads = false;
};

int main()
{
  constexpr std::array<size_t, 4> num_options = {1, 2, 4, 6};
  const int32_t num_values = 1000000;
  const size_t num_test_runs = 10;

  for (size_t test_count = 0; test_count < num_test_runs; ++test_count)
  {
    for (auto num_producers : num_options)
    {
      for (auto num_consumers : num_options)
      {
        test t;
        t.speed_test(num_producers, num_consumers, num_values);
      }
    }

    std::println("Finished test run {}/{} with {} values.", test_count + 1, num_test_runs, num_values);
  }

  return 0;
}
