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

template <size_t QUEUE_SIZE = 128, size_t PS = 2, size_t PL = 40>
class latency_test
{
public:
  latency_test() : _bdd_spsc_moodycamel_queue1(QUEUE_SIZE), _bdd_spsc_moodycamel_queue2(QUEUE_SIZE)
  {
  }

  void run_benchmark(int32_t num_values)
  {
    do_run_benchmark("lat_blocking_circular_queue", &latency_test::lat_bounce_thread1, &latency_test::lat_bounce_thread2, num_values);
    do_run_benchmark("lat_blocking_try_circular_queue", &latency_test::lat_try_bounce_thread1, &latency_test::lat_try_bounce_thread2, num_values);
    do_run_benchmark("lat_circular_queue", &latency_test::lat_nb_bounce_thread1, &latency_test::lat_nb_bounce_thread2, num_values);
    do_run_benchmark("tp_blocking_circular_queue", &latency_test::tp_bounce_thread1, &latency_test::tp_bounce_thread2, num_values);
    do_run_benchmark("tp_circular_queue", &latency_test::tp_nb_bounce_thread1, &latency_test::tp_nb_bounce_thread2, num_values);

    do_run_benchmark("atomic_queue", &latency_test::atomic_queue_bounce_thread1, &latency_test::atomic_queue_bounce_thread2, num_values);
    do_run_benchmark("atomic_queue2", &latency_test::atomic_queue2_bounce_thread1, &latency_test::atomic_queue2_bounce_thread2, num_values);
    do_run_benchmark("try_atomic_queue2", &latency_test::atomic_queue2_bounce_thread1, &latency_test::atomic_queue2_bounce_thread2, num_values);
    do_run_benchmark("spsc_atomic_queue", &latency_test::spsc_atomic_queue_bounce_thread1, &latency_test::spsc_atomic_queue_bounce_thread2, num_values);
    do_run_benchmark("spsc_atomic_queue2", &latency_test::spsc_atomic_queue2_bounce_thread1, &latency_test::spsc_atomic_queue2_bounce_thread2, num_values);

    do_run_benchmark("boost_queue", &latency_test::boost_queue_bounce_thread1, &latency_test::boost_queue_bounce_thread2, num_values);
    do_run_benchmark("spsc_boost_queue", &latency_test::spsc_boost_queue_bounce_thread1, &latency_test::spsc_boost_queue_bounce_thread2, num_values);

    do_run_benchmark("xenium_ramalhete_queue", &latency_test::ramalhete_bounce_thread1, &latency_test::ramalhete_bounce_thread2, num_values);

    do_run_benchmark("moodycamel_concurrentqueue", &latency_test::moodycamel_bounce_thread1, &latency_test::moodycamel_bounce_thread2, num_values);
    do_run_benchmark("spsc_moodycamel_readerwriterqueue", &latency_test::spsc_moodycamel_bounce_thread1, &latency_test::spsc_moodycamel_bounce_thread2, num_values);
    do_run_benchmark("spsc_moodycamel_readerwritercircularbuffer", &latency_test::bdd_spsc_moodycamel_bounce_thread1, &latency_test::bdd_spsc_moodycamel_bounce_thread2, num_values);
  };

private:
  template <typename T>
  void do_run_benchmark(const std::string& queue_name, T func1, T func2, int32_t num_values)
  {
    timer tm;
    tm.start();

    // Start threads
    std::thread t1(func1, this, num_values);
    std::thread t2(func2, this, num_values);

    // std::thread t3(func1, this, num_values);
    // std::thread t4(func2, this, num_values);

    // Wait for threads to finish
    t1.join();
    t2.join();

    // t3.join();
    // t4.join();

    // Get and store all results
    tm.stop();
    std::ofstream file("benchmarks/data/comparison/latency_data.csv", std::ios::app);
    std::println(file, "{}, {}, {}, {}, {}", queue_name, QUEUE_SIZE, "int32_t", num_values, tm.get_us());
    file.close();
  }

