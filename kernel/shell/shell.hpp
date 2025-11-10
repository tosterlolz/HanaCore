#pragma once

namespace hanacore {
namespace shell {

// Main shell command loop - runs an interactive shell
// This is a built-in shell that runs in the kernel
void builtin_shell_main(void);

// Utility functions for parsing
class CommandParser {
public:
    // Parse a command line into command and arguments
    static void parse(const char* line, char* cmd, int cmd_size, char* args, int args_size);
    
    // Split arguments into tokens
    static int tokenize(const char* args, char* tokens[], int max_tokens, int token_size);
    
    // Get current working directory
    static const char* get_cwd(void);
    
    // Set current working directory
    static int set_cwd(const char* path);
};

} // namespace shell
} // namespace hanacore
