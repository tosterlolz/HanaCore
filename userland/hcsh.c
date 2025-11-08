/* Minimal userland shell (hcsh)
 * This program runs in user mode (once kernel starts user tasks). It
 * demonstrates basic use of hana_write and hana_exit from libhana.c.
 */
#include "../kernel/api/hanaapi.h"
#include <stddef.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    const char *msg = "[user hcsh] Hello from /bin/hcsh running in user mode\n";
    hana_write(1, msg, 0);
    hana_exit(0);
    return 0;
}
