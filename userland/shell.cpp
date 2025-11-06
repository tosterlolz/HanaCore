#include "shell.hpp"

#include <stddef.h>

// Use C linkage to match kernel symbols
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

		// Ignore other control codes
	}
}
