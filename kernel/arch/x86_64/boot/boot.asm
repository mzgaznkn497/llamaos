; ==============================================================================
; LlamaOS/A - x86-64 Early Bootstrap and Long Mode Entry
; ==============================================================================
; This module provides the Multiboot2 specification compliant header, receives
; control from the bootloader in 32-bit protected mode, performs CPU feature
; verification (CPUID, 64-bit Long Mode), establishes early 4-level page tables,
; activates IA-32e Long Mode, sets up the 64-bit GDT, and transitions control
; to the higher-half C++ kernel entry point (kernel_main).
; ==============================================================================

[BITS 32]

; Multiboot2 Header Constants
MB2_MAGIC           equ 0xe85250d6
MB2_ARCH_I386       equ 0
MB2_HEADER_LEN      equ (mb2_header_end - mb2_header_start)
MB2_CHECKSUM        equ (0x100000000 - (MB2_MAGIC + MB2_ARCH_I386 + MB2_HEADER_LEN))

section .multiboot_header
align 8
mb2_header_start:
    dd MB2_MAGIC
    dd MB2_ARCH_I386
    dd MB2_HEADER_LEN
    dd MB2_CHECKSUM

    ; Tag: Information Request
    align 8
tag_info_req_start:
    dw 1                        ; Type: Information request
    dw 0                        ; Flags
    dd (tag_info_req_end - tag_info_req_start)
    dd 1                        ; Request command line
    dd 2                        ; Request bootloader name
    dd 4                        ; Request basic memory info
    dd 6                        ; Request BIOS memory map
    dd 8                        ; Request framebuffer info
    dd 14                       ; Request ACPI old RSDP
    dd 15                       ; Request ACPI new RSDP
tag_info_req_end:

    ; Tag: Framebuffer Request (1024x768x32 preferred)
    align 8
tag_fb_start:
    dw 5                        ; Type: Framebuffer
    dw 1                        ; Flags: Optional
    dd (tag_fb_end - tag_fb_start)
    dd 1024                     ; Width
    dd 768                      ; Height
    dd 32                       ; Bits per pixel
tag_fb_end:

    ; Tag: Console Flags (Request console support)
    align 8
tag_console_start:
    dw 4                        ; Type: Console flags
    dw 1                        ; Flags: Optional
    dd (tag_console_end - tag_console_start)
    dd 3                        ; Console flags: EGA text support
tag_console_end:

    ; Tag: End of Multiboot2 header
    align 8
    dw 0                        ; Type: End tag
    dw 0                        ; Flags
    dd 8                        ; Size: 8 bytes
mb2_header_end:

; ------------------------------------------------------------------------------
; Early 32-Bit Bootstrap Entry Point
; ------------------------------------------------------------------------------
section .boot.text
global _start
extern kernel_main

