; =============================================================================
; LlamaOS/A - Phase 6 64-bit System Call Entry & Return Assembly
; =============================================================================
; Implements the low-level CPU SYSCALL entry point configured in IA32_LSTAR.
; Captures CPU register state into a 128-byte SyscallFrame, handles stack
; transitions, invokes the C++ dispatcher, and cleanly returns to caller.
; =============================================================================

[bits 64]
default rel

global syscall_entry
global g_syscall_scratch_rsp
global g_syscall_scratch_rax
extern syscall_dispatch

section .data
align 16
g_syscall_scratch_rsp: dq 0
g_syscall_scratch_rax: dq 0

section .text
align 16
syscall_entry:
    ; -------------------------------------------------------------------------
    ; 1. Hardware Entry State:
    ;    RCX    <- Caller RIP (saved by CPU hardware)
    ;    R11    <- Caller RFLAGS (saved by CPU hardware)
    ;    RFLAGS <- RFLAGS & ~IA32_FMASK (IF=0, DF=0, TF=0, NT=0, AC=0)
    ;    CS     <- 0x0008 (Kernel Code Selector)
    ;    SS     <- 0x0010 (Kernel Data Selector)
    ;    RSP    <- Unchanged caller stack pointer
    ;    RAX    <- Syscall number
    ;    RDI    <- Argument 0
    ;    RSI    <- Argument 1
    ;    RDX    <- Argument 2
    ;    R10    <- Argument 3
    ;    R8     <- Argument 4
    ;    R9     <- Argument 5
    ; -------------------------------------------------------------------------

    ; Save caller RSP to temporary scratch memory
    mov [rel g_syscall_scratch_rsp], rsp

    ; Determine whether the caller executed from Ring 0 (kernel) or Ring 3 (user).
    ; In canonical AMD64 Long Mode:
    ; - Kernel virtual addresses reside in the higher half (bit 63 == 1, negative).
    ; - User virtual addresses reside in the lower half (bit 63 == 0, positive).
    ; RCX contains the caller RIP.
    test rcx, rcx
    js .caller_is_kernel

.caller_is_user:
    ; Caller was in Ring 3 (Phase 7):
    ; Switch to the active thread's kernel stack recorded in TSS.RSP0.
    ; TSS is mapped at fixed virtual address 0xFFFFFFFF70010000, rsp0 is at offset +4.
    mov [rel g_syscall_scratch_rax], rax
    mov rax, 0xFFFFFFFF70010004
    mov rsp, [rax]
    mov rax, [rel g_syscall_scratch_rax]
    jmp .build_frame

.caller_is_kernel:
    ; Caller was already in Ring 0 (Phase 6):
    ; RSP is already valid on the active kernel thread's stack.
    mov rsp, [rel g_syscall_scratch_rsp]

.build_frame:
    ; -------------------------------------------------------------------------
    ; 2. Push SyscallFrame (128 bytes total = 16 qwords, 16-byte aligned):
    ;    Offset 0x78: saved caller rsp
    ;    Offset 0x70: saved caller rflags (from R11)
    ;    Offset 0x68: saved caller rip (from RCX)
    ;    Offset 0x60: rax (syscall number)
    ;    Offset 0x58: rdi (arg0)
    ;    Offset 0x50: rsi (arg1)
    ;    Offset 0x48: rdx (arg2)
    ;    Offset 0x40: r10 (arg3)
    ;    Offset 0x38: r8  (arg4)
    ;    Offset 0x30: r9  (arg5)
    ;    Offset 0x28: rbx
    ;    Offset 0x20: rbp
    ;    Offset 0x18: r12
    ;    Offset 0x10: r13
    ;    Offset 0x08: r14
    ;    Offset 0x00: r15
    ; -------------------------------------------------------------------------
    push qword [rel g_syscall_scratch_rsp] ; +0x78: rsp
    push r11                              ; +0x70: rflags
    push rcx                              ; +0x68: rip
    push rax                              ; +0x60: rax (syscall number)
    push rdi                              ; +0x58: rdi (arg0)
    push rsi                              ; +0x50: rsi (arg1)
    push rdx                              ; +0x48: rdx (arg2)
    push r10                              ; +0x40: r10 (arg3)
    push r8                               ; +0x38: r8  (arg4)
    push r9                               ; +0x30: r9  (arg5)
    push rbx                              ; +0x28: rbx
    push rbp                              ; +0x20: rbp
    push r12                              ; +0x18: r12
    push r13                              ; +0x10: r13
    push r14                              ; +0x08: r14
    push r15                              ; +0x00: r15

    ; Conforms to System V AMD64 ABI: Direction flag must be clear
    cld

    ; -------------------------------------------------------------------------
    ; 3. Invoke C++ Syscall Dispatcher:
    ;    int64_t syscall_dispatch(SyscallFrame* frame)
    ;    Pass frame pointer in RDI (1st argument per SysV AMD64 ABI)
    ; -------------------------------------------------------------------------
    mov rdi, rsp
    call syscall_dispatch

    ; Store dispatcher return value into frame.rax
    mov [rsp + 0x60], rax

    ; -------------------------------------------------------------------------
    ; 4. Restore Architectural State from SyscallFrame:
    ; -------------------------------------------------------------------------
    pop r15                               ; +0x00
    pop r14                               ; +0x08
    pop r13                               ; +0x10
    pop r12                               ; +0x18
    pop rbp                               ; +0x20
    pop rbx                               ; +0x28
    pop r9                                ; +0x30
    pop r8                                ; +0x38
    pop r10                               ; +0x40
    pop rdx                               ; +0x48
    pop rsi                               ; +0x50
    pop rdi                               ; +0x58
    pop rax                               ; +0x60: return value in RAX
    pop rcx                               ; +0x68: caller rip into RCX
    pop r11                               ; +0x70: caller rflags into R11
    pop rsp                               ; +0x78: restore caller rsp

    ; -------------------------------------------------------------------------
    ; 5. Architectural Return:
    ;    Check if returning to Ring 0 (kernel) or Ring 3 (user space).
    ; -------------------------------------------------------------------------
    test rcx, rcx
    js .return_to_kernel

.return_to_user:
    ; Returning to Ring 3 (Phase 7):
    ; Execute SYSRETQ (REX.W prefix + 0x0F 0x07).
    ; Hardware restores: RIP <- RCX, RFLAGS <- R11, CS <- 0x33, SS <- 0x2B
    o64 sysret

.return_to_kernel:
    ; Returning to Ring 0 (Phase 6):
    ; SYSRETQ cannot be used because it forces CPL=3.
    ; Restore caller RFLAGS and transfer control back to caller RIP.
    push r11
    popfq                                 ; Restores RFLAGS (including IF)
    jmp rcx                               ; Resume execution at caller RIP
