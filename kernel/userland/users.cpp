#include "users.hpp"
#include "../utils/logger.hpp"
#include "../libs/libc.h"
#include <string.h>

namespace hanacore {
    namespace users {

        // Simple user database (in-memory, can be extended to disk storage)
        static User user_database[64];
        static int user_count = 0;

        static Group group_database[16];
        static int group_count = 0;

        // Current user context
        static uid_t current_uid = 0;      // 0 = root
        static gid_t current_gid = 0;      // 0 = root group

        // Simple hash function (NOT cryptographically secure - for demo only)
        static uint32_t simple_hash(const char* str) {
            uint32_t hash = 5381;
            int c;
            while ((c = *str++)) {
                hash = ((hash << 5) + hash) + c;
            }
            return hash;
        }

        // Initialize default users and groups
        void init_users() {
            memset(user_database, 0, sizeof(user_database));
            memset(group_database, 0, sizeof(group_database));

            // Root user
            user_database[0].uid = 0;
            user_database[0].gid = 0;
            strncpy(user_database[0].username, "root", 63);
            strncpy(user_database[0].password_hash, "root", 127);  // Password hash for "root"
            strncpy(user_database[0].home_dir, "/root", 255);
            strncpy(user_database[0].shell, "/bin/hcsh", 255);

            // Guest user
            user_database[1].uid = 1000;
            user_database[1].gid = 1000;
            strncpy(user_database[1].username, "guest", 63);
            strncpy(user_database[1].password_hash, "guest", 127);  // Password hash for "guest"
            strncpy(user_database[1].home_dir, "/home/guest", 255);
            strncpy(user_database[1].shell, "/bin/sh", 255);

            user_count = 2;

            // Root group
            group_database[0].gid = 0;
            strncpy(group_database[0].groupname, "root", 63);
            group_database[0].member_count = 1;
            group_database[0].members[0] = 0;

            // Users group
            group_database[1].gid = 1000;
            strncpy(group_database[1].groupname, "users", 63);
            group_database[1].member_count = 1;
            group_database[1].members[0] = 1000;

            group_count = 2;

            log_ok("User system initialized with %d users and %d groups", user_count, group_count);
        }

        uid_t get_uid_by_name(const char* username) {
            if (!username) return (uid_t)-1;
            for (int i = 0; i < user_count; ++i) {
                if (strcmp(user_database[i].username, username) == 0) {
                    return user_database[i].uid;
                }
            }
            return (uid_t)-1;
        }

        const char* get_username_by_uid(uid_t uid) {
            for (int i = 0; i < user_count; ++i) {
                if (user_database[i].uid == uid) {
                    return user_database[i].username;
                }
            }
            return "unknown";
        }

        const User* get_user_by_uid(uid_t uid) {
            for (int i = 0; i < user_count; ++i) {
                if (user_database[i].uid == uid) {
                    return &user_database[i];
                }
            }
            return nullptr;
        }

        const User* get_user_by_name(const char* username) {
            if (!username) return nullptr;
            for (int i = 0; i < user_count; ++i) {
                if (strcmp(user_database[i].username, username) == 0) {
                    return &user_database[i];
                }
            }
            return nullptr;
        }

        gid_t get_gid_by_name(const char* groupname) {
            if (!groupname) return (gid_t)-1;
            for (int i = 0; i < group_count; ++i) {
                if (strcmp(group_database[i].groupname, groupname) == 0) {
                    return group_database[i].gid;
                }
            }
            return (gid_t)-1;
        }

        const char* get_groupname_by_gid(gid_t gid) {
            for (int i = 0; i < group_count; ++i) {
                if (group_database[i].gid == gid) {
                    return group_database[i].groupname;
                }
            }
            return "unknown";
        }

        const Group* get_group_by_gid(gid_t gid) {
            for (int i = 0; i < group_count; ++i) {
                if (group_database[i].gid == gid) {
                    return &group_database[i];
                }
            }
            return nullptr;
        }

        // Simple authentication (NOT secure - for demo purposes)
        bool authenticate_user(const char* username, const char* password) {
            if (!username || !password) return false;
            
            const User* user = get_user_by_name(username);
            if (!user) {
                log_info("User not found: %s", username);
                return false;
            }

            // Simple string comparison (NOT secure - in real system, use proper password hashing)
            bool authenticated = (strcmp(user->password_hash, password) == 0);
            
            if (authenticated) {
                log_ok("User authenticated: %s (uid=%u)", username, user->uid);
            } else {
                log_fail("Authentication failed for user: %s", username);
            }
            
            return authenticated;
        }

        uid_t get_current_uid() {
            return current_uid;
        }

        gid_t get_current_gid() {
            return current_gid;
        }

        void set_current_user(uid_t uid, gid_t gid) {
            current_uid = uid;
            current_gid = gid;
            log_info("Current user context changed to uid=%u, gid=%u", uid, gid);
        }

        // Permission checking (Unix-style)
        bool check_permission(uint16_t mode, uid_t uid, gid_t gid, int required_bits) {
            uid_t file_uid = (mode >> 16) & 0xFFFF;
            gid_t file_gid = (mode >> 8) & 0xFF;
            
            // Root (uid=0) bypasses all permission checks
            if (uid == 0) return true;
            
            // Check owner permissions
            if (uid == file_uid) {
                return (mode & (required_bits << 6)) != 0;
            }
            
            // Check group permissions
            if (gid == file_gid) {
                return (mode & (required_bits << 3)) != 0;
            }
            
            // Check other permissions
            return (mode & required_bits) != 0;
        }

        bool can_read(const char* path, uid_t uid, gid_t gid) {
            // Simplified: allow if uid is 0 or if path exists
            if (uid == 0) return true;
            // TODO: Implement full permission check with VFS metadata
            return true;
        }

        bool can_write(const char* path, uid_t uid, gid_t gid) {
            if (uid == 0) return true;
            // TODO: Implement full permission check with VFS metadata
            return true;
        }

        bool can_execute(const char* path, uid_t uid, gid_t gid) {
            if (uid == 0) return true;
            // TODO: Implement full permission check with VFS metadata
            return true;
        }

    } // namespace users
} // namespace hanacore

// C-linkage wrappers for shell integration
extern "C" {
    uint32_t get_current_user_uid() {
        return hanacore::users::get_current_uid();
    }

    uint32_t get_current_user_gid() {
        return hanacore::users::get_current_gid();
    }

    const char* get_current_username() {
        return hanacore::users::get_username_by_uid(hanacore::users::get_current_uid());
    }

    void set_current_user_c(uint32_t uid, uint32_t gid) {
        hanacore::users::set_current_user(uid, gid);
    }

    bool authenticate_user_c(const char* username, const char* password) {
        return hanacore::users::authenticate_user(username, password);
    }
}
