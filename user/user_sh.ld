/* LlamaOS/A Userland Executable Linker Script for /bin/sh
 * Targets lower-half canonical user virtual address space at 0x600000 (6 MiB)
 */

ENTRY(_start)

SECTIONS
{
    . = 0x0000000000600000;

    .text : ALIGN(4K)
    {
        *(.text.entry)
        *(.text .text.*)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(4K)
    {
        *(.data .data.*)
    }

    .bss : ALIGN(4K)
    {
        *(.bss .bss.*)
        *(COMMON)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame)
    }
}
