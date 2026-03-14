# Circular Queue

This is a high performance lock-free multi-producer multi-consumer bounded queue. It is implemented as a single header only library in C++20 (although the benchmarks use C++23 for convenience).

## Who should use this

There are a number of existing lock-free queue implementations in C++ at this point. Why should you use this one?

- It is [faster](#benchmarks) than other implementations in most cases
- There are no arbitrary restrictions on element type: The only limitations in place are by choice, to enforce effective usage.
    - Elements must be default constructible. They are stored contiguously in a `std::array`. If your type doesn't have a default constructor, you should probably be wrapping it in a unique pointer anyway.
    - For non-trivially copyable types of size greater than 16 bytes, all operations are move-only. Again, if your type doesn't work with this, you should be wrapping it in a unique pointer.
- Offers a simple and versatile interface
    - Offers both blocking (`push`/`pop`) and non-blocking (`try_push`/`try_pop`) operations - with _no meaningful performance difference_. Use whichever suits your use case.
    - Has functions `was_empty` and `was_full` which have _well-defined_ behaviour - chosen to not allow false positives.
    - Also offers a `was_size` function - a useful approximate estimate, especially when debugging or testing!
- Easily tunable and configurable for your platform and use case. See [below](#tuning-the-queue-for-your-platform) for more information on this.

More precisely, in terms of performance figures, you should consider using this queue in the following circumstances:

- You want the lowest possible latency MPMC queue in any situation ([benchmarks](#latency)).
- You want the highest throughput thread safe queue in any situation (including SPSC) ([benchmarks](#throughput)).

Obviously, if in doubt run your own benchmarks to choose the best solution for your use case! There are of course some cases where better solutions exist elsewhere. We list some here:

- You want an unbounded queue. In which case, a block-based linked queue such as `xenium/ramalhete_queue` would be your best bet, both in terms of latency and throughput.
- You want the lowest latency strictly SPSC queue. In which case, `atomic_queue` used in SPSC mode should give better results.
- You want the highest throughput SPSC queue and only need blocking `push` and `pop` operations. In which case, `atomic_queue` used in SPSC mode should give better results.
- You are running on a virtualised CPU. I doubt that some of the optimisations used here would play well with the scheduler in this case...

Note that all of my benchmarking has been done on x86, although I have done some testing on ARM to verify the functionality, as memory barriers are different here. I may return to this in future.

## Usage

The queue is provided as a single header which you can include into your project.

There are several template parameters to configure the queue. First, you must provide the data type and queue size. Then we provide two primary modes via a boolean flag - true to minimise latency (the default configuration), false to maximise throughput. We also provide a flag NONBLOCKING which defaults to true. If you want the lowest possible average latency and don't care much about theoretical guarantees of lockfree behaviour, you can set this to false. This removes one of the two CAS operations in the push/pop operations, leading to significantly lower latency on average. Then there are some (optional but recommended) pause length parameters which depend on the platform - there are benchmarks that can be used to choose these (more on this below), or alternatively guidance is provided for appropriate values.

We provide the following public methods (note `T/T&&` indicates that it accepts a pass by value only if the type is trivially copyable and <= 16 bytes):
- `push(T/T&& element)` Push an element to the queue, and busy wait if it is full.
- `T pop()` Pop an element from the queue and return it, and busy wait if it is empty.
- `bool try_push(T/T&& element)` Push an element to the queue and return true if it is not full, otherwise return false.
- `bool try_pop(T& element)` Pop an element from the queue and return true if it is not empty, otherwise return false.
- `bool was_empty()` Queue was empty when checked. May return a false negative, but not a false positive.
- `bool was_full()` Queue was full when checked. May return a false negative, but not a false positive.
- `size_t was_size()` Approximate size of the queue, only really useful for debug purposes.

Note that the size of the queue must be a power of two - using a different size will produce a compilation error. Furthermore, for non-trivially copyable types, and any type of size greater than 16 bytes, the queue is move-only. Finally, if you use the `try_push`/`try_pop` functions in a spin loop, you do not need to add a spin pause as this is built in and tuned for optimum performance already. Note that there is no measurable performance hit from using these methods, unlike some [other implementations](#throughput)!

```
#include <circular_queue.h>

struct msg_t { ... };

// Queue of integers of size 1024, minimise latency mode
lockfree::circular_queue<int, 1024, true> q;

std::unique_ptr<msg_t> message = std::make_unique<msg_t>(...);

q.push(std::move(message));

if (q.try_pop())
{
    ...
}

```

## Benchmarks

We provide a selection of benchmarks to compare against existing implementations. All benchmarks were run on an AMD Ryzen 5600X. We show a few different configurations of our queue: Optimised for latency or throughput, and truly lock-free or the faster version described above. We colour our queue optimised for latency in yellow, throughput in red, and more aggressively optimised for throughput in black. Note that the queues in grey are _SPSC-only_.

### Latency

For this test, we take two queues, and in two threads repeatedly push to one queue and pop from the other. We push 100000 integers through the queue this way. This measures the average time taken for a value to be pushed and then popped from a queue - thus is a measure of latency.

![latency comparison](./benchmarks/plots/comparison/latency.png)

### Throughput

Here, we simply push 1000000 integers through the queue for varying numbers of producers and consumers, and measure the time taken. We offer two benchmarks here.

The first uses the common "push" and "try_pop" pattern - consumer threads will monitor for cancellation, and clear out the queue before exiting. Note that many existing implementation struggle with this: `atomic_queue` suffers a huge performance hit when using `try_pop`, and `ramalhete_queue` does not offer any method to determine whether the queue is empty (or the current size).

![throughput comparison 1](./benchmarks/plots/comparison/throughput.png)

For the second benchmark, each consumer simply pops a fixed number of elements (essentially total number of values / number of consumers) before returning.

![throughput comparison 2](./benchmarks/plots/comparison/throughput2.png)

## Tuning the queue for your platform

Depending on your CPU architecture, your spin pause length may differ significantly. Based on limited testing, the following choices may work well for x86 processors:

  - PAUSE_SHORT = 180 / NUM_CYCLES_PER_PAUSE
  - PAUSE_LONG = 3000 / NUM_CYCLES_PER_PAUSE (only used when maximising throughput)

Where NUM_CYCLES_PER_PAUSE is dependent on the target CPU - on modern AMD/Intel CPUs it is likely around the range of 30-150 clock cycles, but may be shorter on some older models.

If you want the maximum performance, you may wish to benchmark using `benchmarks/test_latency.cpp` or `benchmarks/test_throughput.cpp` to choose optimum pause lengths. These will generate graphs like the below.

![pause length latency](./benchmarks/plots/pause_lengths/latency.png)

![pause length throughput](./benchmarks/plots/pause_lengths/throughput.png)

## Testing

The script `tests/test_correctness.cpp` is designed to test the safety of the queue by essentially running a throughput benchmark for varying numbers of producers and consumers in order to achieve maximum contention in a variety of circumstances, whilst also storing the results and ensuring that what went into the queue came out the same. I have run this test extensively on both x86 and ARM platforms with no issues. Furthermore, the throughput benchmark also verifies that the number of values pushed matches the number of values received. However, a formal proof of correctness would require more leg work than I am willing to spend right now, thus I cannot guarantee there are no extreme edge cases that lead to UB.

## Design, Implementation and Optimisation Decisions

Please see [my website](https://austinhill.me/posts/lockfree-queue/) for a full write up of this project, as well as some motivation and background.
