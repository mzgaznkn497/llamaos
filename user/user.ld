/* LlamaOS/A Userland Executable Linker Script
 * Targets lower-half canonical user virtual address space at 0x400000 (4 MiB)
 */

ENTRY(_start)

SECTIONS
{
    . = 0x0000000000400000;

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
