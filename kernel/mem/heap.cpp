#include "heap.hpp"
#include "bump_alloc.hpp"
#include "pma.hpp"
#include "vmm.hpp"
#include "../utils/logger.hpp"
#include <stdint.h>

// Very small, single-threaded free-list heap for kernel use.
// Not re-entrant or SMP-safe. Intended as a simple first-pass heap.

namespace hanacore::utils {
    extern void log_hex64(const char *label, uint64_t value);
    extern void log_fail(const char *msg);
}
namespace hanacore { namespace mem {
    // Temporary static heap buffer to avoid relying on bump allocator
    // during early boot. This ensures the heap memory is already mapped
    // as part of the kernel image and writable.
    static uint8_t static_heap[1024 * 1024]; // 1 MiB static fallback
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

    // Grow the heap by allocating `pages` pages from PMA and (optionally)
    // mapping them with VMM. Current PMA returns a virtual pointer (bump-backed),
    // and VMM mapping is a no-op; we still call vmm_map_range to keep the
    // contract consistent for later replacement with a real mapper.
    static bool heap_grow_pages(size_t pages) {
        if (pages == 0) return false;
        void *blk = pma_alloc_pages(pages);
        if (!blk) {
            hanacore::utils::log_fail_cpp("heap: pma_alloc_pages failed");
            return false;
        }
        size_t grow_size = pages * 0x1000;
        // Call vmm_map_range for the mapping contract. For now this is a noop
        // but when vmm_map_range is implemented this will ensure the physical
        // memory is mapped into the chosen virtual address.
        int r = vmm_map_range(blk, blk, grow_size, 0);
        if (r != 0) {
            hanacore::utils::log_fail_cpp("heap: vmm_map_range failed: %d", r);
            // We won't free the pages (pma_free_pages is noop for bump),
            // but mark this as a failure so caller can abort.
            return false;
        }

        // Prepend new block to free list
        FreeBlock *newblk = (FreeBlock *)blk;
        newblk->size = grow_size;
        // Keep existing free_list linkage
        newblk->next = free_list;
        free_list = newblk;
        heap_size += grow_size;
        hanacore::utils::log_hex64_cpp("heap: grew, new block", (uint64_t)(uintptr_t)newblk);
        hanacore::utils::log_hex64_cpp("heap: grew, size", (uint64_t)grow_size);
        return true;
    }

    void heap_init(size_t size) {
        if (heap_start) return; // already initialized
        size_t alloc_size = align_up(size, 0x1000);
        if (alloc_size > sizeof(static_heap)) alloc_size = sizeof(static_heap);
        void *mem = static_heap;
        heap_start = mem;
        heap_size = alloc_size;
        hanacore::utils::log_hex64("heap: using static heap start", (uint64_t)heap_start);
        hanacore::utils::log_hex64("heap: using static heap size", (uint64_t)heap_size);

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

        // Try up to one grow attempt if allocation fails.
        for (int attempt = 0; attempt < 2; ++attempt) {
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

            // no block found: try to grow heap once on first attempt
            if (attempt == 0) {
                size_t pages_needed = align_up(total, 0x1000) / 0x1000;
                if (pages_needed == 0) pages_needed = 1;
                // try to grow by at least pages_needed, but prefer a small batch to
                // reduce PMA calls (grow by pages_needed or 4 pages whichever is larger)
                size_t grow_pages = pages_needed;
                if (grow_pages < 4) grow_pages = 4;
                bool grew = heap_grow_pages(grow_pages);
                if (!grew) return nullptr;
                // on success, loop and try allocation again
                continue;
            }
            // second attempt failed as well
            return nullptr;
        }
        return nullptr; // unreachable, but keep signature
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
}} // namespace hanacore::mem