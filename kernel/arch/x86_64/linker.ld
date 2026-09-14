/* LlamaOS/A Kernel Linker Script for x86-64 Architecture
 * Targets higher-half virtual address space (-2 GiB / 0xFFFFFFFF80000000)
 * with physical load address at 1 MiB (0x00100000).
 */

ENTRY(_start)

KERNEL_PHYSICAL_BASE = 0x00100000;
KERNEL_VIRTUAL_BASE  = 0xFFFFFFFF80000000;

SECTIONS
{
    . = KERNEL_PHYSICAL_BASE;
    _kernel_physical_start = .;

    /* Early boot section: Loaded and linked at physical memory */
    .boot : ALIGN(4K)
    {
        KEEP(*(.multiboot_header))
        *(.boot.text)
        *(.boot.rodata)
        *(.boot.data)
        *(.boot.bss)
    }

    /* Transition location counter to higher-half virtual space */
    . += KERNEL_VIRTUAL_BASE;
    _kernel_virtual_start = .;

    /* Kernel code */
    .text : AT(ADDR(.text) - KERNEL_VIRTUAL_BASE) ALIGN(4K)
    {
        _text_start = .;
        *(.text .text.*)
        _text_end = .;
    }

    /* Read-only data */
    .rodata : AT(ADDR(.rodata) - KERNEL_VIRTUAL_BASE) ALIGN(4K)
    {
        _rodata_start = .;
        *(.rodata .rodata.*)
        . = ALIGN(8);
        _init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array .ctors))
        _init_array_end = .;
        _rodata_end = .;
    }

    /* Initialized data */
    .data : AT(ADDR(.data) - KERNEL_VIRTUAL_BASE) ALIGN(4K)
    {
        _data_start = .;
        *(.data .data.*)
        _data_end = .;
    }

    /* Uninitialized data (BSS) */
    .bss : AT(ADDR(.bss) - KERNEL_VIRTUAL_BASE) ALIGN(4K)
    {
        _bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        _bss_end = .;
    }

    . = ALIGN(4K);
    _kernel_virtual_end = .;
    _kernel_physical_end = . - KERNEL_VIRTUAL_BASE;

    /* Discard comments and note sections to ensure clean binary */
    /DISCARD/ :
    {
        *(.comment)
        *(.note.gnu.build-id)
        *(.eh_frame)
    }
}
