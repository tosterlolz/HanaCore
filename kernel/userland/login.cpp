#include "login.hpp"
#include "../userland/users.hpp"
#include "../tty/tty.hpp"
#include "../libs/libc.h"
#include "../utils/logger.hpp"
#include "../scheduler/scheduler.hpp"
#include "../filesystem/vfs.hpp"
#include "elf_loader.hpp"
#include "../shell/shell.hpp"
#include "../../third_party/limine/limine.h"
#include <string.h>
#include <cstdio>
#include "../mem/heap.hpp"

extern volatile struct limine_module_request module_request;
extern volatile struct limine_hhdm_request limine_hhdm_request;

extern "C" {
    void print(const char*);
    char keyboard_poll_char(void);
    int memcmp(const void* s1, const void* s2, size_t n);
}

namespace hanacore {
    namespace userland {

        // Simple password input (no echo)
        static void read_password(char* buf, int maxlen) {
            int pos = 0;
            while (pos < maxlen - 1) {
                char c = keyboard_poll_char();
                if (c == 0) continue;  // No character available, try again
                if (c == '\n' || c == '\r') {
                    tty_write("\n");
                    break;
                } else if (c == '\b' && pos > 0) {
                    tty_write("\b \b");  // Backspace
                    pos--;
                    buf[pos] = '\0';
                } else if (c >= 32 && c < 127) {
                    tty_write("*");  // Echo asterisk instead of password char
                    buf[pos++] = c;
                    buf[pos] = '\0';
                }
            }
            buf[pos] = '\0';
        }

        static void read_line(char* buf, int maxlen) {
            int pos = 0;
            while (pos < maxlen - 1) {
                char c = keyboard_poll_char();
                if (c == 0) continue;  // No character available, try again
                if (c == '\n' || c == '\r') {
                    tty_write("\n");
                    break;
                } else if (c == '\b' && pos > 0) {
                    tty_write("\b \b");  // Backspace
                    pos--;
                    buf[pos] = '\0';
                } else if (c >= 32 && c < 127) {
                    buf[pos++] = c;
                    buf[pos] = '\0';
                    char ch[2] = {c, '\0'};
                    tty_write(ch);
                }
            }
            buf[pos] = '\0';
        }

