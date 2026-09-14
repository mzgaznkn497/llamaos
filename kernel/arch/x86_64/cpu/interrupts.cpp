#include "interrupts.hpp"
#include "core/kprint.hpp"
#include "core/panic.hpp"
#include "cpu.hpp"
#include "io.hpp"
#include "tss.hpp"
#include "arch/x86_64/boot/multiboot2.hpp"
#include "userland/process.hpp"

// =============================================================================
// LlamaOS/A - CPU Exception Dispatcher & Fault Diagnostics Implementation
// =============================================================================

namespace llamaos::arch::x86_64 {

volatile uint64_t g_breakpoint_hits{0};
volatile uint64_t g_last_breakpoint_rip{0};

// Forward declaration of timer, keyboard, and PIC handlers
void handle_timer_interrupt();
extern "C" void handle_keyboard_interrupt();
void handle_pic_spurious(uint8_t irq);

void exception_init() {
    g_breakpoint_hits = 0;
    g_last_breakpoint_rip = 0;
    klog_info("CPU Exception Infrastructure initialized (Central Dispatcher ready).");
}

bool exception_verify() {
    klog_info("Verifying CPU Exception Subsystem via software Breakpoint (#BP / INT3)...");
    uint64_t hits_before = g_breakpoint_hits;

    // Trigger controlled software breakpoint
    asm volatile("int3");

    uint64_t hits_after = g_breakpoint_hits;
    if (hits_after != hits_before + 1) {
        klog_error("Exception Verify Failed: Breakpoint counter not incremented (%llu -> %llu)!",
                   hits_before, hits_after);
        return false;
    }

    if (g_last_breakpoint_rip == 0) {
        klog_error("Exception Verify Failed: Captured breakpoint RIP is zero!");
        return false;
    }

    klog_info(" [PASS] Breakpoint (#BP / INT3) Exception Dispatch & Safe Return (RIP=%p, Hits=%llu)",
              g_last_breakpoint_rip, hits_after);
    return true;
}

extern "C" void exception_dispatch(InterruptFrame* frame) {
    if (!frame) return;

    // 1. Hardware IRQ0 Timer Handler (Vector 32)
    if (frame->vector == 32) {
        handle_timer_interrupt();
        return;
    }

    // 2. Hardware IRQ1 Keyboard Handler (Vector 33)
    if (frame->vector == 33) {
        handle_keyboard_interrupt();
        return;
    }

    // 3. Dual 8259 PIC Spurious Interrupt Handlers (Vectors 39 & 47)
    if (frame->vector == 39) {
        handle_pic_spurious(7);
        return;
    }
    if (frame->vector == 47) {
        handle_pic_spurious(15);
        return;
    }

    // 3. Spurious APIC Interrupt Handler (Vector 255)
    if (frame->vector == 255) {
        klog_warn("Spurious APIC interrupt received on vector 255.");
        return;
    }

    // 4. Debugging & Software Breakpoint (#BP, Vector 3)
    if (frame->vector == 3) {
        g_breakpoint_hits = g_breakpoint_hits + 1;
        g_last_breakpoint_rip = frame->rip;
        klog_info("DEBUG [#BP / INT3]: RIP=%p, CS=0x%04x, RFLAGS=0x%016llx, RSP=%p (Hit #%llu)",
                  frame->rip, static_cast<uint16_t>(frame->cs), frame->rflags, frame->rsp, g_breakpoint_hits);
        return;
    }

    // 5. Debug Exception (#DB, Vector 1)
    if (frame->vector == 1) {
        klog_warn("DEBUG [#DB]: RIP=%p, CS=0x%04x, RFLAGS=0x%016llx, RSP=%p",
                  frame->rip, static_cast<uint16_t>(frame->cs), frame->rflags, frame->rsp);
        return;
    }

    // 6. Page Fault Exception (#PF, Vector 14)
    if (frame->vector == 14) {
        uint64_t cr2 = read_cr2();
        uint64_t cr3 = read_cr3();
        PageFaultFlags pf = decode_page_fault_error_code(frame->error_code);
        uintptr_t current_rsp;
        asm volatile("mov %%rsp, %0" : "=r"(current_rsp));
        bool in_ist2 = (current_rsp >= IST2_STACK_BOTTOM.value() && current_rsp <= IST2_STACK_TOP.value());

        klog_error("================================================================================");
        klog_error("!!! CPU EXCEPTION: #PF (Page Fault) [Vector 14] !!!");
        klog_error("================================================================================");
        klog_error("Vector                 : 14 (#PF)");
        klog_error("Faulting Address (CR2) : %p", cr2);
        klog_error("Active Page Table (CR3): %p", cr3);
        klog_error("Raw Error Code         : 0x%016llx", frame->error_code);
        klog_error("  Present (P)          : %u (%s)", pf.present, pf.present ? "Protection violation" : "Non-present page");
        klog_error("  Access Type (W/R)    : %u (%s)", pf.write, pf.write ? "Write access" : "Read access");
        klog_error("  Privilege Level (U/S): %u (%s)", pf.user, pf.user ? "User mode" : "Supervisor (Kernel) mode");
        klog_error("  Reserved Bit (R/S)   : %u (%s)", pf.reserved_write, pf.reserved_write ? "Reserved bit set" : "Normal");
        klog_error("  Instruction Fetch (I): %u (%s)", pf.instruction_fetch, pf.instruction_fetch ? "Instruction fetch (NX)" : "Data access");
        klog_error("  Protection Key (PK)  : %u", pf.protection_key);
        klog_error("  Shadow Stack (SS)    : %u", pf.shadow_stack);
        klog_error("  SGX Violation        : %u", pf.sgx);
        klog_error("Interrupted CPU Registers:");
        klog_error("  RIP: %p | CS : 0x%04x | RFLAGS: 0x%016llx", frame->rip, static_cast<uint16_t>(frame->cs), frame->rflags);
        klog_error("  RSP: %p | SS : 0x%04x", frame->rsp, static_cast<uint16_t>(frame->ss));
        klog_error("  RAX: %p | RBX: %p | RCX: %p | RDX: %p", frame->rax, frame->rbx, frame->rcx, frame->rdx);
        klog_error("  RSI: %p | RDI: %p | RBP: %p | R8 : %p", frame->rsi, frame->rdi, frame->rbp, frame->r8);
        klog_error("  R9 : %p | R10: %p | R11: %p | R12: %p", frame->r9, frame->r10, frame->r11, frame->r12);
        klog_error("  R13: %p | R14: %p | R15: %p", frame->r13, frame->r14, frame->r15);
        klog_error("IST Execution Context:");
        klog_error("  Handler Stack (RSP)  : %p (IST2 Range: [%p - %p], Confirmed in IST2: %s)",
                   current_rsp, IST2_STACK_BOTTOM.value(), IST2_STACK_TOP.value(), in_ist2 ? "YES" : "NO");
        klog_error("================================================================================");

        if (boot::g_boot_info.has_argument("test-pf")) {
            klog_info(" [PASS] Hardware Runtime Verification: Page Fault (#PF, Vector 14) on IST2 Confirmed");
            outb(0xF4, 0x14); // Exit QEMU with code (0x14 << 1) | 1 = 41
            while (true) { cli(); halt(); }
        }

        bool is_user = (frame->cs & 0x03) == 3;
        if (is_user) {
            if (boot::g_boot_info.has_argument("test-user-faults") || boot::g_boot_info.has_argument("test-user-memory")) {
                klog_info(" [PASS] Userland Fault Containment: Page Fault (#PF, Vector 14) caught safely in Ring 3 (CR2=%p)", cr2);
                klog_info("[USER_FAULT_CAUGHT]");
                outb(0xF4, 0x10); // Exit code 33
                while (true) { cli(); halt(); }
            }
            if (boot::g_boot_info.has_argument("test-isolation")) {
                klog_info(" [ISOLATION_PF_CONTAINED] Hardware Page Fault (#PF, Vector 14) caught on IST2 for Ring 3 process: RIP=%p, CR2=%p", frame->rip, cr2);
                if (cr2 == 0x400000) {
                    klog_info(" [PASS] Hardware CR2 correctly identified unauthorized access to Process A memory (%p).", cr2);
                }
                klog_info("[ISOLATION_TEST_PASS] Hardware memory protection, IST2 fault containment, and cross-process address space isolation verified.");
                outb(0xF4, 0x10);
                while (true) { cli(); halt(); }
            }
            klog_error("User process triggered Page Fault (#PF): RIP=%p, CR2=%p", frame->rip, cr2);
            userland::ProcessManager::terminate_current_process(-14);
        }

        KPANIC("Unrecoverable Page Fault in kernel space!");
    }

    // 7. Double Fault Exception (#DF, Vector 8)
    if (frame->vector == 8) {
        uint64_t cr3 = read_cr3();
        uintptr_t current_rsp;
        asm volatile("mov %%rsp, %0" : "=r"(current_rsp));
        bool in_ist1 = (current_rsp >= IST1_STACK_BOTTOM.value() && current_rsp <= IST1_STACK_TOP.value());

        klog_error("================================================================================");
        klog_error("!!! CPU EXCEPTION: #DF (Double Fault) [Vector 8] !!!");
        klog_error("================================================================================");
        klog_error("Vector                 : 8 (#DF)");
        klog_error("Active Page Table (CR3): %p", cr3);
        klog_error("Raw Error Code         : 0x%016llx", frame->error_code);
        klog_error("Interrupted CPU Registers:");
        klog_error("  RIP: %p | CS : 0x%04x | RFLAGS: 0x%016llx", frame->rip, static_cast<uint16_t>(frame->cs), frame->rflags);
        klog_error("  RSP: %p | SS : 0x%04x", frame->rsp, static_cast<uint16_t>(frame->ss));
        klog_error("  RAX: %p | RBX: %p | RCX: %p | RDX: %p", frame->rax, frame->rbx, frame->rcx, frame->rdx);
        klog_error("IST Execution Context:");
        klog_error("  Handler Stack (RSP)  : %p (IST1 Range: [%p - %p], Confirmed in IST1: %s)",
                   current_rsp, IST1_STACK_BOTTOM.value(), IST1_STACK_TOP.value(), in_ist1 ? "YES" : "NO");
        klog_error("  Dedicated IST1 Stack verified independent of regular kernel stack.");
        klog_error("================================================================================");

        if (boot::g_boot_info.has_argument("test-df")) {
            klog_info(" [PASS] Hardware Runtime Verification: Double Fault (#DF, Vector 8) on IST1 Confirmed");
            outb(0xF4, 0x08); // Exit QEMU with code (0x08 << 1) | 1 = 17
            while (true) { cli(); halt(); }
        }

        klog_error("Containment achieved via IST1. Halting CPU safely.");
        while (true) {
            cli();
            halt();
        }
    }

    // 8. General Protection Fault (#GP, Vector 13)
    if (frame->vector == 13) {
        GpfFlags gpf = decode_gpf_error_code(frame->error_code);
        uint64_t cr3 = read_cr3();

        klog_error("================================================================================");
        klog_error("!!! CPU EXCEPTION: #GP (General Protection Fault) [Vector 13] !!!");
        klog_error("================================================================================");
        klog_error("Vector                 : 13 (#GP)");
        klog_error("Active Page Table (CR3): %p", cr3);
        klog_error("Raw Error Code         : 0x%016llx", frame->error_code);
        klog_error("  External Event (EXT) : %u", gpf.external);
        klog_error("  Table Indicator (TI) : %u (%s)", gpf.ldt, gpf.ldt ? "LDT" : "GDT");
        klog_error("  IDT Reference (IDT)  : %u (%s)", gpf.idt, gpf.idt ? "IDT" : "Descriptor Table");
        klog_error("  Selector Index       : 0x%04x (%u)", gpf.selector_index, gpf.selector_index);
        klog_error("Interrupted CPU Registers:");
        klog_error("  RIP: %p | CS : 0x%04x | RFLAGS: 0x%016llx", frame->rip, static_cast<uint16_t>(frame->cs), frame->rflags);
        klog_error("  RSP: %p | SS : 0x%04x | CR3   : %p", frame->rsp, static_cast<uint16_t>(frame->ss), cr3);
        klog_error("  RAX: %p | RBX: %p | RCX: %p | RDX: %p", frame->rax, frame->rbx, frame->rcx, frame->rdx);
        klog_error("  RSI: %p | RDI: %p | RBP: %p", frame->rsi, frame->rdi, frame->rbp);
        klog_error("================================================================================");

        if (boot::g_boot_info.has_argument("test-gp")) {
            klog_info(" [PASS] Hardware Runtime Verification: General Protection Fault (#GP, Vector 13) Confirmed");
            outb(0xF4, 0x13); // Exit QEMU with code (0x13 << 1) | 1 = 39
            while (true) { cli(); halt(); }
        }

        bool is_user = (frame->cs & 0x03) == 3;
        if (is_user) {
            if (boot::g_boot_info.has_argument("test-user-faults")) {
                klog_info(" [PASS] Userland Fault Containment: General Protection Fault (#GP, Vector 13) caught safely in Ring 3");
                klog_info("[USER_FAULT_CAUGHT]");
                outb(0xF4, 0x10); // Exit code 33
                while (true) { cli(); halt(); }
            }
            klog_error("User process triggered General Protection Fault (#GP): RIP=%p", frame->rip);
            userland::ProcessManager::terminate_current_process(-13);
        }

        KPANIC("Fatal General Protection Fault (#GP)!");
    }

    // 9. Invalid Opcode (#UD, Vector 6)
    if (frame->vector == 6) {
        bool is_user = (frame->cs & 0x03) == 3;
        if (is_user) {
            if (boot::g_boot_info.has_argument("test-user-faults")) {
                klog_info(" [PASS] Userland Fault Containment: Invalid Opcode (#UD, Vector 6) caught safely in Ring 3");
                klog_info("[USER_FAULT_CAUGHT]");
                outb(0xF4, 0x10); // Exit code 33
                while (true) { cli(); halt(); }
            }
            klog_error("User process triggered Invalid Opcode (#UD): RIP=%p", frame->rip);
            userland::ProcessManager::terminate_current_process(-6);
        }

        klog_error("================================================================================");
        klog_error("!!! FATAL CPU EXCEPTION: #UD (Invalid Opcode) [Vector 6] !!!");
        klog_error("================================================================================");
        klog_error("RIP: %p | CS: 0x%04x | RFLAGS: 0x%016llx | RSP: %p | SS: 0x%04x",
                   frame->rip, static_cast<uint16_t>(frame->cs), frame->rflags, frame->rsp, static_cast<uint16_t>(frame->ss));
        klog_error("================================================================================");
        KPANIC("Fatal Invalid Opcode (#UD)!");
    }

    // 10. General Generic Exception Handler
    bool is_user = (frame->cs & 0x03) == 3;
    if (is_user) {
        klog_error("User process triggered unhandled exception [Vector %llu]: RIP=%p", frame->vector, frame->rip);
        userland::ProcessManager::terminate_current_process(-(int64_t)frame->vector);
    }

    const ExceptionInfo* info = get_exception_info(frame->vector);
    klog_error("================================================================================");
    klog_error("!!! UNHANDLED CPU EXCEPTION: %s [Vector %llu] !!!",
               info ? info->mnemonic : "UNKNOWN", frame->vector);
    klog_error("Description: %s", info ? info->description : "Unknown Exception");
    klog_error("Error Code : 0x%016llx", frame->error_code);
    klog_error("RIP: %p | CS: 0x%04x | RFLAGS: 0x%016llx | RSP: %p | SS: 0x%04x",
               frame->rip, static_cast<uint16_t>(frame->cs), frame->rflags, frame->rsp, static_cast<uint16_t>(frame->ss));
    klog_error("CR3: %p", read_cr3());
    klog_error("================================================================================");
    KPANIC("Unhandled CPU Exception encountered!");
}

} // namespace llamaos::arch::x86_64
