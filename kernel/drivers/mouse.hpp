#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize PS/2 mouse (enable data reporting). Safe to call multiple times.
void mouse_init(void);

// Poll for a mouse packet. If a full packet is available, fills dx, dy and
// buttons and returns 1. Otherwise returns 0.
int mouse_poll_delta(int* dx, int* dy, int* buttons);

#ifdef __cplusplus
}
#endif
