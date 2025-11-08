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
    log_info("scheduler: task cleanup (task exiting)");
    if (current_task) current_task->state = TASK_DEAD;
    // Try to schedule another runnable task. If none exists, fall back to
    // halting the CPU to avoid spinning the busy loop here.
    hanacore::scheduler::schedule_next();
    for (;;) asm volatile("cli; hlt");
}

static void task_trampoline(void) {
    if (current_task && current_task->entry) {
        // If entry_arg is set, call entry as void(*)(void*)
        if (current_task->entry_arg) {
            void (*fn)(void*) = (void(*)(void*))(current_task->entry);
            fn(current_task->entry_arg);
        } else {
            current_task->entry();
        }
    }
    task_cleanup();
}

// Small helper that runs in kernel context and performs an iretq to user-mode.
// It reads the current_task->user_entry and user_stack fields and constructs
// an iret frame on the kernel stack then executes iretq. This is a minimal
// transition: VMM/page-table isolation is not enforced here (vmm is a stub).
static void user_mode_entry_trampoline(void) {
    if (!current_task) {
        task_cleanup();
    }

    void* uentry = current_task->user_entry;
    void* ustack = current_task->user_stack;
    size_t usz = current_task->user_stack_size;

    if (!uentry) {
        task_cleanup();
    }

    // Compute user rsp as top of allocated user stack (grow-down)
    uintptr_t user_rsp = 0;
    if (ustack && usz > 0) {
        user_rsp = (uintptr_t)ustack + usz;
        // align to 16 bytes as required by ABI
        user_rsp &= ~((uintptr_t)0xF);
    } else {
        // no user stack provided: fallback to a small internal stack
        user_rsp = 0x800000; // arbitrary fallback (should not happen)
    }

    // User segment selectors: these must match GDT entries added for user
    // code/data: user CS = 0x1B, user SS = 0x23
    const uint64_t user_cs = 0x1B;
    const uint64_t user_ss = 0x23;

    // Prepare an iretq frame on the current kernel stack and iret into user
    // mode with RFLAGS IF bit enabled.
    asm volatile (
        "cli\n\t" // disable interrupts while preparing frame
        // push user SS
        "pushq %0\n\t"
        // push user RSP
        "pushq %1\n\t"
        // push RFLAGS (take current RFLAGS and set IF)
        "pushfq\n\t"
        "popq %%rax\n\t"
        "orq $0x200, %%rax\n\t"
        "pushq %%rax\n\t"
        // push user CS
        "pushq %2\n\t"
        // push RIP (user entry)
        "pushq %3\n\t"
        // iretq will pop RIP, CS, RFLAGS, RSP, SS and transfer to CPL=3
        "iretq\n\t"
        : /* no outputs */
        : "r"(user_ss), "r"(user_rsp), "r"(user_cs), "r"(uentry)
        : "rax", "memory"
    );

    // We should never reach here. If we do, cleanup the task.
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
    t->entry_arg = nullptr;
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

int create_task_with_arg(void (*entry)(void*), void* arg) {
    if (!entry) return 0;
    Task *t = (Task *)kmalloc(sizeof(Task));
    if (!t) return 0;
    memset(t, 0, sizeof(Task));

    uint8_t *stack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    if (!stack) return 0;

    t->pid = next_pid++;
    t->state = TASK_READY;
    t->entry = (void(*)(void))entry;
    t->entry_arg = arg;
    // allocate FD table for the task
    t->fd_count = 64;
    t->fds = fdtable_create(t->fd_count);
    if (t->fds) {
        // initialize std fds to TTY placeholders (0/1/2)
        for (int i = 0; i < 3 && i < t->fd_count; ++i) {
            t->fds[i].type = FD_TTY;
        }
    }
    t->exit_status = -1;
    t->parent_pid = 0;

    uint64_t *sp = (uint64_t *)(stack + TASK_STACK_SIZE);
    sp = (uint64_t *)((uintptr_t)sp & ~0xF);
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
        while (cur->next && cur->next != task_list) cur = cur->next;
        cur->next = t;
        t->next = task_list;
    }

    log_info("scheduler: created task (with arg)");
    log_hex64("scheduler: created task ptr", (uint64_t)t);
    log_hex64("scheduler: created task rsp", (uint64_t)t->rsp);

    return t->pid;
}

int create_user_task(void *user_entry, size_t user_stack_size) {
    if (!user_entry) return 0;
    Task *t = (Task *)kmalloc(sizeof(Task));
    if (!t) return 0;
    memset(t, 0, sizeof(Task));

    uint8_t *kstack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    if (!kstack) return 0;

    // Allocate user stack (simple heap allocation for now)
    void *ustack = nullptr;
    if (user_stack_size > 0) {
        ustack = kmalloc(user_stack_size);
        // Note: VMM mapping is a no-op currently; later this must map with user
        // permissions.
    }

    t->pid = next_pid++;
    t->state = TASK_READY;
    t->is_user = true;
    t->user_entry = user_entry;
    t->user_stack = ustack;
    t->user_stack_size = user_stack_size;
    t->entry = user_mode_entry_trampoline; // kernel wrapper that iret's
    t->entry_arg = nullptr;
    // allocate FD table for the task
    t->fd_count = 64;
    t->fds = fdtable_create(t->fd_count);
    if (t->fds) {
        for (int i = 0; i < 3 && i < t->fd_count; ++i) {
            t->fds[i].type = FD_TTY;
        }
    }
    t->exit_status = -1;
    t->parent_pid = 0;

    uint64_t *sp = (uint64_t *)(kstack + TASK_STACK_SIZE);
    sp = (uint64_t *)((uintptr_t)sp & ~0xF);
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
        while (cur->next && cur->next != task_list) cur = cur->next;
        cur->next = t;
        t->next = task_list;
    }

    log_info("scheduler: created user task");
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
