# Preemptive User-Thread Runtime

A compact educational runtime that demonstrates how multiple execution flows can be scheduled inside a single Linux process without using `pthread` for thread management.

The project models the core responsibilities of a threading system: saving execution state, selecting the next runnable thread, handling completion, and protecting shared data. Its scheduler supports voluntary yields as well as timer-driven preemption.

## Runtime design

Each user thread owns a stack and a saved CPU context. Runnable threads wait in a FIFO queue, and the scheduler rotates through that queue using round-robin order.

A virtual interval timer delivers `SIGVTALRM` after each time slice. When the signal arrives, the runtime preserves the current context and transfers control to the next ready thread. Scheduler-critical operations temporarily block the timer signal so queue and lifecycle state cannot be interrupted halfway through an update.

Thread execution follows this lifecycle:

```text
created -> ready -> running -> completed
                 |       |
                 +-> suspended
```

Completed threads retain their return value until another thread joins them, after which their runtime resources can be reclaimed.

## Supported operations

- initialize the runtime with a configurable scheduling quantum
- create a thread from a function and argument
- yield execution voluntarily
- identify the currently running thread
- wait for a thread and collect its return value
- suspend and resume existing threads
- terminate a thread explicitly
- coordinate access to shared state with a lightweight lock

## What this project demonstrates

- user-space context switching
- preemptive round-robin scheduling
- signal masking around scheduler-critical state
- thread lifecycle and resource ownership
- synchronization between independently scheduled tasks

## Scope

This is a learning project intended to make scheduling internals small enough to inspect and explain. It targets Linux on x86-64 and is not intended to replace production threading libraries such as POSIX threads.

This repository currently contains project documentation only.
