#pragma once
#include <stdint.h>
#include <stddef.h>

namespace hanacore::scheduler {

enum TaskState {
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_DEAD
};

struct Task {
    int pid;
    TaskState state;
    uint64_t *rsp;       // Saved stack pointer
    Task *next;          // Next task in circular list
    void (*entry)(void); // Entry point function
    void *fx_state;      // pointer to FX save area (fxsave/fxrstor)
};

// Globals (legacy single-CPU views kept for compatibility)
extern Task *current_task;
extern Task *task_list;

// Per-CPU runqueues (simple fixed-size array for now)
// Tune max CPUs for your target; default to 4 for initial SMP support.
constexpr int SCHED_MAX_CPUS = 4;
extern Task *per_cpu_current[SCHED_MAX_CPUS];
extern Task *per_cpu_task_list[SCHED_MAX_CPUS];

// Helper: create task on a specific CPU. The old create_task(entry) will
// still create on CPU 0 for compatibility.
int create_task_on_cpu(void (*entry)(void), int cpu);

// Scheduler API
void init_scheduler();
int create_task(void (*entry)(void));
void sched_yield();
void schedule_next();
int sched_getpid();

} // namespace hanacore::scheduler