  /*
  Each loop in these threads, the element has to be both pushed and popped from both queues.

  There is duplication here, but this is to keep it simple for ease of changing when trying out different things during benchmarks.
  */
  void lat_bounce_thread1(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _lat_queue1.push(value);
      _lat_queue2.pop();
    }
  }

  void lat_bounce_thread2(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _lat_queue2.push(value);
      _lat_queue1.pop();
    }
  }

  void lat_try_bounce_thread1(int32_t num_bounces)
  {
    int32_t val;
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      while (!_lat_queue1.try_push(value))
        ;
      while (!_lat_queue2.try_pop(val))
        ;
    }
  }

  void lat_try_bounce_thread2(int32_t num_bounces)
  {
    int32_t val;
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      while (!_lat_queue2.try_push(value))
        ;
      while (!_lat_queue1.try_pop(val))
        ;
    }
  }

  void lat_nb_bounce_thread1(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _lat_nb_queue1.push(value);
      _lat_nb_queue2.pop();
    }
  }

  void lat_nb_bounce_thread2(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _lat_nb_queue2.push(value);
      _lat_nb_queue1.pop();
    }
  }

  void tp_bounce_thread1(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _tp_queue1.push(value);
      _tp_queue2.pop();
    }
  }

  void tp_bounce_thread2(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _tp_queue2.push(value);
      _tp_queue1.pop();
    }
  }

  void tp_nb_bounce_thread1(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _tp_nb_queue1.push(value);
      _tp_nb_queue2.pop();
    }
  }

  void tp_nb_bounce_thread2(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _tp_nb_queue2.push(value);
      _tp_nb_queue1.pop();
    }
  }

  void atomic_queue_bounce_thread1(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _atomic_queue1.push(value);
      _atomic_queue2.pop();
    }
  }

  void atomic_queue_bounce_thread2(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _atomic_queue2.push(value);
      _atomic_queue1.pop();
    }
  }

  void atomic_queue2_bounce_thread1(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _atomic_queue21.push(value);
      _atomic_queue22.pop();
    }
  }

  void atomic_queue2_bounce_thread2(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _atomic_queue22.push(value);
      _atomic_queue21.pop();
    }
  }

  void try_atomic_queue2_bounce_thread1(int32_t num_bounces)
  {
    int32_t val;
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      while (!_atomic_queue21.try_push(value))
      {
        lockfree::spin_pause<1>();
      }
      while (!_atomic_queue22.try_pop(val))
      {
        lockfree::spin_pause<1>();
      }
    }
  }

  void try_atomic_queue2_bounce_thread2(int32_t num_bounces)
  {
    int32_t val;
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      while (!_atomic_queue22.try_push(value))
      {
        lockfree::spin_pause<1>();
      }
      while (!_atomic_queue21.try_pop(val))
      {
        lockfree::spin_pause<1>();
      }
    }
  }

  void spsc_atomic_queue_bounce_thread1(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _spsc_atomic_queue1.push(value);
      _spsc_atomic_queue2.pop();
    }
  }

  void spsc_atomic_queue_bounce_thread2(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _spsc_atomic_queue2.push(value);
      _spsc_atomic_queue1.pop();
    }
  }

  void spsc_atomic_queue2_bounce_thread1(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _spsc_atomic_queue21.push(value);
      _spsc_atomic_queue22.pop();
    }
  }

  void spsc_atomic_queue2_bounce_thread2(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _spsc_atomic_queue22.push(value);
      _spsc_atomic_queue21.pop();
    }
  }

  void boost_queue_bounce_thread1(int32_t num_bounces)
  {
    int32_t val;
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      while (!_boost_queue1.push(value))
      {
        lockfree::spin_pause<1>();
      }
      while (!_boost_queue2.pop(val))
      {
        lockfree::spin_pause<1>();
      }
    }
  }

  void boost_queue_bounce_thread2(int32_t num_bounces)
  {
    int32_t val;
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      while (!_boost_queue2.push(value))
      {
        lockfree::spin_pause<1>();
      }
      while (!_boost_queue1.pop(val))
      {
        lockfree::spin_pause<1>();
      }
    }
  }

  void spsc_boost_queue_bounce_thread1(int32_t num_bounces)
  {
    int32_t val;
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      while (!_spsc_boost_queue1.push(value))
      {
        lockfree::spin_pause<1>();
      }
      while (!_spsc_boost_queue2.pop(val))
      {
        lockfree::spin_pause<1>();
      }
    }
  }

  void spsc_boost_queue_bounce_thread2(int32_t num_bounces)
  {
    int32_t val;
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      while (!_spsc_boost_queue2.push(value))
      {
        lockfree::spin_pause<1>();
      }
      while (!_spsc_boost_queue1.pop(val))
      {
        lockfree::spin_pause<1>();
      }
    }
  }

  void ramalhete_bounce_thread1(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _ramalhete_queue1.push(value);
      while (!_ramalhete_queue2.pop())
      {
        lockfree::spin_pause<1>();
      }
    }
  }

  void ramalhete_bounce_thread2(int32_t num_bounces)
  {
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _ramalhete_queue2.push(value);
      while (!_ramalhete_queue1.pop())
      {
        lockfree::spin_pause<1>();
      }
    }
  }

  void moodycamel_bounce_thread1(int32_t num_bounces)
  {
    int32_t val;
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _moodycamel_queue1.enqueue(value);
      while (!_moodycamel_queue2.try_dequeue(val))
      {
        lockfree::spin_pause<1>();
      }
    }
  }

  void moodycamel_bounce_thread2(int32_t num_bounces)
  {
    int32_t val;
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _moodycamel_queue2.enqueue(value);
      while (!_moodycamel_queue1.try_dequeue(val))
      {
        lockfree::spin_pause<1>();
      }
    }
  }

  void spsc_moodycamel_bounce_thread1(int32_t num_bounces)
  {
    int32_t val;
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _spsc_moodycamel_queue1.enqueue(value);
      while (!_spsc_moodycamel_queue2.try_dequeue(val))
      {
        lockfree::spin_pause<1>();
      }
    }
  }

  void spsc_moodycamel_bounce_thread2(int32_t num_bounces)
  {
    int32_t val;
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      _spsc_moodycamel_queue2.enqueue(value);
      while (!_spsc_moodycamel_queue1.try_dequeue(val))
      {
        lockfree::spin_pause<1>();
      }
    }
  }

  void bdd_spsc_moodycamel_bounce_thread1(int32_t num_bounces)
  {
    int32_t val;
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      while (!_bdd_spsc_moodycamel_queue1.try_enqueue(value))
      {
        lockfree::spin_pause<1>();
      }
      while (!_bdd_spsc_moodycamel_queue2.try_dequeue(val))
      {
        lockfree::spin_pause<1>();
      }
    }
  }

  void bdd_spsc_moodycamel_bounce_thread2(int32_t num_bounces)
  {
    int32_t val;
    for (int32_t value = 1; value < num_bounces + 1; ++value)
    {
      while (!_bdd_spsc_moodycamel_queue2.try_enqueue(value))
      {
        lockfree::spin_pause<1>();
      }
      while (!_bdd_spsc_moodycamel_queue1.try_dequeue(val))
      {
        lockfree::spin_pause<1>();
      }
    }
  }

