#include "heap.hpp"
#include "bump_alloc.hpp"
#include <stdint.h>

// Very small, single-threaded free-list heap for kernel use.
// Not re-entrant or SMP-safe. Intended as a simple first-pass heap.

struct FreeBlock {
    size_t size; // total size of this block including header
    FreeBlock *next;
};

static FreeBlock *free_list = nullptr;
static void *heap_start = nullptr;
static size_t heap_size = 0;

static inline size_t align_up(size_t v, size_t a) {
    return (v + (a - 1)) & ~(a - 1);
}

void heap_init(size_t size) {
    if (heap_start) return; // already initialized
    // allocate from bump allocator, page-align
    size_t alloc_size = align_up(size, 0x1000);
    void *mem = bump_alloc_alloc(alloc_size, 0x1000);
    if (!mem) return; // can't do much

    heap_start = mem;
    heap_size = alloc_size;

    // single free block spans the whole heap
    free_list = (FreeBlock *)heap_start;
    free_list->size = heap_size;
    free_list->next = nullptr;
}

void *kmalloc(size_t size) {
    if (!free_list) return nullptr;
    if (size == 0) return nullptr;

    const size_t align = 16;
    size_t payload = align_up(size, align);
    const size_t header = align_up(sizeof(FreeBlock), align);
    size_t total = payload + header;

    FreeBlock *prev = nullptr;
    FreeBlock *cur = free_list;

    while (cur) {
        if (cur->size >= total) {
            // found a block
            if (cur->size >= total + (header + 16)) {
                // split
                FreeBlock *next = (FreeBlock *)((uint8_t *)cur + total);
                next->size = cur->size - total;
                next->next = cur->next;
                cur->size = total;
                if (prev) prev->next = next; else free_list = next;
            } else {
                // use entire block
                if (prev) prev->next = cur->next; else free_list = cur->next;
            }
            // return payload pointer after header
            void *payload_ptr = (void *)((uint8_t *)cur + header);
            return payload_ptr;
        }
        prev = cur;
        cur = cur->next;
    }
    return nullptr; // out of memory
}

void kfree(void *ptr) {
    if (!ptr) return;
    const size_t align = 16;
    const size_t header = align_up(sizeof(FreeBlock), align);
    FreeBlock *blk = (FreeBlock *)((uint8_t *)ptr - header);

    // insert sorted by address
    FreeBlock *prev = nullptr;
    FreeBlock *cur = free_list;
    while (cur && cur < blk) {
        prev = cur;
        cur = cur->next;
    }

    // try coalescing with previous
    if (prev) {
        uint8_t *prev_end = (uint8_t *)prev + prev->size;
        if (prev_end == (uint8_t *)blk) {
            // merge into prev
            prev->size += blk->size;
            blk = prev;
        } else {
            // link after prev
            prev->next = blk;
        }
    } else {
        // insert at head
        free_list = blk;
    }

    // try coalescing with next (cur)
    if (cur) {
        uint8_t *blk_end = (uint8_t *)blk + blk->size;
        if (blk_end == (uint8_t *)cur) {
            blk->size += cur->size;
            blk->next = cur->next;
        } else {
            blk->next = cur;
        }
    } else {
        blk->next = nullptr;
    }
}
