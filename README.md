# Circular Queue

This is a high performance lock-free multi-producer multi-consumer bounded queue. It is implemented as a single header only library in C++20 (although the benchmarks use C++23 for convenience).

## Who should use this

There are a number of existing lock-free queue implementations in C++ at this point. Based on the below benchmarks, you should consider using this queue in the following circumstances:

- You want the lowest possible latency MPMC queue in any situation.
- You want the highest throughput thread safe queue in any situation (including SPSC).

Obviously, if in doubt run your own benchmarks to choose the best solution for your use case! There are of course some cases where better solutions exist elsewhere. We list some here:

- You want an unbounded queue. In which case, a block-based linked queue such as `xenium/ramalhete_queue` would be your best bet, both in terms of latency and throughput.
- You want the lowest latency strictly SPSC queue. In which case, `atomic_queue` used in SPSC mode should give better results.
- You are running on a virtualised CPU. I doubt that some of the optimisations used here would play well with the scheduler in this case...

Note that all of my benchmarking has been done on x86, although I have done some testing on ARM to verify the functionality (TODO check again since adding yield).

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

Note that the size of the queue must be a power of two - using a different size will produce a compilation error. Furthermore, for non-trivially copyable types, and any type of size greater than 16 bytes, the queue is move-only. Finally, if you use the `try_push`/`try_pop` functions in a spin loop, you do not need to add a spin pause as this is built in and tuned for optimum performance already.

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

We provide a selection of benchmarks to compare against existing implementations. All benchmarks were run on an AMD Ryzen 5600X. We show a few different configurations of our queue: Optimised for latency or throughput, and truly lock-free or the faster version described above. We colour our queue optimised for throughput in red, with the version optimised for latency in yellow. Note that the queues in grey are _SPSC-only_.

### Latency

For this test, we take two queues, and in two threads repeatedly push to one queue and pop from the other. We push 100000 integers through the queue this way. This measures the average time taken for a value to be pushed and then popped from a queue - thus is a measure of latency.

![latency comparison](./benchmarks/plots/comparison/latency.png)

### Throughput

Here, we simply push 1000000 integers through the queue for a varying number of producers and consumers, and measure the time taken.

![latency comparison](./benchmarks/plots/comparison/throughput.png)

## Tuning the queue for your platform

Depending on your CPU architecture, your spin pause length may differ significantly. If you want the maximum performance, you may wish to benchmark using `benchmarks/test_latency.cpp` or `benchmarks/test_throughput.cpp` to choose optimum pause lengths. These will generate graphs like the below.

![pause length latency](./benchmarks/plots/pause_lengths/latency.png)

![pause length throughput](./benchmarks/plots/pause_lengths/throughput.png)

## Testing

The script `tests/test_correctness.cpp` is designed to test the safety of the queue by essentially running a throughput benchmark for varying numbers of producers and consumers in order to achieve maximum contention in a variety of circumstances, whilst also storing the results and ensuring that what went into the queue came out the same. I have run this test extensively on both x86 and ARM platforms with no issues. Furthermore, the throughput benchmark also verifies that the number of values pushed matches the number of values received. However, a formal proof of correctness would require more leg work than I am willing to spend right now, thus I cannot guarantee there are no extreme edge cases that lead to UB.

## Design, Implementation and Optimisation Decisions

Please see [my website](https://austinhill.me/posts/lockfree-queue/) for a full write up of this project, as well as some motivation and background.
