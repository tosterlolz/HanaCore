#include "scheduler.hpp"
#include "../mem/bump_alloc.hpp"
#include "../utils/logger.hpp"

//TODO: fix this shit

static int next_pid = 1;

namespace hanacore::scheduler {

Task *current_task = nullptr;
Task *task_list = nullptr;

void init_scheduler() {
    // Allocate main task structure
    Task *main = (Task *)bump_alloc_alloc(sizeof(Task), 16);
    if (!main) {
        for (;;) asm volatile("cli; hlt");
    }
    
    main->pid = next_pid++;
    main->state = TASK_RUNNING;
    main->next = main;
    main->entry = nullptr;

    task_list = main;
    current_task = main;
}

int create_task(void (*entry)(void)) {
    if (!entry) return 0;
    
    // Allocate task structure
    Task *t = (Task *)bump_alloc_alloc(sizeof(Task), 16);
    if (!t) return 0;

    // Initialize task structure
    t->pid = next_pid++;
    t->state = TASK_READY;
    t->entry = entry;
    t->next = nullptr;

    // Insert into circular task list after current_task
    if (!task_list) {
        task_list = t;
        current_task = t;
        t->next = t;
    } else {
        t->next = current_task->next;
        current_task->next = t;
    }

    return t->pid;
}

void schedule_next() {
    if (!current_task || !current_task->next) {
        return;
    }
    
    // Don't switch if there's only one task
    if (current_task->next == current_task) {
        return;
    }

    Task *prev = current_task;
    Task *next = current_task->next;
    
    // Skip dead tasks
    while (next->state == TASK_DEAD && next != prev) {
        next = next->next;
    }
    
    if (next == prev) {
        return;  // No runnable tasks
    }

    // Update current task pointer
    current_task = next;
    next->state = TASK_RUNNING;
    
    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
    }

    // For cooperative scheduling, just return
    // Tasks will call sched_yield() when they want to yield
}

void sched_yield() {
    schedule_next();
}

int sched_getpid() {
    return current_task ? current_task->pid : 0;
}

} // namespace hanacore::scheduler
