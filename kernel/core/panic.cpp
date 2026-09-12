#include "panic.hpp"
#include "kprint.hpp"
#include "drivers/serial.hpp"
#include "drivers/vga.hpp"
#include "arch/x86_64/cpu/cpu.hpp"
#include "arch/x86_64/cpu/msr.hpp"
#include "arch/x86_64/cpu/io.hpp"

// =============================================================================
// LlamaOS/A - Deterministic Kernel Panic Implementation
// =============================================================================

namespace llamaos {

using arch::x86_64::cli;
using arch::x86_64::halt;
using arch::x86_64::read_cr0;
using arch::x86_64::read_cr2;
using arch::x86_64::read_cr3;
using arch::x86_64::read_cr4;
using arch::x86_64::read_rflags;
using arch::x86_64::rdmsr;
using arch::x86_64::Msr;
using arch::x86_64::outb;
using drivers::VgaConsole;
using drivers::VgaColor;

[[noreturn]] void panic_handler(const char* file, int line, const char* function, const char* message) {
    cli();

    VgaConsole::set_color(VgaColor::White, VgaColor::Red);
    kprint("\n================================================================================\n");
    kprint("                        !!! KERNEL PANIC: LlamaOS/A !!!                         \n");
    kprint("================================================================================\n");

    VgaConsole::set_color(VgaColor::Yellow, VgaColor::Black);
    kprintf("Reason   : %s\n", message ? message : "Unknown reason");
    kprintf("Location : %s:%d in %s()\n", file ? file : "unknown", line, function ? function : "unknown");

    VgaConsole::set_color(VgaColor::LightCyan, VgaColor::Black);
    kprint("\n--- Architectural State ---\n");
    kprintf("CR0   : %p    CR2 (Page Fault Addr) : %p\n", read_cr0(), read_cr2());
    kprintf("CR3   : %p    CR4                   : %p\n", read_cr3(), read_cr4());
    kprintf("RFLAGS: %p    IA32_EFER             : %p\n", read_rflags(), rdmsr(Msr::Efer));

    VgaConsole::set_color(VgaColor::LightRed, VgaColor::Black);
    kprint("\nKernel halted. System execution suspended indefinitely.\n");
    kprint("================================================================================\n");

    // Signal failure code to QEMU debug exit if running in automated test harness
    outb(0xF4, 0x1F); // Resulting in exit code 63

    while (true) {
        halt();
    }
}

} // namespace llamaos
