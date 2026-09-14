; =============================================================================
; LlamaOS/A - Phase 7 Low-Level Ring 0 -> Ring 3 Entry Assembly
; =============================================================================
; Performs the architectural hardware transition from supervisor mode (Ring 0)
; to user mode (Ring 3) via iretq.
; Establishes User Code Segment (0x33), User Data Segment (0x2B), canonical
; User RIP and RSP, and zeroes all general-purpose registers to eliminate
; kernel information leakage.
; =============================================================================

[bits 64]
default rel

global enter_ring3

section .text
align 16
; void enter_ring3(uint64_t rip, uint64_t rsp);
; SysV AMD64 ABI:
;   RDI = target user RIP
;   RSI = target user RSP
enter_ring3:
    ; Guarantee atomic privilege transition: disable interrupts while constructing frame
    cli

    ; 1. Construct architectural iretq frame on the active kernel stack:
    ;    [rsp + 32] SS     <- 0x002B (User Data Selector: 0x28 | RPL 3)
    ;    [rsp + 24] RSP    <- User Stack Pointer (RSI)
    ;    [rsp + 16] RFLAGS <- 0x00000202 (IF=1, Bit 1 reserved=1)
    ;    [rsp + 08] CS     <- 0x0033 (User Code Selector: 0x30 | RPL 3)
    ;    [rsp + 00] RIP    <- User Entry Point (RDI)
    push qword 0x2B             ; SS = User Data Segment (DPL 3)
    push rsi                    ; RSP = User Stack Top
    push qword 0x202            ; RFLAGS: Interrupts Enabled (IF=1)
    push qword 0x33             ; CS = User Code Segment (DPL 3)
    push rdi                    ; RIP = User Entry Address

    ; 2. Reload data segment registers with User Data Selector (0x2B)
    mov ax, 0x2B
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; 3. Zero out all general-purpose registers to eliminate kernel data leaks
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor rbp, rbp
    xor r8,  r8
    xor r9,  r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15

    ; 4. Execute iretq: Hardware pops RIP, CS (0x33), RFLAGS, RSP, SS (0x2B)
    ; Drops CPU privilege level from Ring 0 to Ring 3 (CPL = 3)
    iretq
