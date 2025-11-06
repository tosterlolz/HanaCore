#include "screen.h"

extern "C" void kernel_main() {
    clear_screen();
    print("HanaCore Kernel Initialized \n");
    print("Welcome to HanaCore — minimalist C++ OS kernel.\n");
    print("System ready.\n");

    while (true) {
        // Prosta pętla nieskończona — tu można dodać obsługę przerwań
    }
}
