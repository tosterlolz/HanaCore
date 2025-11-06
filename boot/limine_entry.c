#include <stdint.h>
#include "../boot/limine.h"

// Limine request markers - MUST be in this order and in .limine_requests section
__attribute__((used, section(".limine_requests_start_marker")))
uint64_t limine_requests_start_marker[4] = {
    0xf03dcc8458224f02, 0x6fe4038153e7e6eb, 0, 0
};

// Base revision - this tells Limine what protocol version we support
__attribute__((used, section(".limine_requests")))
uint64_t limine_base_revision[4] = {
    0xf3dde7e28e7f38f9, 0x29450aabb7e8d74d, 0, 0
};

/* HHDM request: ask Limine for the higher-half direct map offset so the kernel
   can convert physical addresses (like the framebuffer address) into valid
   kernel virtual addresses. */
__attribute__((used, section(".limine_requests")))
volatile struct limine_hhdm_request limine_hhdm_request = LIMINE_HHDM_REQUEST;

// End marker
__attribute__((used, section(".limine_requests_end_marker")))
uint64_t limine_requests_end_marker[2] = {
    0, 0
};
