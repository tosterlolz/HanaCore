// Simple polled PS/2 keyboard driver
// Provides keyboard_init() and keyboard_poll_char() (returns 0 if no char)
// and keyboard_poll_and_log() which will print characters when available.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Use the kernel-provided print() to emit characters when requested.
extern "C" void print(const char*);
// TTY switch hook (Alt+Fn)
#include "../tty/tty.hpp"

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a" (val), "Nd" (port));
}

static inline void io_wait(void) {
    // Port 0x80 is used for 'delay' on x86
    asm volatile ("outb %%al, $0x80" : : "a" (0));
}

// PS/2 controller ports
enum {
    PS2_DATA = 0x60,
    PS2_STATUS = 0x64,
};

// Minimal US set 1 scancode -> ASCII map (no AltGr, minimal symbols)
static const char scancode_map[] = {
    0,  0x1B, '1','2','3','4','5','6','7','8','9','0','-','=', '\b', '\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,  'a','s',
    'd','f','g','h','j','k','l',';', '\'', '`',  0, '\\', 'z','x','c','v',
    'b','n','m',',','.','/', 0,  '*', 0,  ' ',
};

// Shifted variants for common printable keys (keeps digits to symbols)
static const char scancode_map_shift[] = {
    0,  0x1B, '!','@','#','$','%','^','&','*','(',')','_','+', '\b', '\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,  'A','S',
    'D','F','G','H','J','K','L',':','"','~', 0,  '|', 'Z','X','C','V',
    'B','N','M','<','>','?', 0,  '*', 0,  ' ',
};

// Track shift key state
static bool shift_down = false;
// Track Alt and Ctrl key state
static bool alt_down = false;
static bool ctrl_down = false;

// Debug helper: print a single byte as [HH]
static void print_scancode(uint8_t sc) {
    const char *hex = "0123456789ABCDEF";
    char buf[4] = { '[', '0', '0', ']' };
    buf[1] = hex[(sc >> 4) & 0xF];
    buf[2] = hex[sc & 0xF];
    buf[3] = '\0';
    print(buf);
}

extern "C" void keyboard_init(void) {
    // Drain the PS/2 output buffer if any
    while (inb(PS2_STATUS) & 1) {
        (void)inb(PS2_DATA);
    }
}

// Return ASCII char or 0 if none available
extern "C" char keyboard_poll_char(void) {
    // Check output buffer full
    if (!(inb(PS2_STATUS) & 1)) return 0;

    uint8_t sc = inb(PS2_DATA);

    // Handle extended prefix (E0). Read the following byte if available
    if (sc == 0xE0) {
        // If next byte hasn't arrived yet, bail (will be handled next poll)
        if (!(inb(PS2_STATUS) & 1)) return 0;
        uint8_t next = inb(PS2_DATA);
        // Map common extended keys (arrow keys, home/end, insert/delete)
        // into ANSI sequences by enqueueing the remainder and returning ESC
        // so the TTY layer receives a standard escape sequence stream.
        switch (next) {
            case 0x48: // Up
                tty_enqueue_input("[A");
                return '\x1B';
            case 0x50: // Down
                tty_enqueue_input("[B");
                return '\x1B';
            case 0x4B: // Left
                tty_enqueue_input("[D");
                return '\x1B';
            case 0x4D: // Right
                tty_enqueue_input("[C");
                return '\x1B';
            case 0x47: // Home
                tty_enqueue_input("[H");
                return '\x1B';
            case 0x4F: // End
                tty_enqueue_input("[F");
                return '\x1B';
            case 0x52: // Insert
                tty_enqueue_input("[2~");
                return '\x1B';
            case 0x53: // Delete
                tty_enqueue_input("[3~");
                return '\x1B';
            default:
                // Unknown extended code: print for debugging
                print_scancode(0xE0);
                print_scancode(next);
                return 0;
        }
    }

    // Key release event: top bit set
    if (sc & 0x80) {
        uint8_t base = sc & 0x7F;
        if (base == 0x2A || base == 0x36) shift_down = false;
        if (base == 0x38) alt_down = false;
        if (base == 0x1D) ctrl_down = false;
        return 0;
    }

    // Key press handling
    if (sc == 0x2A || sc == 0x36) { // left or right shift
        shift_down = true;
        return 0;
    }

    if (sc == 0x38) { // left Alt press
        alt_down = true;
        return 0;
    }

    if (sc == 0x1D) { // left Ctrl press
        ctrl_down = true;
        return 0;
    }

    // If Alt is held and a function key is pressed, switch VT
    if (alt_down) {
        int vt = -1;
        if (sc >= 0x3B && sc <= 0x44) {
            vt = (int)(sc - 0x3B) + 1; // F1..F10 -> 1..10
        } else if (sc == 0x57) vt = 11; // F11
        else if (sc == 0x58) vt = 12; // F12

        if (vt != -1) {
            // call TTY switch (C linkage)
            tty_switch_vt(vt);
            return 0;
        }
    }

    // Some environments emit arrow/home/etc. scancodes without the E0
    // prefix. Handle common arrow/home/end/insert/delete scancodes here
    // as well so they map correctly to ANSI sequences.
    switch (sc) {
        case 0x48: // Up
            tty_enqueue_input("[A");
            return '\x1B';
        case 0x50: // Down
            tty_enqueue_input("[B");
            return '\x1B';
        case 0x4B: // Left
            tty_enqueue_input("[D");
            return '\x1B';
        case 0x4D: // Right
            tty_enqueue_input("[C");
            return '\x1B';
        case 0x47: // Home
            tty_enqueue_input("[H");
            return '\x1B';
        case 0x4F: // End
            tty_enqueue_input("[F");
            return '\x1B';
        case 0x52: // Insert
            tty_enqueue_input("[2~");
            return '\x1B';
        case 0x53: // Delete
            tty_enqueue_input("[3~");
            return '\x1B';
        default:
            break;
    }

    // Only map scancodes that are in our small table range
    if (sc < sizeof(scancode_map)) {
        char c = shift_down ? scancode_map_shift[(size_t)sc] : scancode_map[(size_t)sc];
        // If Ctrl is held and we have a letter, convert to control code (e.g. 'L'->0x0C)
        if (ctrl_down) {
            if (c >= 'a' && c <= 'z') c = (char)(c & 0x1F);
            else if (c >= 'A' && c <= 'Z') c = (char)(c & 0x1F);
        }
        return c;
    }

    // Unmapped scancode: print for debugging
    print_scancode(sc);
    return 0;
}

// Convenience helper: poll once and print any character read
extern "C" void keyboard_poll_and_log(void) {
    char c = keyboard_poll_char();
    if (!c) return;
    char buf[2] = { c, '\0' };
    print(buf);
}
