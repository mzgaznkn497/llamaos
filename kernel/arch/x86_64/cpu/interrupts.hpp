#pragma once

#include "core/types.hpp"
#include "cpu.hpp"

// =============================================================================
// LlamaOS/A - CPU Exception Dispatcher and Interrupt Infrastructure
// =============================================================================
// Provides normalized exception frames, central exception dispatcher, fault
// diagnostics (Page Fault, Double Fault, General Protection Fault, etc.),
// software breakpoint support (#BP / INT3), and real CPU interrupt control APIs.
// =============================================================================

namespace llamaos::arch::x86_64 {

// Normalized Interrupt / Exception Frame (176 bytes)
// Exactly matches the layout constructed by the assembly ISR stubs
struct [[gnu::packed]] InterruptFrame {
    // 15 General Purpose Registers saved by common assembly stub
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    // Vector and Error Code pushed by vector stub
    uint64_t vector;
    uint64_t error_code;

    // Architectural stack frame pushed automatically by x86-64 CPU
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

static_assert(sizeof(InterruptFrame) == 176, "InterruptFrame must be exactly 176 bytes");

// Exception Metadata
struct ExceptionInfo {
    const char* mnemonic;
    const char* description;
    bool has_error_code;
};

inline constexpr ExceptionInfo s_exception_table[32] = {
    { "#DE", "Divide-by-Zero Error",                false }, // 0
    { "#DB", "Debug Exception",                     false }, // 1
    { "#NMI","Non-Maskable Interrupt",              false }, // 2
    { "#BP", "Breakpoint (INT3)",                   false }, // 3
    { "#OF", "Overflow",                            false }, // 4
    { "#BR", "Bound Range Exceeded",                false }, // 5
    { "#UD", "Invalid Opcode",                      false }, // 6
    { "#NM", "Device Not Available (No Coproc)",    false }, // 7
    { "#DF", "Double Fault",                        true  }, // 8
    { "#CSO","Coprocessor Segment Overrun",         false }, // 9
    { "#TS", "Invalid TSS",                         true  }, // 10
    { "#NP", "Segment Not Present",                 true  }, // 11
    { "#SS", "Stack-Segment Fault",                 true  }, // 12
    { "#GP", "General Protection Fault",            true  }, // 13
    { "#PF", "Page Fault",                          true  }, // 14
    { "#RES","Reserved",                            false }, // 15
    { "#MF", "x87 Floating-Point Exception",        false }, // 16
    { "#AC", "Alignment Check",                     true  }, // 17
    { "#MC", "Machine Check",                       false }, // 18
    { "#XM", "SIMD Floating-Point Exception",       false }, // 19
    { "#VE", "Virtualization Exception",            false }, // 20
    { "#CP", "Control Protection Exception",        true  }, // 21
    { "#RES","Reserved",                            false }, // 22
    { "#RES","Reserved",                            false }, // 23
    { "#RES","Reserved",                            false }, // 24
    { "#RES","Reserved",                            false }, // 25
    { "#RES","Reserved",                            false }, // 26
    { "#RES","Reserved",                            false }, // 27
    { "#HV", "Hypervisor Injection Exception",      false }, // 28
    { "#VC", "VMM Communication Exception",         true  }, // 29
    { "#SX", "Security Exception",                  true  }, // 30
    { "#RES","Reserved",                            false }  // 31
};

inline constexpr const ExceptionInfo* get_exception_info(size_t vector) {
    if (vector < 32) {
        return &s_exception_table[vector];
    }
    return nullptr;
}

// Page Fault Error Code Decoder
struct PageFaultFlags {
    bool present;              // Bit 0: 0=non-present page, 1=protection violation
    bool write;                // Bit 1: 0=read access, 1=write access
    bool user;                 // Bit 2: 0=supervisor/kernel, 1=user mode
    bool reserved_write;       // Bit 3: 1=reserved bit set in page hierarchy
    bool instruction_fetch;    // Bit 4: 1=instruction fetch violation (NX/SMEP)
    bool protection_key;       // Bit 5: 1=protection key violation (PKEY)
    bool shadow_stack;         // Bit 6: 1=shadow stack access violation
    bool sgx;                  // Bit 15: 1=SGX violation
};

inline PageFaultFlags decode_page_fault_error_code(uint64_t error_code) {
    PageFaultFlags f{};
    f.present           = (error_code & (1ULL << 0)) != 0;
    f.write             = (error_code & (1ULL << 1)) != 0;
    f.user              = (error_code & (1ULL << 2)) != 0;
    f.reserved_write    = (error_code & (1ULL << 3)) != 0;
    f.instruction_fetch = (error_code & (1ULL << 4)) != 0;
    f.protection_key    = (error_code & (1ULL << 5)) != 0;
    f.shadow_stack      = (error_code & (1ULL << 6)) != 0;
    f.sgx               = (error_code & (1ULL << 15)) != 0;
    return f;
}

// General Protection Fault Error Code Decoder
struct GpfFlags {
    bool external;             // Bit 0: Event originated externally
    bool idt;                  // Bit 1: Selector references IDT
    bool ldt;                  // Bit 2: Selector references LDT (0=GDT)
    uint16_t selector_index;   // Bits 3..15: Descriptor index
};

inline GpfFlags decode_gpf_error_code(uint64_t error_code) {
    GpfFlags f{};
    f.external       = (error_code & (1ULL << 0)) != 0;
    f.idt            = (error_code & (1ULL << 1)) != 0;
    f.ldt            = (error_code & (1ULL << 2)) != 0;
    f.selector_index = static_cast<uint16_t>((error_code >> 3) & 0x1FFF);
    return f;
}

// Breakpoint verification tracking
extern volatile uint64_t g_breakpoint_hits;
extern volatile uint64_t g_last_breakpoint_rip;

// Exception subsystem verification and dispatch
void exception_init();
bool exception_verify();

// Central C++ Exception & Interrupt Dispatcher
extern "C" void exception_dispatch(InterruptFrame* frame);

// Architecture Interrupt Control API (Section 14)
inline bool interrupts_enabled() noexcept {
    return (read_rflags() & (1ULL << 9)) != 0;
}

inline void enable_interrupts() noexcept {
    asm volatile("sti" ::: "memory");
}

inline void disable_interrupts() noexcept {
    asm volatile("cli" ::: "memory");
}

inline uint64_t save_and_disable_interrupts() noexcept {
    uint64_t flags = read_rflags();
    asm volatile("cli" ::: "memory");
    return flags;
}

inline void restore_interrupt_state(uint64_t flags) noexcept {
    if ((flags & (1ULL << 9)) != 0) {
        asm volatile("sti" ::: "memory");
    } else {
        asm volatile("cli" ::: "memory");
    }
}

// ISR Assembly Entry Point Declarations
extern "C" {
    void isr_stub_0();
    void isr_stub_1();
    void isr_stub_2();
    void isr_stub_3();
    void isr_stub_4();
    void isr_stub_5();
    void isr_stub_6();
    void isr_stub_7();
    void isr_stub_8();
    void isr_stub_10();
    void isr_stub_11();
    void isr_stub_12();
    void isr_stub_13();
    void isr_stub_14();
    void isr_stub_16();
    void isr_stub_17();
    void isr_stub_18();
    void isr_stub_19();
    void isr_stub_20();
    void isr_stub_21();

    void isr_stub_32(); // IRQ0 Timer
    void isr_stub_33(); // IRQ1 Keyboard
    void isr_stub_39(); // IRQ7 Spurious Master
    void isr_stub_47(); // IRQ15 Spurious Slave
    void isr_stub_255(); // Spurious APIC
}

} // namespace llamaos::arch::x86_64
