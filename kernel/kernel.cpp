#include "screen.h"
#include "drivers/framebuffer.h"

// Linker script symbols for init/fini arrays
extern "C" {
    extern void (*__init_array_start)();
    extern void (*__init_array_end)();
    extern void (*__fini_array_start)();
    extern void (*__fini_array_end)();
}

static inline void serial_putchar(char c) {
    __asm__ volatile("outb %0, $0x3f8" : : "a"(c));
}

static void serial_puts(const char* str) {
    if (!str) return;
    while (*str) {
        if (*str == '\n') {
            serial_putchar('\r');
        }
        serial_putchar(*str++);
    }
}

// Call global constructors
static void call_constructors() {
    // Safely iterate through init array
    if (&__init_array_start == 0 || &__init_array_end == 0) {
        return;
    }
    
    for (void (**func)() = &__init_array_start; func != &__init_array_end; func++) {
        if (*func != nullptr) {
            (*func)();
        }
    }
}

// Call global destructors
static void call_destructors() {
    // Safely iterate through fini array
    if (&__fini_array_start == 0 || &__fini_array_end == 0) {
        return;
    }
    
    for (void (**func)() = &__fini_array_start; func != &__fini_array_end; func++) {
        if (*func != nullptr) {
            (*func)();
        }
    }
}

extern "C" void kernel_main() {
    serial_puts("\n=== HanaCore Kernel Starting ===\n");
    
    // Call global constructors
    call_constructors();
    
    clear_screen();
    print("🎉 HanaCore Kernel Initialized!\n");
    print("Bootloader: Limine (x86_64)\n");
    print("Welcome to HanaCore — minimalist C++ OS kernel.\n");
    print("System ready.\n\n");
    
    // Try to initialize framebuffer for graphics
    if (framebuffer_init()) {
        print("✓ Framebuffer initialized!\n");
        
        uint32_t width = framebuffer_get_width();
        uint32_t height = framebuffer_get_height();
        
        // Draw some graphics
        uint32_t red = framebuffer_rgb(255, 0, 0);
        uint32_t green = framebuffer_rgb(0, 255, 0);
        uint32_t blue = framebuffer_rgb(0, 0, 255);
        uint32_t yellow = framebuffer_rgb(255, 255, 0);
        
        // Draw circles
        framebuffer_draw_filled_circle(width / 2, height / 2, 100, red);
        framebuffer_draw_filled_circle(width / 4, height / 4, 50, green);
        framebuffer_draw_filled_circle(3 * width / 4, height / 4, 50, blue);
        framebuffer_draw_filled_circle(width / 4, 3 * height / 4, 50, yellow);
        
        print("✓ Graphics rendered!\n");
    } else {
        print("⚠ Framebuffer not available\n");
    }
    
    serial_puts("Kernel initialization complete.\n");

    // Keep displaying something to show kernel is alive
    int counter = 0;
    while (true) {
        counter++;
        if (counter % 1000000 == 0) {
            print(".");
        }
        __asm__ volatile("pause");
    }
}
