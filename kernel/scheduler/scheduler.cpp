#include "scheduler.hpp"
#include "../mem/heap.hpp"
#include "../utils/logger.hpp"
#include "../userland/fdtable.hpp"
#include <string.h>

extern "C" void context_switch(uint64_t **old_sp_ptr, uint64_t **new_sp_ptr,
                               void *old_fx, void *new_fx);

namespace hanacore::scheduler {

static constexpr size_t TASK_STACK_SIZE = 16 * 1024;

static int next_pid = 1;
Task *current_task = nullptr;
Task *task_list = nullptr;

static inline int get_cpu_id() { return 0; }

// ==========================================================
// INIT
// ==========================================================
void init_scheduler() {
    hanacore::mem::heap_init(256 * 1024);

    static Task main_storage;
    Task *main = (Task *)kmalloc(sizeof(Task));
    if (!main) main = &main_storage;

    memset(main, 0, sizeof(Task));
    main->pid = next_pid++;
    main->state = TASK_RUNNING;

    uint64_t *rsp_val;
    asm volatile("mov %%rsp, %0" : "=r"(rsp_val));
    main->rsp = rsp_val;
    main->next = main;  // Single-element circular list

    current_task = main;
    task_list = main;

    log_info("scheduler: initialized main task pid=%d", main->pid);
}

// ==========================================================
// TASK CLEANUP
// ==========================================================
static void idle_task() {
    for (;;) {
        asm volatile("sti; hlt");
    }
}

void task_cleanup() {
    log_info("scheduler: task %d exiting", current_task ? current_task->pid : -1);
    if (current_task) {
        current_task->state = TASK_DEAD;
    }

    schedule_next();

    log_info("scheduler: no more tasks, starting idle loop");
    idle_task();
}

// ==========================================================
// TRAMPOLINES
// ==========================================================
static void user_mode_entry_trampoline();
static void task_trampoline() {
    if (current_task) {
        if (current_task->is_user) {
            user_mode_entry_trampoline();
        } else if (current_task->entry) {
            if (current_task->entry_arg)
                ((void(*)(void*))(current_task->entry))(current_task->entry_arg);
            else
                ((void(*)(void))(current_task->entry))();
        }
    }
    task_cleanup(); // task kończy się -> usuń z listy
}


static void user_mode_entry_trampoline() {
    if (!current_task || !current_task->user_entry) {
        uintptr_t user_rsp = (uintptr_t)current_task->user_stack + current_task->user_stack_size;
        user_rsp &= ~0xF; // align

        task_cleanup();
    }

    uintptr_t uentry = (uintptr_t)current_task->user_entry;
    uintptr_t ustack_top = (uintptr_t)current_task->user_stack + current_task->user_stack_size;
    ustack_top &= ~0xFULL;

    const uint64_t user_cs = 0x1B;
    const uint64_t user_ss = 0x23;

    asm volatile (
        "cli\n\t"
        "pushq %[ss]\n\t"
        "pushq %[rsp]\n\t"
        "pushfq\n\t"
        "popq %%rax\n\t"
        "orq $0x200, %%rax\n\t"
        "pushq %%rax\n\t"
        "pushq %[cs]\n\t"
        "pushq %[rip]\n\t"
        "iretq\n\t"
        :
        : [ss]"r"(user_ss), [rsp]"r"(ustack_top),
          [cs]"r"(user_cs), [rip]"r"(uentry)
        : "rax", "memory"
    );

    task_cleanup();
}

// ==========================================================
// TASK CREATION
// ==========================================================
static Task* alloc_task_common() {
    Task *t = (Task *)kmalloc(sizeof(Task));
    if (!t) return nullptr;
    memset(t, 0, sizeof(Task));

    t->pid = next_pid++;
    t->fd_count = 64;
    t->fds = fdtable_create(t->fd_count);

    if (t->fds) {
        for (int i = 0; i < 3 && i < t->fd_count; ++i) {
            t->fds[i].type = FD_TTY;
        }
    }

    t->exit_status = -1;
    t->parent_pid = current_task ? current_task->pid : 0;
    return t;
}

int create_task(void (*entry)(void)) {
    if (!entry) return 0;
    Task *t = alloc_task_common();
    if (!t) return 0;

    t->state = TASK_READY;
    t->entry = entry;

    uint8_t *stack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    if (!stack) return 0;

    uint64_t *sp = (uint64_t *)(stack + TASK_STACK_SIZE);
    sp = (uint64_t *)((uintptr_t)sp & ~0xF);
    *(--sp) = (uint64_t)task_trampoline;

    for (int i = 0; i < 6; ++i) *(--sp) = 0; // rbp..r15

    t->rsp = sp;
    t->kstack = stack;

    // Insert into circular list
    if (!task_list) {
        task_list = t;
        t->next = t;
    } else {
        Task *cur = task_list;
        while (cur->next && cur->next != task_list) cur = cur->next;
        cur->next = t;
        t->next = task_list;
    }

    log_info("scheduler: created task pid=%d", t->pid);
    return t->pid;
}

int create_user_task(void* user_entry, size_t user_stack_size) {
    if (!user_entry || user_stack_size == 0) return 0;

    Task* t = (Task*)kmalloc(sizeof(Task));
    if (!t) return 0;
    memset(t, 0, sizeof(Task));

    // Kernel stack
    uint8_t* kstack = (uint8_t*)kmalloc(TASK_STACK_SIZE);
    if (!kstack) { kfree(t); return 0; }

    // User stack
    void* ustack = kmalloc(user_stack_size);
    if (!ustack) { kfree(kstack); kfree(t); return 0; }

    t->pid = next_pid++;
    t->state = TASK_READY;
    t->is_user = true;
    t->user_entry = user_entry;
    t->user_stack = ustack;
    t->user_stack_size = user_stack_size;
    t->entry = user_mode_entry_trampoline; // kernel trampoline to iret
    t->entry_arg = nullptr;

    // Allocate FD table
    t->fd_count = 64;
    t->fds = fdtable_create(t->fd_count);
    if (t->fds) {
        for (int i = 0; i < 3 && i < t->fd_count; ++i)
            t->fds[i].type = FD_TTY;
    }
    t->exit_status = -1;
    t->parent_pid = 0;

    // Prepare kernel stack (callee-saved frame for context_switch)
    uint64_t* sp = (uint64_t*)(kstack + TASK_STACK_SIZE);
    sp = (uint64_t*)((uintptr_t)sp & ~0xF); // align 16B
    *(--sp) = (uint64_t)task_trampoline; // return address
    *(--sp) = 0; // rbp
    *(--sp) = 0; // rbx
    *(--sp) = 0; // r12
    *(--sp) = 0; // r13
    *(--sp) = 0; // r14
    *(--sp) = 0; // r15

    t->rsp = sp;
    t->kstack = kstack;

    // Insert into circular list after current_task
    if (!task_list) {
        task_list = t;
        t->next = t;
        current_task = t;
    } else {
        Task* cur = task_list;
        while (cur->next && cur->next != task_list) cur = cur->next;
        cur->next = t;
        t->next = task_list;
    }

    log_info("scheduler: created user task (pid=%d)", t->pid);
    return t->pid;
}


int create_task_with_arg(void (*entry)(void*), void* arg) {
    if (!entry) return 0;
    Task *t = alloc_task_common();
    if (!t) return 0;

    t->state = TASK_READY;
    t->entry = (void(*)(void))entry;
    t->entry_arg = arg;

    uint8_t *stack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    if (!stack) return 0;

    uint64_t *sp = (uint64_t *)(stack + TASK_STACK_SIZE);
    sp = (uint64_t *)((uintptr_t)sp & ~0xF);
    *(--sp) = (uint64_t)task_trampoline;
    for (int i = 0; i < 6; ++i) *(--sp) = 0;

    t->rsp = sp;
    t->kstack = stack;

    // Insert into circular list
    if (!task_list) {
        task_list = t;
        t->next = t;
    } else {
        Task *cur = task_list;
        while (cur->next && cur->next != task_list) cur = cur->next;
        cur->next = t;
        t->next = task_list;
    }

    log_info("scheduler: created task (arg) pid=%d", t->pid);
    return t->pid;
}

// ==========================================================
// SCHEDULING
// ==========================================================

void schedule_next() {
    if (!current_task || !task_list) return;

    Task *prev = current_task;

    // Disable interrupts while mutating the task list to avoid races
    asm volatile ("cli" ::: "memory");

    // Clean up any DEAD tasks in the circular list, including current task
    Task *freed_current = nullptr;
    if (task_list) {
        Task *iter = task_list;
        Task *iter_prev = nullptr;
        
        // Find the previous task (last in circular list)
        Task *tmp = task_list;
        while (tmp->next != task_list) tmp = tmp->next;
        iter_prev = tmp;
        iter = task_list;
        
        // Scan for DEAD tasks
        do {
            if (iter->state == TASK_DEAD) {
                // Remove this DEAD task
                iter_prev->next = iter->next;
                if (iter == task_list) {
                    task_list = iter->next;
                }
                if (iter == prev) {
                    freed_current = prev;
                }
                log_info("scheduler: freeing dead task pid=%d", iter->pid);
                if (iter->fds) fdtable_destroy(iter->fds, iter->fd_count);
                if (iter->user_stack) hanacore::mem::kfree(iter->user_stack);
                if (iter->kstack) hanacore::mem::kfree(iter->kstack);
                hanacore::mem::kfree(iter);
                iter = iter_prev->next;
            } else {
                iter_prev = iter;
                iter = iter->next;
            }
        } while (iter != task_list && task_list);
    }

    // Re-enable interrupts after list mutation
    asm volatile ("sti" ::: "memory");

    // Find next runnable task
    if (!task_list) {
        log_info("scheduler: no tasks in list");
        return;
    }

    // Debug: list all tasks
    {
        Task *debug_iter = task_list;
        int count = 0;
        do {
            log_info("scheduler: task[%d] pid=%d state=%d", count++, debug_iter->pid, debug_iter->state);
            debug_iter = debug_iter->next;
            if (count > 10) break; // Safety: prevent infinite loop in logging
        } while (debug_iter && debug_iter != task_list);
    }

    // Sanity check: ensure at least one READY/RUNNING task exists
    {
        Task *debug_iter = task_list;
        int found_runnable = 0, count = 0;
        do {
            if (debug_iter->state == TASK_READY || debug_iter->state == TASK_RUNNING) found_runnable++;
            debug_iter = debug_iter->next;
            if (++count > 10) break;
        } while (debug_iter && debug_iter != task_list);
        if (!found_runnable) log_info("scheduler: SANITY CHECK FAILED: no READY/RUNNING tasks in list!");
    }

    Task *next = nullptr;
    if (freed_current) {
        // The current task was freed, so start from task_list
        next = task_list;
    } else {
        // Normal case: start from prev->next
        next = prev->next ? prev->next : prev;
    }
    
    Task *probe_start = next;
    do {
        if (next && (next->state == TASK_READY || next->state == TASK_RUNNING)) break;
        if (next) next = next->next ? next->next : prev;
        else break;
    } while (next && next != probe_start);

    if (!next || (next->state != TASK_READY && next->state != TASK_RUNNING)) {
        log_info("scheduler: no runnable tasks found in list (prev pid=%d state=%d)", prev->pid, prev->state);
        return;
    }

    if (prev->state == TASK_RUNNING) prev->state = TASK_READY;
    next->state = TASK_RUNNING;
    log_info("scheduler: switch pid=%d -> pid=%d", prev->pid, next->pid);
    current_task = next;
    asm volatile ("" ::: "memory");
    context_switch(&prev->rsp, &next->rsp, nullptr, nullptr);
}

void sched_yield() {
    schedule_next();
}

int sched_getpid() {
    return current_task ? current_task->pid : 0;
}

Task* find_task_by_pid(int pid) {
    Task* cur = task_list;
    while (cur) {
        if (cur->pid == pid) return cur;
        cur = cur->next;
    }
    return nullptr;
}

void kill_task(int pid) {
    Task* t = find_task_by_pid(pid);
    if (t && t->state != TASK_DEAD) {
        t->state = TASK_DEAD;
        log_info("scheduler: killed task pid=%d", pid);
    }
}

void wait_task(int pid) {
    // Poll until task dies or is not found
    while (true) {
        Task* t = find_task_by_pid(pid);
        if (!t || t->state == TASK_DEAD) break;
        
        // Explicitly call schedule_next to let other tasks run
        schedule_next();
    }
}

} // namespace hanacore::scheduler

