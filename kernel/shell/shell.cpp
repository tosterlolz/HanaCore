#include "shell.hpp"
#include "commands.hpp"
#include "../tty/tty.hpp"
#include "../libs/libc.h"
#include "../utils/logger.hpp"
#include "../userland/users.hpp"
#include <cstdio>
#include <cstring>

extern "C" {
    void print(const char*);
    char keyboard_poll_char(void);
}

namespace hanacore {
namespace shell {

// Global current working directory
static char g_cwd[256] = "/";

const char* CommandParser::get_cwd(void) {
    return g_cwd;
}

int CommandParser::set_cwd(const char* path) {
    if (!path || path[0] == '\0') return -1;
    strncpy(g_cwd, path, sizeof(g_cwd) - 1);
    g_cwd[sizeof(g_cwd) - 1] = '\0';
    return 0;
}

void CommandParser::parse(const char* line, char* cmd, int cmd_size, char* args, int args_size) {
    if (!line || !cmd || !args) return;
    
    // Skip leading whitespace
    while (*line && (*line == ' ' || *line == '\t')) line++;
    
    // Extract command
    int i = 0;
    while (*line && *line != ' ' && *line != '\t' && i < cmd_size - 1) {
        cmd[i++] = *line++;
    }
    cmd[i] = '\0';
    
    // Skip whitespace between command and arguments
    while (*line && (*line == ' ' || *line == '\t')) line++;
    
    // Copy remaining as arguments
    i = 0;
    while (*line && i < args_size - 1) {
        args[i++] = *line++;
    }
    args[i] = '\0';
}

int CommandParser::tokenize(const char* args, char* tokens[], int max_tokens, int token_size) {
    if (!args) return 0;
    
    int count = 0;
    const char* p = args;
    
    while (*p && count < max_tokens) {
        // Skip whitespace
        while (*p && (*p == ' ' || *p == '\t')) p++;
        if (!*p) break;
        
        // Extract token
        int i = 0;
        while (*p && *p != ' ' && *p != '\t' && i < token_size - 1) {
            tokens[count][i++] = *p++;
        }
        tokens[count][i] = '\0';
        count++;
    }
    
    return count;
}

static void read_line(char* buf, int maxlen) {
    int pos = 0;
    while (pos < maxlen - 1) {
        char c = keyboard_poll_char();
        if (c == 0) continue;
        if (c == '\n' || c == '\r') {
            print("\n");
            break;
        } else if (c == '\b' && pos > 0) {
            print("\b \b");
            pos--;
            buf[pos] = '\0';
        } else if (c >= 32 && c < 127) {
            buf[pos++] = c;
            buf[pos] = '\0';
            char ch[2] = {c, '\0'};
            print(ch);
        }
    }
    buf[pos] = '\0';
}

void builtin_shell_main(void) {
    print("\n╔════════════════════════════════════════╗\n");
    print("║     HanaCore          Shell v1.0       ║\n");
    print("╚════════════════════════════════════════╝\n\n");
    print("Type 'help' for available commands.\n\n");
    
    char line[512];
    char cmd[128];
    char args[384];
    
    while (1) {
        // Print prompt with current user and path
        const char* username = hanacore::users::get_username_by_uid(hanacore::users::get_current_uid());
        if (username) {
            print(username);
        } else {
            print("root");
        }
        print(":");
        print(CommandParser::get_cwd());
        print("$ ");
        
        read_line(line, sizeof(line));
        
        if (line[0] == '\0') {
            continue;  // Empty line
        }
        
        // Parse the command
        CommandParser::parse(line, cmd, sizeof(cmd), args, sizeof(args));
        
        // Dispatch to appropriate command handler
        int ret = 0;
        if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "logout") == 0) {
            print("Exiting shell...\n");
            break;
        } else if (strcmp(cmd, "help") == 0) {
            ret = commands::cmd_help(args);
        } else if (strcmp(cmd, "echo") == 0) {
            ret = commands::cmd_echo(args);
        } else if (strcmp(cmd, "whoami") == 0) {
            ret = commands::cmd_whoami(args);
        } else if (strcmp(cmd, "version") == 0) {
            ret = commands::cmd_version(args);
        } else if (strcmp(cmd, "pwd") == 0) {
            ret = commands::cmd_pwd(args);
        } else if (strcmp(cmd, "cd") == 0) {
            ret = commands::cmd_cd(args);
        } else if (strcmp(cmd, "clear") == 0) {
            ret = commands::cmd_clear(args);
        } else if (strcmp(cmd, "ls") == 0) {
            ret = commands::cmd_ls(args);
        } else if (strcmp(cmd, "lsblk") == 0) {
            ret = commands::cmd_lsblk(args);
        } else {
            // Try to execute as external command from Limine modules
            ret = commands::cmd_exec_external(cmd, args);
        }
    }
}

} // namespace shell
} // namespace hanacore
