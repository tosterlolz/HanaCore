// Minimal TTY implementation: thin wrapper around existing console helpers.
#include "tty.hpp"
#include "../drivers/screen.hpp"

extern "C" {
    void print(const char* s);
    char keyboard_poll_char(void);
}

#include "pty.hpp"

// Small internal input buffer (not strictly necessary yet)
static char input_buf[256];
static unsigned input_read = 0;
static unsigned input_write = 0;

extern "C" void tty_init(void) {
    input_read = input_write = 0;
}

// Enqueue a NUL-terminated string into the TTY input buffer. Called by
// keyboard driver to inject sequences like "\x1b[A". It's safe to call
// from IRQ/poll contexts in our simple kernel.
extern "C" void tty_enqueue_input(const char* s) {
    if (!s) return;
    const char* p = s;
    while (*p) {
        unsigned idx = input_write & (sizeof(input_buf) - 1);
        input_buf[idx] = *p++;
        ++input_write;
    }
}

// tty_write/tty_putc implemented further below (with VT buffering)

// tty_poll_char is defined after VT helpers so those static symbols are visible.

// Virtual terminal support
#define TTY_NUM_VT 12
#define TTY_BUF_SZ (16 * 1024)

static char vt_buf[TTY_NUM_VT][TTY_BUF_SZ];
static unsigned vt_head[TTY_NUM_VT]; // next write index
static int active_vt = 0; // 0..(TTY_NUM_VT-1)

// Switch to virtual terminal (1..12). Internally stored 0..11
extern "C" void tty_switch_vt(int n) {
    if (n < 1 || n > TTY_NUM_VT) return;
    int idx = n - 1;
    if (idx == active_vt) return;
    active_vt = idx;

    // Clear screen and replay buffer for the selected VT
    clear_screen();
    // Print buffered content for this VT safely (buffer is not NUL-terminated).
    unsigned len = vt_head[idx];
    if (len == 0) {
        // No buffered content for this VT yet — show a simple header so the
        // user sees a new, empty terminal window instead of a blank screen.
        char hdr[32];
        // "VT %d\n"
        hdr[0] = 'V'; hdr[1] = 'T'; hdr[2] = ' ';
        int n = (idx + 1);
        if (n >= 10) {
            hdr[3] = '1'; hdr[4] = '0' + (n - 10); hdr[5] = '\n'; hdr[6] = '\0';
        } else {
            hdr[3] = '0' + n; hdr[4] = '\n'; hdr[5] = '\0';
        }
        // Append header into the VT buffer directly (avoid calling vt_append
        // before its definition).
        const char *hp = hdr;
        while (*hp) {
            unsigned h = vt_head[idx] & (TTY_BUF_SZ - 1);
            vt_buf[idx][h] = *hp++;
            vt_head[idx]++;
        }
        print(hdr);
        return;
    }

    // If we've wrapped, only print the most recent TTY_BUF_SZ bytes.
    unsigned start = 0;
    if (len > TTY_BUF_SZ) start = len - TTY_BUF_SZ;
    for (unsigned i = start; i < len; ++i) {
        char ch = vt_buf[idx][i & (TTY_BUF_SZ - 1)];
        char tmp[2] = { ch, '\0' };
        print(tmp);
    }
}

// Append to active VT buffer and print if active
static void vt_append(const char* s) {
    if (!s) return;
    // append bytes to buffer until NUL
    const char* p = s;
    while (*p) {
        unsigned h = vt_head[active_vt] & (TTY_BUF_SZ - 1);
        vt_buf[active_vt][h] = *p++;
        vt_head[active_vt]++;
    }
}

extern "C" void tty_write(const char* s) {
    if (!s) return;
    vt_append(s);
    // Delegate to existing print for actual console output
    print(s);
}

extern "C" void tty_putc(char c) {
    char tmp[2] = { c, '\0' };
    vt_append(tmp);
    print(tmp);
}

// Now define tty_poll_char with VT/PTY integration
extern "C" char tty_poll_char(void) {
    // If we have buffered input, return it first
    if (input_read != input_write) {
        char c = input_buf[input_read & (sizeof(input_buf)-1)];
        ++input_read;
        return c;
    }

    // Otherwise poll keyboard directly
    char c = keyboard_poll_char();
    if (!c) return 0;

    // Push into active VT buffer
    vt_append((const char[]){c, '\0'});

    // If an attached PTY slave exists for the active VT, push input to it
    int pid = pty_vt_map_get(active_vt);
    if (pid >= 0) pty_slave_push_input(pid, c);
    return c;
}