_start:
    ; Disable interrupts immediately
    cli
    cld

    ; Setup temporary 32-bit stack
    mov esp, early_stack_top

    ; Multiboot2 verification: eax must contain 0x36d76289
    cmp eax, 0x36d76289
    jne .error_no_multiboot

    ; Preserve Multiboot2 information pointer (ebx)
    mov [boot_mb2_magic], eax
    mov [boot_mb2_info_ptr], ebx

    ; Step 1: Verify CPUID presence via EFLAGS bit 21 (ID bit)
    pushfd
    pop eax
    mov ecx, eax
    xor eax, (1 << 21)
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je .error_no_cpuid

    ; Step 2: Verify Long Mode (x86-64) support via CPUID extended functions
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .error_no_long_mode

    mov eax, 0x80000001
    cpuid
    test edx, (1 << 29)         ; Bit 29: Long Mode (LM)
    jz .error_no_long_mode

    ; Step 3: Enable SSE / FPU support for freestanding C++ ABI
    mov eax, cr0
    and ax, 0xFFFB              ; Clear EM (bit 2)
    or ax, 0x0002               ; Set MP (bit 1)
    mov cr0, eax

    mov eax, cr4
    or eax, (1 << 9) | (1 << 10) ; Set OSFXSR (bit 9) and OSXMMEXCPT (bit 10)
    mov cr4, eax

    ; Step 4: Populate early page tables
    call setup_early_page_tables

    ; Step 5: Enable PAE (Physical Address Extension) in CR4
    mov eax, cr4
    or eax, (1 << 5)            ; PAE bit
    mov cr4, eax

    ; Step 6: Load CR3 with early PML4 physical address
    mov eax, early_pml4
    mov cr3, eax

    ; Step 7: Enable Long Mode (LME) and No-Execute (NXE) in IA32_EFER MSR (0xC0000080)
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8) | (1 << 11) ; Bit 8: LME, Bit 11: NXE
    wrmsr

    ; Step 8: Enable Paging (PG) and Protection (PE) in CR0
    mov eax, cr0
    or eax, (1 << 31) | (1 << 0) ; PG (bit 31) | PE (bit 0)
    mov cr0, eax

    ; Step 9: Load 64-bit Global Descriptor Table
    lgdt [early_gdt_desc]

    ; Step 10: Far return transition into 64-bit Long Mode
    push dword 0x08             ; 64-bit Code Segment Selector
    push dword long_mode_entry   ; 32-bit address of entry
    retf

.error_no_multiboot:
    mov esi, msg_err_mb2
    jmp .early_halt

.error_no_cpuid:
    mov esi, msg_err_cpuid
    jmp .early_halt

.error_no_long_mode:
    mov esi, msg_err_lm
    jmp .early_halt

.early_halt:
    ; Output error string to COM1 (0x3F8) and QEMU debug port (0xE9)
    mov dx, 0x3F8
.print_loop:
    lodsb
    test al, al
    jz .halt_forever
    out dx, al
    out 0xE9, al
    jmp .print_loop
.halt_forever:
    cli
.hlt_loop:
    hlt
    jmp .hlt_loop

; ------------------------------------------------------------------------------
; Early Page Table Initialization (32-bit protected mode)
; ------------------------------------------------------------------------------
setup_early_page_tables:
    ; Zero out PML4, PDPTs, and PDs
    mov edi, early_pml4
    mov ecx, (5 * 4096) / 4
    xor eax, eax
    rep stosd

    ; Link PML4[0] -> early_pdpt_identity (Identity mapping for transition)
    mov eax, early_pdpt_identity
    or eax, 0x03                ; Present | Writable
    mov [early_pml4 + 0], eax

    ; Link PML4[511] -> early_pdpt_kernel (Higher-half top 512 GiB)
    mov eax, early_pdpt_kernel
    or eax, 0x03
    mov [early_pml4 + 511 * 8], eax

    ; Link early_pdpt_identity[0] -> early_pd_identity
    mov eax, early_pd_identity
    or eax, 0x03
    mov [early_pdpt_identity + 0], eax

    ; Link early_pdpt_kernel[510] -> early_pd_kernel (-2 GiB: 0xFFFFFFFF80000000)
    mov eax, early_pd_kernel
    or eax, 0x03
    mov [early_pdpt_kernel + 510 * 8], eax

    ; Link early_pdpt_kernel[511] -> early_pd_kernel2 (-1 GiB: 0xFFFFFFFFC0000000)
    mov eax, early_pd_kernel2
    or eax, 0x03
    mov [early_pdpt_kernel + 511 * 8], eax

    ; Populate early_pd_identity: 512 x 2 MiB large pages (0 .. 1 GiB identity)
    mov ecx, 0
