#include "elf_loader.hpp"
#include "../mem/bump_alloc.hpp"
#include <stdint.h>
#include <stddef.h>

extern "C" int memcmp(const void* s1, const void* s2, size_t n);
extern "C" void* memcpy(void* dst, const void* src, size_t n);
extern "C" void* memset(void* s, int c, size_t n);

// Minimal ELF64 parsing structures (subset)
typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define PT_LOAD 1

static inline int is_valid_elf64(const Elf64_Ehdr* eh) {
    return eh->e_ident[EI_MAG0] == ELFMAG0 && eh->e_ident[EI_MAG1] == ELFMAG1 && eh->e_ident[EI_MAG2] == ELFMAG2 && eh->e_ident[EI_MAG3] == ELFMAG3;
}

void* elf64_load_from_memory(const void* data, size_t size) {
    if (!data || size < sizeof(Elf64_Ehdr)) return NULL;
    const Elf64_Ehdr* eh = (const Elf64_Ehdr*)data;
    if (!is_valid_elf64(eh)) return NULL;
    if (eh->e_phoff == 0 || eh->e_phnum == 0) return NULL;

    // Find min and max vaddr among PT_LOAD
    uint64_t min_vaddr = (uint64_t)-1;
    uint64_t max_vaddr = 0;
    const uint8_t* base = (const uint8_t*)data;
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const Elf64_Phdr* ph = (const Elf64_Phdr*)(base + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_vaddr < min_vaddr) min_vaddr = ph->p_vaddr;
        if (ph->p_vaddr + ph->p_memsz > max_vaddr) max_vaddr = ph->p_vaddr + ph->p_memsz;
    }
    if (min_vaddr == (uint64_t)-1) return NULL;

    uint64_t total = max_vaddr - min_vaddr;
    // Allocate kernel memory for the whole image (page aligned)
    void* mem = bump_alloc_alloc((size_t)total, 0x1000);
    if (!mem) return NULL;
    // Zero entire region
    memset(mem, 0, (size_t)total);

    // Load segments
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const Elf64_Phdr* ph = (const Elf64_Phdr*)(base + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_offset + ph->p_filesz > size) return NULL; // truncated
        uintptr_t dest = (uintptr_t)mem + (ph->p_vaddr - min_vaddr);
        memcpy((void*)dest, base + ph->p_offset, (size_t)ph->p_filesz);
        // remaining bytes are already zero
    }

    // Compute relocated entry
    void* entry = (void*)((uintptr_t)mem + (eh->e_entry - min_vaddr));
    return entry;
}
