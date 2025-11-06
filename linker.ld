OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kernel_main)

/*
 * Linker script for HanaCore
 * - Link at higher-half 0xffffffff80000000
 * - Place .limine_requests on its own page (aligned to 4K) so it cannot
 *   share a page with .text/.rodata that have different segment permissions.
 */
PHDRS {
  limine PT_LOAD FLAGS(0x2); /* RW */
  text   PT_LOAD FLAGS(0x5); /* RX */
  rodata PT_LOAD FLAGS(0x4); /* R  */
  data   PT_LOAD FLAGS(0x2); /* RW */
}

SECTIONS
{
  /* Start at higher-half virtual address */
  . = 0xffffffff80000000;

  /* Put limine requests on their own 4K-aligned page at the start */
  .limine_requests ALIGN(4096) : PHDRS(limine) {
    KEEP(*(.limine_requests_start_marker))
    KEEP(*(.limine_requests))
    KEEP(*(.limine_requests_end_marker))
    . = ALIGN(4096);
  }

  /* Text segment - aligned to page */
  .text ALIGN(4096) : PHDRS(text) {
    *(.text*)
  }

  .rodata ALIGN(4096) : PHDRS(rodata) {
    *(.rodata*)
  }

  .data ALIGN(4096) : PHDRS(data) {
    *(.data*)

    __init_array_start = .;
    KEEP(*(.init_array*))
    __init_array_end = .;

    __fini_array_start = .;
    KEEP(*(.fini_array*))
    __fini_array_end = .;
  }

  .bss ALIGN(4096) : {
    *(.bss*)
    *(COMMON)
  }

  /* Discard unneeded sections */
  /DISCARD/ : {
    *(.note.GNU-stack)
    *(.gnu_debuglink)
    *(.gnu_debugaltlink)
  }
}
