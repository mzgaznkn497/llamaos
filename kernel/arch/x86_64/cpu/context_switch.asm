; ==============================================================================
; LlamaOS/A - Low-Level CPU Context Switch & Thread Bootstrap (x86-64)
; ==============================================================================
; Implements deterministic context switching for System V AMD64 ABI callee-saved
; registers and the initial execution trampoline for newly created kernel threads.
; ==============================================================================

[BITS 64]
section .text

global context_switch
; void context_switch(ThreadContext* old_ctx, const ThreadContext* new_ctx);
; SysV AMD64 ABI:
;   RDI = old_ctx (pointer to ThreadContext of yielding thread)
;   RSI = new_ctx (pointer to ThreadContext of thread to activate)
context_switch:
    ; 1. Save old thread context if old_ctx is not null
    test rdi, rdi
    jz .load_new

    mov [rdi + 0],  r15
    mov [rdi + 8],  r14
    mov [rdi + 16], r13
    mov [rdi + 24], r12
    mov [rdi + 32], rbp
    mov [rdi + 40], rbx

    pushfq
    pop rax
    mov [rdi + 48], rax         ; saved rflags

    mov [rdi + 56], rsp         ; saved rsp (points to return address of call)

    lea rax, [rel .resume]
    mov [rdi + 64], rax         ; saved rip -> .resume

.load_new:
    ; 2. Load new thread context from new_ctx (RSI)
    mov r15, [rsi + 0]
    mov r14, [rsi + 8]
    mov r13, [rsi + 16]
    mov r12, [rsi + 24]
    mov rbp, [rsi + 32]
    mov rbx, [rsi + 40]

    mov rax, [rsi + 48]
    push rax
    popfq                       ; restore rflags

    mov rsp, [rsi + 56]         ; switch stack pointer
    mov rax, [rsi + 64]         ; target rip

    jmp rax                     ; transfer control to new thread context

.resume:
    ; Resumed here when old_ctx is rescheduled
    ret

; ------------------------------------------------------------------------------
; thread_bootstrap_trampoline: First-time entry point for new kernel threads
; ------------------------------------------------------------------------------
; Invariants on entry:
;   R12 = ThreadEntry function pointer: void (*)(void*)
;   R13 = void* argument
;   RSP = 16-byte aligned stack top
; ------------------------------------------------------------------------------
global thread_bootstrap_trampoline
extern thread_exit

thread_bootstrap_trampoline:
    ; System V ABI: direction flag must be clear
    cld

    ; Ensure interrupts are enabled for kernel thread execution
    sti

    ; Place argument in RDI (SysV ABI first argument register)
    mov rdi, r13
    call r12                    ; call entry(argument)

    ; If entry function returns, cleanly terminate thread
    call thread_exit

    ; Safety catch in case thread_exit returns (it must never return)
.hang:
    cli
    hlt
    jmp .hang

; ------------------------------------------------------------------------------
; test_reg_preservation_asm: Deterministic assembly test for callee-saved registers
; ------------------------------------------------------------------------------
; bool test_reg_preservation_asm(const uint64_t* sentinels /* RDI */, int64_t iterations /* RSI */);
; sentinels: [0]=RBX, [1]=RBP, [2]=R12, [3]=R13, [4]=R14, [5]=R15
; ------------------------------------------------------------------------------
global test_reg_preservation_asm
extern schedule_voluntary_yield

test_reg_preservation_asm:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    sub rsp, 32
    mov [rsp + 0], rdi          ; sentinels pointer
    mov [rsp + 8], rsi          ; remaining iterations

    ; Load sentinels into callee-saved registers
    mov rbx, [rdi + 0]
    mov rbp, [rdi + 8]
    mov r12, [rdi + 16]
    mov r13, [rdi + 24]
    mov r14, [rdi + 32]
    mov r15, [rdi + 40]

.reg_loop:
    mov rax, [rsp + 8]
    test rax, rax
    jle .reg_success

    ; Call voluntary yield to perform context switches
    call schedule_voluntary_yield

    ; Validate all 6 registers against initial sentinels
    mov rdi, [rsp + 0]
    cmp rbx, [rdi + 0]
    jne .reg_fail
    cmp rbp, [rdi + 8]
    jne .reg_fail
    cmp r12, [rdi + 16]
    jne .reg_fail
    cmp r13, [rdi + 24]
    jne .reg_fail
    cmp r14, [rdi + 32]
    jne .reg_fail
    cmp r15, [rdi + 40]
    jne .reg_fail

    dec qword [rsp + 8]
    jmp .reg_loop

.reg_success:
    add rsp, 32
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    mov rax, 1
    ret

.reg_fail:
    add rsp, 32
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    xor rax, rax
    ret

; Enforce non-executable stack policy
section .note.GNU-stack noalloc noexec nowrite progbits
