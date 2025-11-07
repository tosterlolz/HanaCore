// Simple in-kernel PTY (pseudo-terminal) implementation.
#pragma once
#include <stddef.h>

// Create up to this many PTY pairs
#define HANACORE_PTY_MAX 16

extern "C" {
    // Create a new PTY pair. Returns index (0..HANACORE_PTY_MAX-1) or -1 on failure.
    int pty_create_pair(void);

    // Master-side operations: master reads data written by slave, and writes data
    // which the slave can read.
    int pty_master_read(int id, void* buf, size_t len);
    int pty_master_write(int id, const void* buf, size_t len);

    // Slave-side operations: slave reads data written by master, and writes data
    // which the master can read.
    int pty_slave_read(int id, void* buf, size_t len);
    int pty_slave_write(int id, const void* buf, size_t len);

    // Push input keystroke into slave input buffer (used by keyboard/TTY).
    void pty_slave_push_input(int id, char c);

    // Attach a slave to a VT index (0-based). When VT active, keyboard input
    // will be pushed to the attached slave. Pass -1 to detach.
    int pty_attach_slave_to_vt(int id, int vt_index);

    // Query mapping from VT index to pty id (-1 if none)
    int pty_vt_map_get(int vt_index);
}
