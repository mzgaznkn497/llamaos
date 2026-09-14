; ==============================================================================
; LlamaOS/A - Low-Level Interrupt & Exception Assembly Stubs (x86-64)
; ==============================================================================
; Differentiates between CPU exceptions that push an error code and those that
; do not. Normalizes all frames into a uniform 176-byte C++ InterruptFrame layout,
; preserves complete register state, aligns the stack for the System V AMD64 ABI,
; and invokes the central C++ exception_dispatch routine.
; ==============================================================================

[BITS 64]
section .text

extern exception_dispatch

%macro ISR_NO_ERRCODE 1
global isr_stub_%1
isr_stub_%1:
    push qword 0                ; Push dummy error code
    push qword %1               ; Push vector number
    jmp common_isr_stub
%endmacro

%macro ISR_ERRCODE 1
global isr_stub_%1
isr_stub_%1:
    ; CPU already pushed error code
    push qword %1               ; Push vector number
    jmp common_isr_stub
%endmacro

; ------------------------------------------------------------------------------
; Exception Vector Entry Points (0 .. 31)
; ------------------------------------------------------------------------------
ISR_NO_ERRCODE 0                ; #DE: Divide-by-Zero
ISR_NO_ERRCODE 1                ; #DB: Debug Exception
ISR_NO_ERRCODE 2                ; #NMI: Non-Maskable Interrupt
ISR_NO_ERRCODE 3                ; #BP: Breakpoint (INT3)
ISR_NO_ERRCODE 4                ; #OF: Overflow
ISR_NO_ERRCODE 5                ; #BR: Bound Range Exceeded
ISR_NO_ERRCODE 6                ; #UD: Invalid Opcode
ISR_NO_ERRCODE 7                ; #NM: Device Not Available
ISR_ERRCODE    8                ; #DF: Double Fault
ISR_ERRCODE    10               ; #TS: Invalid TSS
ISR_ERRCODE    11               ; #NP: Segment Not Present
ISR_ERRCODE    12               ; #SS: Stack-Segment Fault
ISR_ERRCODE    13               ; #GP: General Protection Fault
ISR_ERRCODE    14               ; #PF: Page Fault
ISR_NO_ERRCODE 16               ; #MF: x87 Floating-Point Exception
ISR_ERRCODE    17               ; #AC: Alignment Check
ISR_NO_ERRCODE 18               ; #MC: Machine Check
ISR_NO_ERRCODE 19               ; #XM: SIMD Floating-Point Exception
ISR_NO_ERRCODE 20               ; #VE: Virtualization Exception
ISR_ERRCODE    21               ; #CP: Control Protection Exception

; ------------------------------------------------------------------------------
; Hardware IRQ and Spurious Vectors
; ------------------------------------------------------------------------------
ISR_NO_ERRCODE 32               ; IRQ0: Timer Interrupt
ISR_NO_ERRCODE 33               ; IRQ1: Keyboard Interrupt
ISR_NO_ERRCODE 39               ; IRQ7: Master PIC Spurious Interrupt
ISR_NO_ERRCODE 47               ; IRQ15: Slave PIC Spurious Interrupt
ISR_NO_ERRCODE 255              ; Spurious APIC Interrupt

; ------------------------------------------------------------------------------
; Common ISR Dispatch Stub
; ------------------------------------------------------------------------------
common_isr_stub:
    ; Save all 15 General Purpose Registers
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax

    ; Set first argument (RDI) to point to base of InterruptFrame on stack
    mov rdi, rsp

    ; System V AMD64 ABI: Clear direction flag
    cld

    ; Call the central C++ exception and interrupt dispatcher
    call exception_dispatch

    ; Restore all 15 General Purpose Registers in reverse order
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    ; Remove vector number and error code from stack
    add rsp, 16

    ; Return to interrupted context
    iretq
