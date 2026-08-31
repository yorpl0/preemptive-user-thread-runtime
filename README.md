# Preemptive User-Thread Runtime

A compact user-level threading library for Linux, written in C. The runtime creates
independent execution contexts in one process and schedules them with a combination
of fixed priorities and round-robin time slices.

The project is deliberately small: the scheduler, context switching, and
synchronization primitives can all be read without working through a framework.

## What it supports

- user threads with private stacks and integer priorities from 0 to 127
- immediate scheduling when a newly created thread has a higher priority
- time-sliced preemption with `SIGVTALRM`
- cooperative yielding and thread joins
- blocking locks, condition variables, and counting semaphores
- FIFO order among threads at the same priority

## Build and test

The runtime targets Linux because it uses POSIX `ucontext`, signals, and virtual
interval timers.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Public declarations are in `ThreadSystemCode/ThreadLibrary/threads.h`. The
automated smoke test covers scheduling, joins, semaphores, conditions, locks, and
the queue.

## Design notes

Each thread owns a `ucontext_t` and a 64 KiB stack. Ready threads are stored in 128
priority queues. The scheduler chooses the highest non-empty queue, while the timer
handler returns a running thread to the back of its queue to provide round-robin
execution among equal-priority threads. Synchronization operations block threads in
scheduler-managed wait queues instead of spinning.

This is an educational runtime, not a replacement for POSIX threads. In particular,
context switching from a signal handler is representative of classic teaching
implementations rather than a production async-signal-safe design.

## License

Available under the MIT license. See [LICENSE](LICENSE).
