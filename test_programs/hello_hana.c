#include "../kernel/api/hanaapi.h"

// Simple Hello World program using the HanaCore userland API.
// Build as an ELF (x86_64) and place the binary inside rootfs_src/bin
// so mkrootfs will include it in the FAT32 rootfs image used by the ISO.

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    const char msg[] = "Hello from HanaCore userland!\n";
    hana_write(1, msg, (size_t)(sizeof(msg) - 1));
    hana_exit(0);
    return 0;
}
