#include "elf_loader.hpp"
#include "../mem/bump_alloc.hpp"
#include <stdint.h>
#include <stddef.h>

extern "C" int memcmp(const void* s1, const void* s2, size_t n);
extern "C" void* memcpy(void* dst, const void* src, size_t n);
extern "C" void* memset(void* s, int c, size_t n);

struct Elf64_Ehdr {
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
};

struct Elf64_Phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define PT_LOAD 1

static inline bool is_valid_elf64(const Elf64_Ehdr* eh) {
    return eh->e_ident[EI_MAG0] == ELFMAG0 &&
           eh->e_ident[EI_MAG1] == ELFMAG1 &&
           eh->e_ident[EI_MAG2] == ELFMAG2 &&
           eh->e_ident[EI_MAG3] == ELFMAG3;
}

void* elf64_load_from_memory(const void* data, size_t size) {
    if (!data || size < sizeof(Elf64_Ehdr))
        return nullptr;

    const auto* eh = reinterpret_cast<const Elf64_Ehdr*>(data);
    if (!is_valid_elf64(eh))
        return nullptr;

    if (eh->e_phoff == 0 || eh->e_phnum == 0)
        return nullptr;

    // Ensure program header table fits within blob
    const size_t ph_table_end = eh->e_phoff + (size_t)eh->e_phnum * eh->e_phentsize;
    if (ph_table_end > size)
        return nullptr;

    const uint8_t* base = static_cast<const uint8_t*>(data);

    uint64_t min_vaddr = UINT64_MAX;
    uint64_t max_vaddr = 0;

    // Calculate memory span of all PT_LOAD segments
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const auto* ph = reinterpret_cast<const Elf64_Phdr*>(base + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD)
            continue;

        if (ph->p_vaddr < min_vaddr)
            min_vaddr = ph->p_vaddr;
        if (ph->p_vaddr + ph->p_memsz > max_vaddr)
            max_vaddr = ph->p_vaddr + ph->p_memsz;
    }

    if (min_vaddr == UINT64_MAX || max_vaddr <= min_vaddr)
        return nullptr;

    const uint64_t total_size = max_vaddr - min_vaddr;

    // Avoid absurd allocations
    constexpr uint64_t MAX_USER_IMAGE = 64ull * 1024ull * 1024ull;
    if (total_size == 0 || total_size > MAX_USER_IMAGE)
        return nullptr;

    // Allocate aligned region
    void* mem = bump_alloc_alloc((size_t)total_size, 0x1000);
    if (!mem)
        return nullptr;

    memset(mem, 0, (size_t)total_size);

    // Load all PT_LOAD segments
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const auto* ph = reinterpret_cast<const Elf64_Phdr*>(base + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD)
            continue;

        if (ph->p_offset + ph->p_filesz > size)
            return nullptr;

        uintptr_t dest = reinterpret_cast<uintptr_t>(mem) + (ph->p_vaddr - min_vaddr);

        if ((dest + ph->p_filesz) > (uintptr_t)mem + total_size)
            return nullptr;

        memcpy(reinterpret_cast<void*>(dest), base + ph->p_offset, (size_t)ph->p_filesz);
        // rest is already zeroed
    }

    // Compute relocated entry point
    if (eh->e_entry < min_vaddr || eh->e_entry >= max_vaddr)
        return nullptr;

    void* entry = reinterpret_cast<void*>((uintptr_t)mem + (eh->e_entry - min_vaddr));

    if ((uintptr_t)entry < (uintptr_t)mem || (uintptr_t)entry >= (uintptr_t)mem + total_size)
        return nullptr;

    return entry;
}
