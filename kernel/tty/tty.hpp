// Minimal TTY abstraction layer
#pragma once

extern "C" {
    // Initialize TTY subsystem (optional)
    void tty_init(void);

    // Write a NUL-terminated string to the TTY
    void tty_write(const char* s);

    // Write a single character to the TTY
    void tty_putc(char c);

    // Poll for a character from the TTY input (returns 0 if none)
    char tty_poll_char(void);

    // Enqueue input bytes into the TTY input buffer (used by keyboard driver
    // to inject multi-byte sequences like ANSI escape sequences). The data
    // pointed to by `s` must be NUL-terminated.
    void tty_enqueue_input(const char* s);

    // Switch to virtual terminal number (1..12). If n is out of range,
    // the call is ignored.
    void tty_switch_vt(int n);
}

#if defined(__cplusplus)
namespace hanacore { namespace tty {
    void tty_init();
    void tty_write(const char* s);
    void tty_putc(char c);
    char tty_poll_char();
    void tty_switch_vt(int n);
} }
#endif
