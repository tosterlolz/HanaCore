#include "scheduler.hpp"
#include "../mem/bump_alloc.hpp"
#include "../utils/logger.hpp"

extern "C" void context_switch(uint64_t **old_sp_ptr, uint64_t **new_sp_ptr);

using namespace hanacore::scheduler;

static const size_t TASK_STACK_SIZE = 16 * 1024;

static int next_pid = 1;

Task *hanacore::scheduler::current_task = nullptr;
Task *hanacore::scheduler::task_list = nullptr;

static void task_cleanup();

// Called after switching to a new task
static void task_trampoline() {
    Task *t = current_task;
    if (t && t->entry) {
        t->entry();
    }
    task_cleanup();
}

void init_scheduler() {
    // Serial debug output to avoid dependency on higher-level print
    auto serial_putc = [](char c) {
        asm volatile ("outb %0, $0x3f8" : : "a"(c));
    };
    serial_putc('S'); // start mark

    // Create main kernel task
    Task *main = (Task *)bump_alloc_alloc(sizeof(Task), 16);
    main->pid = next_pid++;
    main->state = TASK_RUNNING;
    main->next = main;
    main->entry = nullptr;

    uint64_t *rsp_val;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp_val));
    main->rsp = rsp_val;

    serial_putc('R');
    task_list = main;
    current_task = main;

    log_info("scheduler: initialized");
    log_hex64("scheduler: main task ptr", (uint64_t)main);
    log_hex64("scheduler: main rsp", (uint64_t)main->rsp);
}

static void task_cleanup() {
    log_info("scheduler: task cleanup (halting)");
    current_task->state = TASK_DEAD;
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

static Task *create_task_internal(void (*entry)(void)) {
    Task *t = (Task *)bump_alloc_alloc(sizeof(Task), 16);
    if (!t) return nullptr;
    uint8_t *stack = (uint8_t *)bump_alloc_alloc(TASK_STACK_SIZE, 16);
    if (!stack) return nullptr;

    uint64_t *sp = (uint64_t *)(stack + TASK_STACK_SIZE);

    // Stack layout expected by context_switch
    // After pop+ret: jump -> task_trampoline
    *(--sp) = (uint64_t)task_trampoline;
    *(--sp) = 0; // rbp
    *(--sp) = 0; // rbx
    *(--sp) = 0; // r12
    *(--sp) = 0; // r13
    *(--sp) = 0; // r14
    *(--sp) = 0; // r15

    t->rsp = sp;
    t->pid = next_pid++;
    t->state = TASK_READY;
    t->entry = entry;
    t->next = nullptr;
    return t;
}

int create_task(void (*entry)(void)) {
    Task *t = create_task_internal(entry);
    if (!t) return 0;

    if (!task_list) {
        task_list = t;
        current_task = t;
        t->next = t;
    } else {
        t->next = current_task->next;
        current_task->next = t;
    }

    log_info("scheduler: created task");
    log_hex64("scheduler: new task ptr", (uint64_t)t);
    log_hex64("scheduler: new task rsp", (uint64_t)t->rsp);
    return t->pid;
}

int sched_getpid() {
    return current_task ? current_task->pid : 0;
}

void schedule_next() {
    if (!current_task || !current_task->next || current_task->next == current_task)
        return;

    Task *prev = current_task;
    Task *next = current_task->next;

    current_task = next;

    log_hex64("scheduler: switching prev pid", prev->pid);
    log_hex64("scheduler: switching next pid", next->pid);
    log_hex64("scheduler: prev rsp addr", (uint64_t)prev->rsp);
    log_hex64("scheduler: next rsp addr", (uint64_t)next->rsp);

    context_switch(&prev->rsp, &next->rsp);

    log_info("scheduler: returned from context_switch");
}