        void login_main(void) {
            // Initialize user system and TTY
            hanacore::users::init_users();
            tty_init();

            const char* banner = 
                "\n"
                "  ╔═══════════════════════════════════════╗\n"
                "  ║        HanaCore Operating System      ║\n"
                "  ║           Login Interface             ║\n"
                "  ╚═══════════════════════════════════════╝\n"
                "\n";
            
            print(banner);

            char username[64];
            char password[128];
            int failed_attempts = 0;
            const int max_attempts = 3;

            while (failed_attempts < max_attempts) {
                print("login: ");
                read_line(username, sizeof(username));

                if (strcmp(username, "root") == 0) {
                    print("Password: ");
                } else {
                    print("Password: ");
                }
                read_password(password, sizeof(password));

                if (hanacore::users::authenticate_user(username, password)) {
                    // Authentication successful
                    const hanacore::users::User* user = hanacore::users::get_user_by_name(username);
                    if (user) {
                        hanacore::users::set_current_user(user->uid, user->gid);
                        
                        // Print welcome message
                        print("\nWelcome to HanaCore!\n");
                        print("Type 'help' for available commands.\n\n");

                        // Launch the user's shell
                        size_t shell_size = 0;
                        void* shell_data = nullptr;
                        bool shell_from_vfs = false;

                        // First try VFS
                        shell_data = ::vfs_get_file_alloc(user->shell, &shell_size);
                        if (shell_data && shell_size > 0) shell_from_vfs = true;
                        hanacore::utils::log_info_cpp("login: VFS lookup for shell returned");
                        
                        // Try uppercase variant if lowercase failed
                        if (!(shell_data && shell_size > 0) && user->shell[0] == '/') {
                            char upper_shell[256];
                            strncpy(upper_shell, user->shell, 255);
                            for (int i = 0; upper_shell[i]; i++) {
                                if (upper_shell[i] >= 'a' && upper_shell[i] <= 'z') {
                                    upper_shell[i] = upper_shell[i] - 'a' + 'A';
                                }
                            }
                            shell_data = ::vfs_get_file_alloc(upper_shell, &shell_size);
                            if (shell_data && shell_size > 0) shell_from_vfs = true;
                            hanacore::utils::log_info_cpp("login: VFS lookup for uppercase shell returned");
                        }
                        
                        // If still not found, try Limine modules
                        if (!(shell_data && shell_size > 0) && module_request.response) {
                            hanacore::utils::log_info_cpp("login: Searching Limine modules for shell");
                            volatile struct limine_module_response* resp = module_request.response;
                            // Extract just the filename from the shell path
                            const char* shell_name = user->shell;
                            for (const char* p = user->shell; *p; p++) {
                                if (*p == '/') shell_name = p + 1;
                            }
                            
                            for (uint64_t i = 0; i < resp->module_count; ++i) {
                                volatile struct limine_file* mod = resp->modules[i];
                                const char* path = (const char*)(uintptr_t)mod->path;
                                if (path && limine_hhdm_request.response) {
                                    uint64_t hoff = limine_hhdm_request.response->offset;
                                    if ((uint64_t)path < hoff) path = (const char*)((uintptr_t)path + hoff);
                                }
                                if (!path) continue;
                                
                                // Check if this module matches the shell name
                                size_t pl = 0; while (path[pl]) ++pl;
                                size_t sl = 0; while (shell_name[sl]) ++sl;
                                if (pl >= sl && !memcmp(path + pl - sl, shell_name, sl)) {
                                    hanacore::utils::log_info_cpp("login: Found shell in Limine modules");
                                    uintptr_t addr = (uintptr_t)mod->address;
                                    if (limine_hhdm_request.response) {
                                        uint64_t off = limine_hhdm_request.response->offset;
                                        if ((uint64_t)addr < off) addr = (uintptr_t)(off + addr);
                                    }
                                        shell_data = (void*)addr;
                                        shell_from_vfs = false;
                                    shell_size = (size_t)mod->size;
                                    break;
                                }
                            }
                        }
                        
                        if (shell_data && shell_size > 0) {
                            // If we discovered the shell from a Limine module, shell_data
                            // points into the module image and must not be freed. Detect
                            // that case by checking whether shell_data was set from a
                            // module (above we set it to module address). We mark
                            // shell_from_vfs=false when using module lookup below.
                            
                            void* entry = elf64_load_from_memory(shell_data, shell_size);
                            if (entry) {
                                hanacore::utils::log_info_cpp("login: Launching shell as user task");
                                // Create a user task and wait for it to exit, then
                                // return to the login prompt. Use a 64KB user stack.
                                const size_t USER_STACK = 64 * 1024;
                                int pid = hanacore::scheduler::create_user_task(entry, USER_STACK);
                                if (pid == 0) {
                                    print("Failed to create user shell task.\n");
                                    hanacore::utils::log_info_cpp("login: create_user_task failed");
                                } else {
                                    // If shell_data came from VFS, free it now that
                                    // elf_loader has copied segments into bump alloc.
                                    if (shell_from_vfs) hanacore::mem::kfree(shell_data);
                                    // Wait for shell to exit
                                    hanacore::scheduler::wait_task(pid);
                                    hanacore::utils::log_info_cpp("login: shell exited, returning to login prompt");
                                }
                            } else {
                                print("Failed to load shell binary.\n");
                                hanacore::utils::log_info_cpp("login: ELF load failed for shell");
                                if (shell_from_vfs) hanacore::mem::kfree(shell_data);
                            }
                        } else {
                            print("Shell not found.\n");
                            hanacore::utils::log_info_cpp("login: Shell not found in any location");

                            // Fallback: launch built-in shell
                            print("Using built-in shell.\n\n");
                            hanacore::utils::log_info_cpp("login: Launching built-in shell");
                            hanacore::shell::builtin_shell_main();
                        }
                        // Do not return; stay in login loop so user can re-login after shell exits
                    }
                } else {
                    failed_attempts++;
                    if (failed_attempts < max_attempts) {
                        print("Login failed. Try again.\n\n");
                    } else {
                        print("Maximum login attempts exceeded. System halting.\n");
                        // In a real system, we might reboot here
                        for (;;) asm volatile("hlt");
                    }
                }
            }
        }

    } // namespace shell
} // namespace hanacore
