OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kernel_main)

SECTIONS
{
  /* Start at higher-half virtual address */
  . = 0xffffffff80000000;

  /* Put limine requests on their own 4K-aligned page at the start */
  .limine_requests ALIGN(4096) : {
    KEEP(*(.limine_requests_start_marker))
    KEEP(*(.limine_requests))
    KEEP(*(.limine_requests_end_marker))
    . = ALIGN(4096);
  }

  /* Text segment - aligned to page */
  .text ALIGN(4096) : {
    *(.text*)
  }

  .rodata ALIGN(4096) : {
    *(.rodata*)
  }

  .data ALIGN(4096) : {
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
