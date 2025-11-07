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
};

// Globals
extern Task *current_task;
extern Task *task_list;

// Scheduler API
void init_scheduler();
int create_task(void (*entry)(void));
void sched_yield();
void schedule_next();
int sched_getpid();

} // namespace hanacore::scheduler