.loop_pd_identity:
    mov eax, 0x200000           ; 2 MiB
    mul ecx
    or eax, 0x83                ; Present | Writable | PageSize (2MB)
    mov [early_pd_identity + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .loop_pd_identity

    ; Populate early_pd_kernel: 512 x 2 MiB large pages (0 .. 1 GiB -> -2 GiB)
    mov ecx, 0
.loop_pd_kernel:
    mov eax, 0x200000
    mul ecx
    or eax, 0x83                ; Present | Writable | PageSize (2MB)
    mov [early_pd_kernel + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .loop_pd_kernel

    ; Populate early_pd_kernel2: 512 x 2 MiB large pages (1 GiB .. 2 GiB -> -1 GiB)
    mov ecx, 0
.loop_pd_kernel2:
    mov eax, 0x200000
    mul ecx
    add eax, 0x40000000         ; Base 1 GiB physical
    or eax, 0x83                ; Present | Writable | PageSize (2MB)
    mov [early_pd_kernel2 + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .loop_pd_kernel2

    ret

; ------------------------------------------------------------------------------
; 64-Bit Long Mode Transition
; ------------------------------------------------------------------------------
[BITS 64]
long_mode_entry:
    ; Reload segment registers with 64-bit Data Segment selector (0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Switch to higher-half code execution
    mov rax, higher_half_entry
    jmp rax

higher_half_entry:
    ; Load 64-bit kernel stack located in higher-half BSS
    mov rsp, kernel_stack_top

    ; Clear RFLAGS
    push 0
    popfq

    ; Pass Multiboot2 boot arguments according to System V AMD64 ABI:
    ; RDI: uint64_t magic
    ; RSI: uint64_t multiboot_info_virtual_address
    mov edi, [boot_mb2_magic]
    mov esi, [boot_mb2_info_ptr]
    ; Convert physical info pointer to higher-half virtual address
    mov rax, 0xFFFFFFFF80000000
    add rsi, rax

    ; Enter C++ Kernel Main
    call kernel_main

    ; If kernel_main returns, halt deterministically
    cli
.kernel_halt:
    hlt
    jmp .kernel_halt

; ------------------------------------------------------------------------------
; Early Data and Tables (Physical Memory)
; ------------------------------------------------------------------------------
section .boot.rodata
align 8
early_gdt_start:
    ; Null Descriptor (0x00)
    dq 0x0000000000000000
    ; 64-Bit Kernel Code Descriptor (0x08): DPL=0, L=1, D=0, P=1, Code/Read
    dq 0x00AF9A000000FFFF
    ; 64-Bit Kernel Data Descriptor (0x10): DPL=0, P=1, Data/Write
    dq 0x00AF92000000FFFF
early_gdt_end:

early_gdt_desc:
    dw (early_gdt_end - early_gdt_start - 1)
    dd early_gdt_start
    dd 0

msg_err_mb2:   db "FATAL: LlamaOS/A bootloader handshake failed (Multiboot2 magic missing).", 0x0D, 0x0A, 0
msg_err_cpuid: db "FATAL: CPUID instruction not supported by host CPU.", 0x0D, 0x0A, 0
msg_err_lm:    db "FATAL: x86-64 Long Mode not supported by host CPU.", 0x0D, 0x0A, 0

section .boot.data
global boot_mb2_magic
global boot_mb2_info_ptr
boot_mb2_magic:     dd 0
boot_mb2_info_ptr:   dd 0

; ------------------------------------------------------------------------------
; Early Page Tables (4096-byte aligned in physical memory)
; ------------------------------------------------------------------------------
section .boot.bss nobits alloc write
align 4096
early_pml4:
    resb 4096
early_pdpt_identity:
    resb 4096
early_pdpt_kernel:
    resb 4096
early_pd_identity:
    resb 4096
early_pd_kernel:
    resb 4096
early_pd_kernel2:
    resb 4096

align 16
early_stack_bottom:
    resb 4096
early_stack_top:

; ------------------------------------------------------------------------------
; Higher-Half Kernel Stack (Linked in Higher-Half .bss)
; ------------------------------------------------------------------------------
section .bss
align 16
global kernel_stack_bottom
global kernel_stack_top
kernel_stack_bottom:
    resb 65536                  ; 64 KiB kernel stack
kernel_stack_top:

; Enforce non-executable stack policy
section .note.GNU-stack noalloc noexec nowrite progbits

