; ==============================================================================
; LlamaOS/A - GDT Segment Reload and Task Register Helpers (x86-64)
; ==============================================================================

[BITS 64]
section .text

global reload_segments
reload_segments:
    ; RDI: 64-bit Code Segment Selector (0x08)
    ; RSI: 64-bit Data Segment Selector (0x10)
    push rdi
    lea rax, [rel .reload_cs]
    push rax
    retfq

.reload_cs:
    mov ds, esi
    mov es, esi
    mov ss, esi
    mov fs, esi
    mov gs, esi
    ret

global load_task_register
load_task_register:
    ; RDI: TSS Selector (0x18)
    ltr di
    ret

global read_task_register
read_task_register:
    xor eax, eax
    str ax
    ret

global read_cs
read_cs:
    xor eax, eax
    mov ax, cs
    ret

global read_ds
read_ds:
    xor eax, eax
    mov ax, ds
    ret

global read_ss
read_ss:
    xor eax, eax
    mov ax, ss
    ret
