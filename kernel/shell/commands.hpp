#pragma once

namespace hanacore {
namespace shell {
namespace commands {

// Command handler function pointer
typedef int (*cmd_handler_t)(const char* args);

// Register and execute built-in commands
int cmd_help(const char* args);
int cmd_echo(const char* args);
int cmd_whoami(const char* args);
int cmd_version(const char* args);
int cmd_pwd(const char* args);
int cmd_cd(const char* args);
int cmd_clear(const char* args);
int cmd_ls(const char* args);
int cmd_lsblk(const char* args);

// Try to execute external command from Limine modules or system paths
int cmd_exec_external(const char* cmdname, const char* args);

} // namespace commands
} // namespace shell
} // namespace hanacore
