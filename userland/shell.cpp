#include "shell.hpp"

#include <stddef.h>

// Use C linkage to match kernel symbols
extern "C" {
	void print(const char*);
	char keyboard_poll_char(void);
	void* ext2_get_file_alloc(const char* path, size_t* out_len);
	void* elf64_load_from_memory(const void* data, size_t size);
}

static char cwd[256] = "/";

static void print_prompt() {
	print("user@hanacore:");
	print(cwd);
	print("$ ");
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

			// Null-terminate the command
			buf[pos] = '\0';

			// Handle simple builtin: cd
			if (pos >= 2 && buf[0] == 'c' && buf[1] == 'd' && (pos == 2 || buf[2] == ' ')) {
				// Extract argument (if any)
				const char* arg = NULL;
				if (pos > 3) arg = &buf[3];
				else if (pos == 3) arg = &buf[3];
				if (!arg || *arg == '\0') {
					// `cd` with no args -> go to root
					cwd[0] = '/'; cwd[1] = '\0';
				} else {
					// If absolute path, copy directly
					if (arg[0] == '/') {
						size_t i = 0;
						while (i + 1 < sizeof(cwd) && arg[i]) { cwd[i] = arg[i]; ++i; }
						if (i == 0) { cwd[0] = '/'; cwd[1] = '\0'; }
						else { cwd[i] = '\0'; }
					} else {
						// Relative path handling: support .. and simple append
						if (arg[0] == '.' && arg[1] == '.' && (arg[2] == '\0' || arg[2] == '/')) {
							// go up one level
							size_t len = 0; while (cwd[len]) ++len;
							if (len > 1) {
								// remove trailing '/name'
								size_t p = len - 1;
								while (p > 0 && cwd[p] != '/') --p;
								cwd[p] = '\0';
								if (p == 0) { cwd[0] = '/'; cwd[1] = '\0'; }
							}
						} else {
							// append
							size_t len = 0; while (cwd[len]) ++len;
							if (len > 1 && cwd[len-1] != '/') {
								if (len + 1 < sizeof(cwd)) { cwd[len++] = '/'; cwd[len] = '\0'; }
							}
							size_t i = 0;
							while (len + i + 1 < sizeof(cwd) && arg[i]) { cwd[len + i] = arg[i]; ++i; }
							cwd[len + i] = '\0';
						}
					}
				}
				// Reset input buffer and prompt
				pos = 0;
				print_prompt();
				continue;
			}

			// Try to execute /bin/<cmd> from ext2 image
			char fullpath[256];
			const char* prefix = "/bin/";
			size_t prelen = 5;
			if (pos + prelen + 1 < sizeof(fullpath)) {
				// build /bin/<cmd>
				for (size_t i = 0; i < prelen; ++i) fullpath[i] = prefix[i];
				for (size_t i = 0; i <= pos; ++i) fullpath[prelen + i] = buf[i];
				size_t plen = prelen + pos;
				fullpath[plen] = '\0';

				print("Trying to execute ");
				print(fullpath);
				print("\n");

				size_t fsize = 0;
				void* data = ext2_get_file_alloc(fullpath, &fsize);
				if (data) {
					print("Loaded file from ext2 (size: ");
					// simple integer print - convert decimal
					char numbuf[32];
					size_t n = 0; size_t tmp = fsize;
					if (tmp == 0) { numbuf[n++] = '0'; }
					while (tmp > 0 && n + 1 < sizeof(numbuf)) { numbuf[n++] = '0' + (tmp % 10); tmp /= 10; }
					// reverse
					for (size_t i = 0; i < n/2; ++i) { char t = numbuf[i]; numbuf[i] = numbuf[n-1-i]; numbuf[n-1-i] = t; }
					numbuf[n] = '\0';
					print(numbuf);
					print(")\n");

					void* entry = elf64_load_from_memory(data, fsize);
					if (entry) {
						print("Transferring control to ELF entry...\n");
						void (*e)(void) = (void(*)(void))entry;
						e(); // Note: executes in kernel context
						print("Returned from ELF program\n");
					} else {
						print("ELF load failed\n");
					}
				} else {
					print("File not found in rootfs: "); print(fullpath); print("\n");
				}
			} else {
				print("Command too long\n");
			}

			// Reset
			pos = 0;
			print_prompt();
			continue;
		}

		// Ignore other control codes
	}
}
