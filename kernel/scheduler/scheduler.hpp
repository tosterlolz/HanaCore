#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../userland/fdtable.hpp"

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
	// Per-task file descriptor table
	struct FDEntry *fds; // pointer to fd table (allocated at task creation)
	int fd_count;
	// Simple child/wait tracking
	int exit_status; // if task exited, store status
	int parent_pid;
};

// Globals for single-CPU scheduler
extern Task *current_task;
extern Task *task_list;

// Scheduler API
void init_scheduler();
int create_task(void (*entry)(void));
void sched_yield();
void schedule_next();
int sched_getpid();
Task* find_task_by_pid(int pid);

} // namespace hanacore::scheduler

