// Minimal stub to satisfy link-time reference for userland coreutils.
// This returns a default value; replace or remove if you want the
// real syscall mechanism available to userland binaries.
#include <stdint.h>

uint64_t syscall_dispatch(uint64_t num, uint64_t a, uint64_t b, uint64_t c) {
    (void)num; (void)a; (void)b; (void)c;
    return 0;
}
