#include "shell.hpp"
#include "../filesystem/fat32.hpp"
#include "../userland/elf_loader.hpp"
#include <stddef.h>

// Use C linkage to match kernel symbols
extern "C" {
	void print(const char*);
	char keyboard_poll_char(void);

	// builtin ls implemented in kernel/shell/coreutils/ls.cpp
	void builtin_ls_cmd(const char* path);

	// builtin lsblk implemented in kernel/shell/coreutils/lsblk.cpp
	void builtin_lsblk_cmd(const char* arg);
}

static char cwd[256] = "/";
// Current drive letter for the shell prompt and relative-path resolution.
// Uppercase ASCII 'A'..'Z'. Default to 'C'.
static char current_drive = 'C';

static void print_prompt() {
	print("user@hanacore:");
	// Print drive prefix, e.g. "C:/path"
	char d[3] = { current_drive, ':', '\0' };
	print(d);
	print(cwd);
	print("$ ");
}

// Simple line buffer
#define SHELL_BUF_SIZE 128
namespace hanacore {
	namespace shell {
		void shell_main(void) {
			char buf[SHELL_BUF_SIZE];
			size_t pos = 0;
			print("Welcome to HanaShell!\n");
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
							// `cd` with no args -> go to root of current drive
							cwd[0] = '/'; cwd[1] = '\0';
						} else if ((arg[0] >= 'A' && arg[0] <= 'Z') || (arg[0] >= 'a' && arg[0] <= 'z')) {
							// Drive-prefixed path: D: or D:/path
							char d = arg[0]; if (d >= 'a' && d <= 'z') d = d - 'a' + 'A';
							current_drive = d;
							if (arg[1] == ':' ) {
								if (arg[2] == '/' ) {
									// copy remainder as cwd (starts with '/')
									size_t i = 0; while (i + 1 < sizeof(cwd) && arg[2 + i]) { cwd[i] = arg[2 + i]; ++i; }
									if (i == 0) { cwd[0] = '/'; cwd[1] = '\0'; } else { cwd[i] = '\0'; }
								} else {
									// Just drive letter (e.g. "D:") -> go to root
									cwd[0] = '/'; cwd[1] = '\0';
								}
							} else {
								// Not a drive prefix, fall back to relative handling below
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
						} else if (arg[0] == '/') {
							// Absolute path on current drive
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
						// Reset input buffer and prompt
						pos = 0;
						print_prompt();
						continue;
					}

					// Handle builtin: ls
					if (pos >= 2 && buf[0] == 'l' && buf[1] == 's' && (pos == 2 || buf[2] == ' ')) {
						const char* arg = NULL;
						if (pos > 3) arg = &buf[3];
						else if (pos == 3) arg = &buf[3];
						char path[256];
						// If no arg, list current drive + cwd
						if (!arg || *arg == '\0') {
							// "C:/path"
							path[0] = current_drive; path[1] = ':';
							size_t i = 0; while (i + 1 + 2 < sizeof(path) && cwd[i]) { path[2 + i] = cwd[i]; ++i; }
							if (i == 0) {
								path[2] = '/'; path[3] = '\0';
							} else {
								path[2 + i] = '\0';
							}
						} else if (((arg[0] >= 'A' && arg[0] <= 'Z') || (arg[0] >= 'a' && arg[0] <= 'z')) && arg[1] == ':') {
							// Drive-prefixed argument: normalize letter to uppercase
							char d = arg[0]; if (d >= 'a' && d <= 'z') d = d - 'a' + 'A';
							path[0] = d; path[1] = ':';
							if (arg[2] == '\0') { path[2] = '/'; path[3] = '\0'; }
							else {
								size_t i = 0; while (2 + i + 1 < sizeof(path) && arg[2 + i]) { path[2 + i] = arg[2 + i]; ++i; }
								path[2 + i] = '\0';
							}
						} else if (arg[0] == '/') {
							// Absolute path on current drive: prefix drive
							path[0] = current_drive; path[1] = ':';
							size_t i = 0; while (i + 1 + 2 < sizeof(path) && arg[i]) { path[2 + i] = arg[i]; ++i; }
							path[2 + i] = '\0';
						} else {
							// Relative: prefix with current drive and cwd
							path[0] = current_drive; path[1] = ':';
							size_t len = 0; while (cwd[len]) ++len;
							size_t p = 0;
							for (p = 0; p < len && 2 + p + 1 < sizeof(path); ++p) path[2 + p] = cwd[p];
							if (p > 1 && path[2 + p - 1] != '/') { if (2 + p + 1 < sizeof(path)) path[2 + p++] = '/'; }
							size_t i = 0;
							while (2 + p + i + 1 < sizeof(path) && arg[i]) { path[2 + p + i] = arg[i]; ++i; }
							path[2 + p + i] = '\0';
						}
						builtin_ls_cmd(path);
						pos = 0;
						print_prompt();
						continue;
					}

						// builtin: lsblk
						if (pos >= 5 && buf[0] == 'l' && buf[1] == 's' && buf[2] == 'b' && buf[3] == 'l' && buf[4] == 'k') {
							// no args supported yet
							builtin_lsblk_cmd(NULL);
							pos = 0;
							print_prompt();
							continue;
						}

					// Try to execute /bin/<cmd> from rootfs image. Only use the
					// first token of the input (the command name) and ignore args
					// when locating the binary in /bin.
					char fullpath[256];
					const char* prefix = "/bin/";
					size_t prelen = 5;
					// find first token length (up to space or end)
					size_t cmdlen = 0;
					while (cmdlen < pos && buf[cmdlen] && buf[cmdlen] != ' ') ++cmdlen;
					if (cmdlen + prelen + 1 < sizeof(fullpath)) {
						// build /bin/<cmd>
						for (size_t i = 0; i < prelen; ++i) fullpath[i] = prefix[i];
						for (size_t i = 0; i < cmdlen; ++i) fullpath[prelen + i] = buf[i];
						size_t plen = prelen + cmdlen;
						fullpath[plen] = '\0';

						print("Trying to execute ");
						print(fullpath);
						print("\n");

						size_t fsize = 0;
						void* data = hanacore::fs::fat32_get_file_alloc(fullpath, &fsize);
						if (data) {
							print("Loaded file from FAT32 (size: ");
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
	} // namespace shell
} // namespace hanacore