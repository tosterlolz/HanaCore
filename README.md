# HanaCore
## a simple ToyOS in C++ i guess

## Dependencies:
* gcc, binutils
* nasm
* justfile
* qemu
* cmake
* brain


## how to clone

```bash
git clone --recurse-submodules https://github.com/tosterlolz/HanaCore.git
```

## how to build
```bash
just build # this build the kernel

just run # this builds the kernel and runs qemu
```

## build your own program for this kernel
### example shell code
```cpp
// shell.cpp
#include "shell.hpp"
#include <stddef.h>

extern "C" {
	void print(const char*);
	char keyboard_poll_char(void);
}

static void print_prompt() {
	print("user@hanacore:$ ");
}

// Simple line buffer
#define SHELL_BUF_SIZE 128

extern "C" void shell_main(void) {
	char buf[SHELL_BUF_SIZE];
	size_t pos = 0;
	print("Welcome to HanaCore shell!\n");
	print_prompt();

	while (1) {
		char c = keyboard_poll_char();
		if (!c) {
			// no input, yield a tiny pause
			__asm__ volatile ("pause");
			continue;
		}

		// Handle backspace (0x08) and DEL (0x7f)
		if (c == '\b' || c == 0x7f) {
			if (pos > 0) {
				pos--;
				// erase character visually: backspace + space + backspace
				print("\b ");
				print("\b");
			}
			continue;
		}

		// Echo printable chars
		if ((unsigned char)c >= 0x20 && (unsigned char)c <= 0x7e) {
			char s[2] = { c, '\0' };
			print(s);
			if (pos + 1 < SHELL_BUF_SIZE) buf[pos++] = c;
			continue;
		}

		// Enter (newline) — process the line
		if (c == '\n' || c == '\r') {
			print("\n");
			if (pos == 0) {
				print_prompt();
				continue;
			}

			// Null-terminate and echo the line as a fake command execution
			buf[pos] = '\0';
			print("You typed: ");
			print(buf);
			print("\n");

			// Reset
			pos = 0;
			print_prompt();
			continue;
		}

	}
}
// shell.hpp
#pragma once
#include <stdint.h>

extern "C" void log_ok(const char *msg);
extern "C" void log_fail(const char *msg);
extern "C" void log_info(const char *msg);
extern "C" void log_hex64(const char *label, uint64_t value);

```

### Build
```bash
x86_64-elf-g++ -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-exceptions -fno-rtti -fno-stack-protector -c userland/shell.cpp -o build/shell.o
```