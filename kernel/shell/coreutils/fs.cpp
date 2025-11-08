#include "../../filesystem/vfs.hpp"
#include "../../filesystem/fat32.hpp"
#include "../../filesystem/hanafs.hpp"
#include "../../filesystem/isofs.hpp"
#include <stddef.h>
#include <string.h>
#include <ctype.h>

extern "C" void print(const char*);
extern "C" void tty_write(const char*);

// Helper to print a string with newline
static void println(const char* s) {
    if (s) tty_write(s);
    tty_write("\n");
}

// Helper to parse arguments
static int parse_args(const char* arg, char** argv, int max_argc) {
    if (!arg) return 0;
    int argc = 0;
    const char* start = arg;
    char buf[256];
    size_t buf_pos = 0;
    
    for (const char* p = arg; *p && argc < max_argc; ++p) {
        if (*p == ' ' || *p == '\t') {
            if (buf_pos > 0) {
                buf[buf_pos] = '\0';
                argv[argc] = (char*)start; // Store position in original string
                argc++;
                buf_pos = 0;
                start = p + 1;
            }
        } else if (buf_pos < sizeof(buf) - 1) {
            buf[buf_pos++] = *p;
        }
    }
    
    if (buf_pos > 0 && argc < max_argc) {
        buf[buf_pos] = '\0';
        argv[argc++] = (char*)start;
    }
    
    return argc;
}

// fs mount <fstype> <mountpoint>
static void fs_mount(const char* fstype, const char* mountpoint) {
    if (!fstype || !mountpoint) {
        println("usage: fs mount <fstype> <mountpoint>");
        println("  fstype: fat32, hanafs, isofs, procfs, devfs");
        return;
    }
    
    // Validate fstype
    if (strcmp(fstype, "fat32") != 0 && 
        strcmp(fstype, "hanafs") != 0 &&
        strcmp(fstype, "isofs") != 0 &&
        strcmp(fstype, "procfs") != 0 &&
        strcmp(fstype, "devfs") != 0) {
        println("error: unsupported filesystem type");
        return;
    }
    
    hanacore::fs::register_mount(fstype, mountpoint);
    tty_write("fs: mounted ");
    tty_write(fstype);
    tty_write(" at ");
    println(mountpoint);
}

// fs list
static void fs_list(void) {
    println("Available filesystems:");
    hanacore::fs::list_mounts([](const char* line) {
        tty_write("  ");
        println(line);
    });
    
    println("\nSupported filesystem types:");
    println("  fat32  - FAT32 filesystem");
    println("  hanafs - HanaCore native filesystem");
    println("  isofs  - ISO 9660 cdrom filesystem");
    println("  procfs - Process filesystem (virtual)");
    println("  devfs  - Device filesystem (virtual)");
}

// fs format <type> <drive>
static void fs_format(const char* type, const char* drive) {
    if (!type) {
        println("usage: fs format <type> [drive]");
        println("  type: hanafs, fat32");
        println("  drive: ata, ata0 (default), ata1");
        return;
    }
    
    if (strcmp(type, "hanafs") == 0) {
        // Parse drive number
        int drive_num = 0;
        if (drive && drive[0] == '1') {
            drive_num = 1;
        }
        
        tty_write("fs: formatting ATA drive ");
        if (drive) tty_write(drive);
        else tty_write("ata0");
        println(" as HanaFS...");
        
        int rc = hanacore::fs::hanafs_format_ata_master(drive_num);
        if (rc == 0) {
            println("fs: format completed successfully");
        } else {
            println("fs: format failed");
        }
    } else if (strcmp(type, "fat32") == 0) {
        println("error: FAT32 formatting not yet implemented");
    } else {
        println("error: unsupported filesystem type for formatting");
    }
}

// fs info
static void fs_info(void) {
    println("HanaCore Filesystem Manager");
    println("===========================");
    println("");
    println("Available filesystems:");
    hanacore::fs::list_mounts([](const char* line) {
        tty_write("  ");
        println(line);
    });
    
    println("");
    println("Usage:");
    println("  fs mount <fstype> <mountpoint>  - Mount a filesystem");
    println("  fs list                         - List mounted filesystems");
    println("  fs format <type> [drive]        - Format a drive");
    println("  fs info                         - Show this help");
}

extern "C" void builtin_fs_cmd(const char* arg) {
    if (!arg || *arg == '\0') {
        fs_info();
        return;
    }
    
    // Parse first argument (subcommand)
    const char* cmd = arg;
    const char* rest = arg;
    
    // Find end of first word
    while (*rest && *rest != ' ' && *rest != '\t') {
        rest++;
    }
    
    // Skip whitespace
    while (*rest && (*rest == ' ' || *rest == '\t')) {
        rest++;
    }
    
    // Handle subcommands
    if (strncmp(cmd, "mount", 5) == 0 && (!cmd[5] || cmd[5] == ' ' || cmd[5] == '\t')) {
        // fs mount <fstype> <mountpoint>
        if (!*rest) {
            println("usage: fs mount <fstype> <mountpoint>");
            return;
        }
        
        // Parse fstype and mountpoint
        const char* fstype = rest;
        const char* mp_start = rest;
        
        while (*mp_start && *mp_start != ' ' && *mp_start != '\t') {
            mp_start++;
        }
        
        while (*mp_start && (*mp_start == ' ' || *mp_start == '\t')) {
            mp_start++;
        }
        
        if (!*mp_start) {
            println("usage: fs mount <fstype> <mountpoint>");
            return;
        }
        
        // Create null-terminated fstype and mountpoint
        size_t fstype_len = (size_t)(mp_start - 1 - fstype);
        while (fstype_len > 0 && (rest[fstype_len - 1] == ' ' || rest[fstype_len - 1] == '\t')) {
            fstype_len--;
        }
        
        char fstype_buf[32];
        if (fstype_len >= sizeof(fstype_buf)) fstype_len = sizeof(fstype_buf) - 1;
        strncpy(fstype_buf, fstype, fstype_len);
        fstype_buf[fstype_len] = '\0';
        
        fs_mount(fstype_buf, mp_start);
    } 
    else if (strncmp(cmd, "list", 4) == 0 && (!cmd[4] || cmd[4] == ' ' || cmd[4] == '\t')) {
        fs_list();
    }
    else if (strncmp(cmd, "format", 6) == 0 && (!cmd[6] || cmd[6] == ' ' || cmd[6] == '\t')) {
        // fs format <type> [drive]
        if (!*rest) {
            println("usage: fs format <type> [drive]");
            return;
        }
        
        const char* type = rest;
        const char* drive_start = rest;
        
        while (*drive_start && *drive_start != ' ' && *drive_start != '\t') {
            drive_start++;
        }
        
        while (*drive_start && (*drive_start == ' ' || *drive_start == '\t')) {
            drive_start++;
        }
        
        size_t type_len = (size_t)(drive_start - 1 - type);
        while (type_len > 0 && (rest[type_len - 1] == ' ' || rest[type_len - 1] == '\t')) {
            type_len--;
        }
        
        char type_buf[32];
        if (type_len >= sizeof(type_buf)) type_len = sizeof(type_buf) - 1;
        strncpy(type_buf, type, type_len);
        type_buf[type_len] = '\0';
        
        fs_format(type_buf, *drive_start ? drive_start : nullptr);
    }
    else if (strncmp(cmd, "info", 4) == 0 && (!cmd[4] || cmd[4] == ' ' || cmd[4] == '\t')) {
        fs_info();
    }
    else {
        println("error: unknown fs subcommand");
        println("  try: fs mount|list|format|info");
    }
}

