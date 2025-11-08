#include "scheduler.hpp"
#include "../mem/heap.hpp"
#include "../utils/logger.hpp"
#include "../userland/fdtable.hpp"
#include <string.h>

extern "C" void context_switch(uint64_t **old_sp_ptr, uint64_t **new_sp_ptr,
                               void *old_fx, void *new_fx);

static int next_pid = 1;

namespace hanacore::scheduler {

Task *current_task = nullptr;
Task *task_list = nullptr;

static const size_t TASK_STACK_SIZE = 16 * 1024;

static inline int get_cpu_id() { return 0; }

void init_scheduler() {
    // Initialize heap for task allocations
    hanacore::mem::heap_init(256 * 1024); // 256KiB heap

    // Try to allocate main task from heap, fall back to static if unavailable
    static Task main_storage;
    Task *main = (Task *)kmalloc(sizeof(Task));
    if (!main) main = &main_storage;
    memset(main, 0, sizeof(Task));
    main->pid = next_pid++;
    main->state = TASK_RUNNING;
    main->next = main;

    // Save current stack pointer
    uint64_t *rsp_val;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp_val));
    main->rsp = rsp_val;

    current_task = main;
    task_list = main;

    log_info("scheduler: initialized main task");
    log_hex64("scheduler: main task ptr", (uint64_t)main);
    log_hex64("scheduler: main rsp", (uint64_t)main->rsp);
}

static void task_cleanup(void) {
    log_info("scheduler: task cleanup (halting)");
    if (current_task) current_task->state = TASK_DEAD;
    for (;;) asm volatile("cli; hlt");
}

static void task_trampoline(void) {
    if (current_task && current_task->entry) {
        current_task->entry();
    }
    task_cleanup();
}

int create_task(void (*entry)(void)) {
    if (!entry) return 0;
    Task *t = (Task *)kmalloc(sizeof(Task));
    if (!t) return 0;
    memset(t, 0, sizeof(Task));

    uint8_t *stack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    if (!stack) return 0;

    t->pid = next_pid++;
    t->state = TASK_READY;
    t->entry = entry;
    // allocate FD table for the task
    t->fd_count = 64;
    t->fds = fdtable_create(t->fd_count);
    if (t->fds) {
        // initialize std fds to TTY placeholders (0/1/2)
        // actual mapping to tty is done elsewhere; mark them as TTY for now
        for (int i = 0; i < 3 && i < t->fd_count; ++i) {
            t->fds[i].type = FD_TTY;
        }
    }
    t->exit_status = -1;
    t->parent_pid = 0;

    uint64_t *sp = (uint64_t *)(stack + TASK_STACK_SIZE);
    // Align to 16 bytes
    sp = (uint64_t *)((uintptr_t)sp & ~0xF);

    // Prepare minimal frame matching context_switch which pushes/pops
    // rbp, rbx, r12, r13, r14, r15 and then ret -> task_trampoline
    *(--sp) = (uint64_t)task_trampoline; // return address
    *(--sp) = 0; // rbp
    *(--sp) = 0; // rbx
    *(--sp) = 0; // r12
    *(--sp) = 0; // r13
    *(--sp) = 0; // r14
    *(--sp) = 0; // r15

    t->rsp = sp;

    // Insert into circular list after current_task
    if (!task_list) {
        task_list = t;
        t->next = t;
        current_task = t;
    } else {
        Task *cur = task_list;
        // Insert at tail
        while (cur->next && cur->next != task_list) cur = cur->next;
        cur->next = t;
        t->next = task_list;
    }

    log_info("scheduler: created task");
    log_hex64("scheduler: created task ptr", (uint64_t)t);
    log_hex64("scheduler: created task rsp", (uint64_t)t->rsp);

    return t->pid;
}

void schedule_next() {
    if (!current_task) return;
    Task *prev = current_task;
    Task *next = prev->next;
    if (!next || next == prev) return; // nothing to switch to

    // Find next runnable
    Task *start = next;
    while (next->state == TASK_DEAD) {
        next = next->next;
        if (next == start) return; // no runnable tasks
    }

    current_task = next;

    if (prev->state == TASK_RUNNING) prev->state = TASK_READY;
    next->state = TASK_RUNNING;

    if (!prev->rsp || !next->rsp) return;

    log_hex64("scheduler: switching prev", (uint64_t)prev);
    log_hex64("scheduler: prev->rsp", (uint64_t)prev->rsp);
    log_hex64("scheduler: next", (uint64_t)next);
    log_hex64("scheduler: next->rsp", (uint64_t)next->rsp);

    // Perform the context switch: save previous stack pointer and load next.
    // context_switch is declared at file scope above.
    log_info("scheduler: performing context switch");
    context_switch(&prev->rsp, &next->rsp, nullptr, nullptr);

    // When we return here, we've switched back to this task. Update state.
}

void sched_yield() {
    schedule_next();
}

int sched_getpid() {
    return current_task ? current_task->pid : 0;
}

Task* find_task_by_pid(int pid) {
    if (!task_list) return nullptr;
    Task* cur = task_list;
    do {
        if (cur->pid == pid) return cur;
        cur = cur->next;
    } while (cur && cur != task_list);
    return nullptr;
}

} // namespace hanacore::scheduler
