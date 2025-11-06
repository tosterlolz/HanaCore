// Simple keyboard driver API (polled PS/2)
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize keyboard (drain buffers, prepare state)
void keyboard_init(void);

// Poll for a character. Returns ASCII character, or 0 if none available.
char keyboard_poll_char(void);

// Convenience: poll once and print any character via kernel print().
void keyboard_poll_and_log(void);

#ifdef __cplusplus
}
#endif
