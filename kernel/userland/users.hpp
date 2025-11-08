#pragma once

#include <stdint.h>

namespace hanacore {
    namespace users {

        // User ID and Group ID types
        typedef uint32_t uid_t;
        typedef uint32_t gid_t;

        // File permission bits
        struct FilePerms {
            uint16_t mode;  // rwx rwx rwx (owner group other) + file type + setuid/setgid/sticky
        };

        // User entry
        struct User {
            uid_t uid;
            gid_t gid;
            char username[64];
            char password_hash[128];  // Simple hash storage
            char home_dir[256];
            char shell[256];
        };

        // Group entry
        struct Group {
            gid_t gid;
            char groupname[64];
            uint32_t member_count;
            uid_t members[32];
        };

        // Permission check context
        struct PermContext {
            uid_t current_uid;
            gid_t current_gid;
            gid_t additional_gids[32];
            uint32_t num_additional_gids;
        };

        // Initialization
        void init_users();

        // User management
        uid_t get_uid_by_name(const char* username);
        const char* get_username_by_uid(uid_t uid);
        const User* get_user_by_uid(uid_t uid);
        const User* get_user_by_name(const char* username);

        // Group management
        gid_t get_gid_by_name(const char* groupname);
        const char* get_groupname_by_gid(gid_t gid);
        const Group* get_group_by_gid(gid_t gid);

        // Authentication
        bool authenticate_user(const char* username, const char* password);
        uid_t get_current_uid();
        gid_t get_current_gid();
        void set_current_user(uid_t uid, gid_t gid);

        // Permission checking
        bool check_permission(uint16_t mode, uid_t uid, gid_t gid, int required_bits);
        bool can_read(const char* path, uid_t uid, gid_t gid);
        bool can_write(const char* path, uid_t uid, gid_t gid);
        bool can_execute(const char* path, uid_t uid, gid_t gid);

    } // namespace users
} // namespace hanacore

// C-linkage wrappers for shell integration
extern "C" {
    uint32_t get_current_user_uid();
    uint32_t get_current_user_gid();
    const char* get_current_username();
    void set_current_user_c(uint32_t uid, uint32_t gid);
    bool authenticate_user_c(const char* username, const char* password);
}
