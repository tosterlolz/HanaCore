// Simple polled PS/2 keyboard driver
// Provides keyboard_init() and keyboard_poll_char() (returns 0 if no char)
// and keyboard_poll_and_log() which will print characters when available.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Use the kernel-provided print() to emit characters when requested.
extern "C" void print(const char*);

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
        // For now just print the two-byte scancode for diagnosis and ignore
        print_scancode(0xE0);
        print_scancode(next);
        return 0;
    }

    // Key release event: top bit set
    if (sc & 0x80) {
        uint8_t base = sc & 0x7F;
        if (base == 0x2A || base == 0x36) shift_down = false;
        return 0;
    }

    // Key press handling
    if (sc == 0x2A || sc == 0x36) { // left or right shift
        shift_down = true;
        return 0;
    }

    // Only map scancodes that are in our small table range
    if (sc < sizeof(scancode_map)) {
        char c = shift_down ? scancode_map_shift[(size_t)sc] : scancode_map[(size_t)sc];
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
