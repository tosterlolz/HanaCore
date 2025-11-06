OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kernel_main)

SECTIONS
{
  . = 0xffffffff80000000;
  
  /* Limine requests MUST be first in the ELF, in the lowest memory addresses */
  .limine_requests : ALIGN(8) {
    KEEP(*(.limine_requests_start_marker))
    KEEP(*(.limine_requests))
    KEEP(*(.limine_requests_end_marker))
  }
  
  .text : ALIGN(4096) {
    *(.text*)
  }
  
  .rodata : ALIGN(4096) {
    *(.rodata*)
  }
  
  .data : ALIGN(4096) {
    *(.data*)
    
    __init_array_start = .;
    KEEP(*(.init_array*))
    __init_array_end = .;
    
    __fini_array_start = .;
    KEEP(*(.fini_array*))
    __fini_array_end = .;
  }
  
  .bss : ALIGN(4096) {
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