private:
  lockfree::circular_queue<int32_t, QUEUE_SIZE, true, false, PS, PL> _lat_queue1;
  lockfree::circular_queue<int32_t, QUEUE_SIZE, true, false, PS, PL> _lat_queue2;

  lockfree::circular_queue<int32_t, QUEUE_SIZE, true, true, 3, PL> _lat_nb_queue1;
  lockfree::circular_queue<int32_t, QUEUE_SIZE, true, true, 3, PL> _lat_nb_queue2;

  lockfree::circular_queue<int32_t, QUEUE_SIZE, false, false, PS, PL> _tp_queue1;
  lockfree::circular_queue<int32_t, QUEUE_SIZE, false, false, PS, PL> _tp_queue2;

  lockfree::circular_queue<int32_t, QUEUE_SIZE, false, true, 3, PL> _tp_nb_queue1;
  lockfree::circular_queue<int32_t, QUEUE_SIZE, false, true, 3, PL> _tp_nb_queue2;

  atomic_queue::AtomicQueue<int32_t, QUEUE_SIZE, 0, true, true, false, false> _atomic_queue1;
  atomic_queue::AtomicQueue<int32_t, QUEUE_SIZE, 0, true, true, false, false> _atomic_queue2;

  atomic_queue::AtomicQueue2<int32_t, QUEUE_SIZE, true, true, false, false> _atomic_queue21;
  atomic_queue::AtomicQueue2<int32_t, QUEUE_SIZE, true, true, false, false> _atomic_queue22;

  atomic_queue::AtomicQueue<int32_t, QUEUE_SIZE, 0, true, true, false, true> _spsc_atomic_queue1;
  atomic_queue::AtomicQueue<int32_t, QUEUE_SIZE, 0, true, true, false, true> _spsc_atomic_queue2;

  atomic_queue::AtomicQueue2<int32_t, QUEUE_SIZE, true, true, false, true> _spsc_atomic_queue21;
  atomic_queue::AtomicQueue2<int32_t, QUEUE_SIZE, true, true, false, true> _spsc_atomic_queue22;

  xenium::ramalhete_queue<int32_t, xenium::policy::reclaimer<xenium::reclamation::generic_epoch_based<>>, xenium::policy::entries_per_node<QUEUE_SIZE>> _ramalhete_queue1;
  xenium::ramalhete_queue<int32_t, xenium::policy::reclaimer<xenium::reclamation::generic_epoch_based<>>, xenium::policy::entries_per_node<QUEUE_SIZE>> _ramalhete_queue2;

  moodycamel::ConcurrentQueue<int32_t> _moodycamel_queue1;
  moodycamel::ConcurrentQueue<int32_t> _moodycamel_queue2;

  moodycamel::ReaderWriterQueue<int32_t> _spsc_moodycamel_queue1;
  moodycamel::ReaderWriterQueue<int32_t> _spsc_moodycamel_queue2;

  moodycamel::BlockingReaderWriterCircularBuffer<int32_t> _bdd_spsc_moodycamel_queue1;
  moodycamel::BlockingReaderWriterCircularBuffer<int32_t> _bdd_spsc_moodycamel_queue2;

  boost::lockfree::queue<int32_t, boost::lockfree::capacity<QUEUE_SIZE>> _boost_queue1;
  boost::lockfree::queue<int32_t, boost::lockfree::capacity<QUEUE_SIZE>> _boost_queue2;

  boost::lockfree::spsc_queue<int32_t, boost::lockfree::capacity<QUEUE_SIZE>> _spsc_boost_queue1;
  boost::lockfree::spsc_queue<int32_t, boost::lockfree::capacity<QUEUE_SIZE>> _spsc_boost_queue2;
};

int main()
{
  const int32_t num_values = 100000;
  const size_t num_test_runs = 50;

  for (size_t test_count = 0; test_count < num_test_runs; ++test_count)
  {
    latency_test t;
    t.run_benchmark(num_values);
    std::println("Finished test run {}/{} with {} values.", test_count + 1, num_test_runs, num_values);
  }

  return 0;
}
