// Simple PTY implementation: ring buffers for master and slave sides.
#include "pty.hpp"
#include <stddef.h>
#include <string.h>
#include "tty.hpp"

// Simple ring buffer helper
struct rbuf {
    char* buf;
    size_t cap;
    size_t head; // next write
    size_t tail; // next read
};

static inline void rbuf_init(rbuf* r, char* area, size_t cap) {
    r->buf = area; r->cap = cap; r->head = r->tail = 0;
}

static inline size_t rbuf_available(const rbuf* r) {
    return r->head - r->tail;
}

static inline size_t rbuf_capacity(const rbuf* r) {
    return r->cap;
}

static inline size_t rbuf_write(rbuf* r, const char* src, size_t n) {
    size_t written = 0;
    while (written < n && (r->head - r->tail) < r->cap) {
        r->buf[r->head & (r->cap - 1)] = src[written++];
        r->head++;
    }
    return written;
}

static inline size_t rbuf_read(rbuf* r, char* dst, size_t n) {
    size_t read = 0;
    while (read < n && r->tail < r->head) {
        dst[read++] = r->buf[r->tail & (r->cap - 1)];
        r->tail++;
    }
    return read;
}

// PTY internal struct
struct pty_pair {
    bool in_use;
    // buffers are power-of-two sized
    static const size_t BUFSZ = 1024;
    char m2s_area[BUFSZ]; // master-to-slave
    char s2m_area[BUFSZ]; // slave-to-master
    rbuf m2s;
    rbuf s2m;
    int attached_vt; // -1 if none
};

static pty_pair ptys[HANACORE_PTY_MAX];
// Map VT index to attached PTY id (-1 if none). Size equals 12
// Map VT index to attached PTY id (-1 if none). Size equals 12
// Initialize all entries to -1 explicitly (constructor functions are
// unreliable in this freestanding kernel environment).
static int pty_vt_map[12] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
// Map VT index to attached PTY id (-1 if none). Size equals TTY_NUM_VT from tty.cpp

// Getter used by tty to find PTY attached to a VT
extern "C" int pty_vt_map_get(int vt_index) {
    if (vt_index < 0 || vt_index >= 12) return -1;
    return pty_vt_map[vt_index];
}

extern "C" int pty_create_pair(void) {
    for (int i = 0; i < HANACORE_PTY_MAX; ++i) {
        if (!ptys[i].in_use) {
            ptys[i].in_use = true;
            rbuf_init(&ptys[i].m2s, ptys[i].m2s_area, pty_pair::BUFSZ);
            rbuf_init(&ptys[i].s2m, ptys[i].s2m_area, pty_pair::BUFSZ);
            ptys[i].attached_vt = -1;
            return i;
        }
    }
    return -1;
}

extern "C" int pty_master_read(int id, void* buf, size_t len) {
    if (id < 0 || id >= HANACORE_PTY_MAX) return -1;
    if (!ptys[id].in_use) return -1;
    return (int)rbuf_read(&ptys[id].s2m, (char*)buf, len);
}

extern "C" int pty_master_write(int id, const void* buf, size_t len) {
    if (id < 0 || id >= HANACORE_PTY_MAX) return -1;
    if (!ptys[id].in_use) return -1;
    return (int)rbuf_write(&ptys[id].m2s, (const char*)buf, len);
}

extern "C" int pty_slave_read(int id, void* buf, size_t len) {
    if (id < 0 || id >= HANACORE_PTY_MAX) return -1;
    if (!ptys[id].in_use) return -1;
    return (int)rbuf_read(&ptys[id].m2s, (char*)buf, len);
}

extern "C" int pty_slave_write(int id, const void* buf, size_t len) {
    if (id < 0 || id >= HANACORE_PTY_MAX) return -1;
    if (!ptys[id].in_use) return -1;
    return (int)rbuf_write(&ptys[id].s2m, (const char*)buf, len);
}

extern "C" void pty_slave_push_input(int id, char c) {
    if (id < 0 || id >= HANACORE_PTY_MAX) return;
    if (!ptys[id].in_use) return;
    // push single char into slave-readable buffer (m2s)
    char ch = c;
    rbuf_write(&ptys[id].m2s, &ch, 1);
}

extern "C" int pty_attach_slave_to_vt(int id, int vt_index) {
    if (id < 0 || id >= HANACORE_PTY_MAX) return -1;
    if (!ptys[id].in_use) return -1;
    if (vt_index < -1) return -1;
    ptys[id].attached_vt = vt_index;
    // update mapping table
    if (vt_index >= 0 && vt_index < 12) pty_vt_map[vt_index] = id;
    // detach from all VTs if vt_index == -1
    if (vt_index == -1) {
        for (int i = 0; i < 12; ++i) if (pty_vt_map[i] == id) pty_vt_map[i] = -1;
    }
    return 0;
}
