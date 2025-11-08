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
	// If creating a task with an argument, the entry should be a function
	// compatible with void (*)(void*). We store the argument pointer here and
	// also an optional entry function pointer that accepts the argument.
	void (*entry_arg_fn)(void*);
	void *entry_arg;      // optional argument passed to entry when using create_task_with_arg
	// Per-task file descriptor table
	struct FDEntry *fds; // pointer to fd table (allocated at task creation)
	int fd_count;
	// Simple child/wait tracking
	int exit_status; // if task exited, store status
	int parent_pid;
	// User-mode task fields (if is_user is true): entry point and user stack
	bool is_user;
	void *user_entry;    // user-mode RIP
	void *user_stack;    // pointer to user-mode stack bottom (virtual)
	size_t user_stack_size;
};

// Globals for single-CPU scheduler
extern Task *current_task;
extern Task *task_list;

// Scheduler API
void init_scheduler();
int create_task(void (*entry)(void));
// Create a task with a void* argument passed to the entry function. The
// entry must have the signature void (*)(void*).
int create_task_with_arg(void (*entry)(void*), void* arg);
// Create a user-mode task: the ELF entry will be run in CPL=3 using an iret
// transition. This is a minimal implementation that does not yet provide
// full address-space isolation (VMM mapping is a no-op at present).
int create_user_task(void *user_entry, size_t user_stack_size);
void sched_yield();
void schedule_next();
int sched_getpid();
Task* find_task_by_pid(int pid);

} // namespace hanacore::scheduler

