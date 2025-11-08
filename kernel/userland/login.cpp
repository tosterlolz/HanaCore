#include "login.hpp"
#include "../userland/users.hpp"
#include "../tty/tty.hpp"
#include "../libs/libc.h"
#include "../utils/logger.hpp"
#include "../scheduler/scheduler.hpp"
#include <string.h>

extern "C" {
    void print(const char*);
    char keyboard_poll_char(void);
}

namespace hanacore {
    namespace shell {

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

                        // Start the interactive shell
                        return;
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
