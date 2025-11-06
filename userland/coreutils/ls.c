#include <stdint.h>

extern uint64_t syscall_dispatch(uint64_t num, uint64_t a, uint64_t b, uint64_t c);

extern void _start(void) {
    const char *msg = "ls: not implemented\n";
    syscall_dispatch(1, (uint64_t)msg, 0, 0);
    syscall_dispatch(2, 0, 0, 0);
}
