#include "scheduler.hpp"
#include "../mem/bump_alloc.hpp"
#include "../utils/logger.hpp"
#include <string.h>

extern "C" void context_switch(uint64_t **old_sp_ptr, uint64_t **new_sp_ptr,
                               void *old_fx, void *new_fx);

static int next_pid = 1;

namespace hanacore::scheduler {

Task *current_task = nullptr;
Task *task_list = nullptr;

// Per-CPU runqueue (currently single CPU only)
Task *per_cpu_current[SCHED_MAX_CPUS] = { nullptr };
Task *per_cpu_task_list[SCHED_MAX_CPUS] = { nullptr };

static const size_t TASK_STACK_SIZE = 16 * 1024;
static const size_t FX_STATE_SIZE = 512;

static inline int get_cpu_id() { return 0; }

void init_scheduler() {
    hanacore::utils::log_info_cpp("scheduler: initializing");
    // DEBUG: allocate main task in static storage to rule out bump allocator issues
    static Task main_storage;
    Task *main = &main_storage;
    memset(main, 0, sizeof(Task));
    main->pid = next_pid++;
    main->state = TASK_RUNNING;
    main->next = main;
    hanacore::utils::log_info_cpp("scheduler: main task allocated");
    // Save current stack
    uint64_t *rsp_val;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp_val));
    main->rsp = rsp_val;
    hanacore::utils::log_info_cpp("scheduler: main rsp saved");
    // Allocate and clear fx state region (16B aligned)
    main->fx_state = bump_alloc_alloc(FX_STATE_SIZE, 16);
    if (!main->fx_state) for (;;) asm volatile("cli; hlt");
    memset(main->fx_state, 0, FX_STATE_SIZE);
    hanacore::utils::log_info_cpp("scheduler: main fx_state allocated");
    int cpu = get_cpu_id();
    per_cpu_task_list[cpu] = main;
    per_cpu_current[cpu] = main;
    current_task = main;
    task_list = main;

    log_info("scheduler: initialized main task");
    log_hex64("scheduler: main task ptr", (uint64_t)main);
    log_hex64("scheduler: main rsp", (uint64_t)main->rsp);
    log_hex64("scheduler: main fx_state", (uint64_t)main->fx_state);
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
    return create_task_on_cpu(entry, 0);
}

int create_task_on_cpu(void (*entry)(void), int cpu) {
    if (!entry) return 0;
    if (cpu < 0 || cpu >= SCHED_MAX_CPUS) return 0;

    Task *t = (Task *)bump_alloc_alloc(sizeof(Task), 16);
    if (!t) return 0;
    memset(t, 0, sizeof(Task));

    uint8_t *stack = (uint8_t *)bump_alloc_alloc(TASK_STACK_SIZE, 16);
    if (!stack) return 0;

    t->pid = next_pid++;
    t->state = TASK_READY;
    t->entry = entry;

    t->fx_state = bump_alloc_alloc(FX_STATE_SIZE, 16);
    if (!t->fx_state) return 0;
    memset(t->fx_state, 0, FX_STATE_SIZE);

    uint64_t *sp = (uint64_t *)(stack + TASK_STACK_SIZE);

    // Ensure 16-byte alignment for System V ABI
    sp = (uint64_t *)((uintptr_t)sp & ~0xF);

    *(--sp) = (uint64_t)task_trampoline; // return address for ret
    *(--sp) = 0; // rbp
    *(--sp) = 0; // rbx
    *(--sp) = 0; // r12
    *(--sp) = 0; // r13
    *(--sp) = 0; // r14
    *(--sp) = 0; // r15

    t->rsp = sp;

    // insert into per-CPU circular list
    if (!per_cpu_task_list[cpu]) {
        per_cpu_task_list[cpu] = t;
        per_cpu_current[cpu] = t;
        t->next = t;
    } else {
        Task *cur = per_cpu_current[cpu];
        t->next = cur->next;
        cur->next = t;
    }

    if (cpu == 0) {
        task_list = per_cpu_task_list[0];
        current_task = per_cpu_current[0];
    }

    log_info("scheduler: created task");
    log_hex64("scheduler: created task ptr", (uint64_t)t);
    log_hex64("scheduler: created task rsp", (uint64_t)t->rsp);
    log_hex64("scheduler: created task fx", (uint64_t)t->fx_state);
    return t->pid;
}

void schedule_next() {
    int cpu = get_cpu_id();
    Task *prev = per_cpu_current[cpu];
    if (!prev || !prev->next || prev->next == prev) return;

    Task *next = prev->next;

    // find next runnable
    while (next->state == TASK_DEAD && next->next != prev) {
        next = next->next;
    }
    if (next == prev || !next) return;

    per_cpu_current[cpu] = next;
    current_task = next;

    if (prev->state == TASK_RUNNING) prev->state = TASK_READY;
    next->state = TASK_RUNNING;

    // sanity checks
    if (!prev->rsp || !next->rsp) return;
    if (!prev->fx_state || !next->fx_state) return;

    // Debug logging: show pointers and RSPs before switching
    log_hex64("scheduler: switching prev", (uint64_t)prev);
    log_hex64("scheduler: prev->rsp", (uint64_t)prev->rsp);
    log_hex64("scheduler: prev->fx", (uint64_t)prev->fx_state);
    log_hex64("scheduler: switching next", (uint64_t)next);
    log_hex64("scheduler: next->rsp", (uint64_t)next->rsp);
    log_hex64("scheduler: next->fx", (uint64_t)next->fx_state);

    context_switch(&prev->rsp, &next->rsp, prev->fx_state, next->fx_state);
}

void sched_yield() {
    schedule_next();
}

int sched_getpid() {
    return current_task ? current_task->pid : 0;
}

} // namespace hanacore::scheduler
