#include "core/types.hpp"
#include "core/string.hpp"
#include "core/kprint.hpp"
#include "core/panic.hpp"
#include "arch/x86_64/cpu/cpu.hpp"
#include "arch/x86_64/cpu/io.hpp"
#include "arch/x86_64/cpu/msr.hpp"
#include "arch/x86_64/boot/multiboot2.hpp"
#include "drivers/serial.hpp"
#include "drivers/vga.hpp"

// =============================================================================
// LlamaOS/A - Higher-Half Kernel Entry Point
// =============================================================================

extern "C" {
    extern uint8_t _kernel_physical_start[];
    extern uint8_t _kernel_physical_end[];
    extern uint8_t _kernel_virtual_start[];
    extern uint8_t _kernel_virtual_end[];
    extern uint8_t _text_start[];
    extern uint8_t _text_end[];
    extern uint8_t _rodata_start[];
    extern uint8_t _rodata_end[];
    extern uint8_t _data_start[];
    extern uint8_t _data_end[];
    extern uint8_t _bss_start[];
    extern uint8_t _bss_end[];
    extern uint8_t kernel_stack_bottom[];
    extern uint8_t kernel_stack_top[];

    [[noreturn]] void kernel_main(uint64_t mb2_magic, uint64_t mb2_info_virt);
}

namespace llamaos {

using arch::x86_64::g_cpu_info;
using arch::x86_64::halt;
using arch::x86_64::outb;
using boot::g_boot_info;
using boot::MemoryType;
using drivers::SerialPort;
using drivers::VgaConsole;
using drivers::VgaColor;

static void print_banner() {
    VgaConsole::set_color(VgaColor::LightCyan, VgaColor::Black);
    kprint("================================================================================\n");
    kprint("  _     _                           ____   _____       _   \n");
    kprint(" | |   | |                         / __ \\ / ____|     / \\  \n");
    kprint(" | |   | |     __ _ _ __ ___   __ | |  | | (___      / _ \\ \n");
    kprint(" | |   | |    / _` | '_ ` _ \\ / _`| |  | |\\___ \\    / ___ \\\n");
    kprint(" | |___| |___| (_| | | | | | | (_|| |__| |____) |  / /   \\ \\\n");
    kprint(" |_____|______\\__,_|_| |_| |_|\\__,_\\____/|_____/  /_/     \\_\\\n");
    kprint("================================================================================\n");
    VgaConsole::set_color(VgaColor::White, VgaColor::Black);
    kprint(" LlamaOS/A Kernel [x86-64] - Milestone 1: Bootable Foundation\n");
    kprint(" Build Target: x86_64-elf | Language: C++20 Freestanding\n");
    kprint(" Mode: IA-32e Long Mode (64-bit) | Paging: 4-Level Higher-Half (-2 GiB)\n");
    kprint("================================================================================\n\n");
    VgaConsole::set_color(VgaColor::LightGray, VgaColor::Black);
}

static bool run_self_tests() {
    klog_info("Executing Phase 1 early verification self-tests...");

    // Test 1: CPUID and Long Mode capability
    if (!g_cpu_info.features.lm) {
        klog_error("Self-Test Failed: Long Mode flag not reported by CPUID.");
        return false;
    }
    klog_info(" [PASS] CPU Architecture Verification (x86-64 Long Mode validated)");

    // Test 2: Memory string and memory manipulation integrity
    char test_buf1[64] = "LlamaOS/A Verification Test String";
    char test_buf2[64];
    memset(test_buf2, 0, sizeof(test_buf2));
    memcpy(test_buf2, test_buf1, strlen(test_buf1) + 1);

    if (strcmp(test_buf1, test_buf2) != 0) {
        klog_error("Self-Test Failed: memcpy / strcmp memory check mismatch.");
        return false;
    }
    klog_info(" [PASS] Freestanding Memory Primitives (memcpy, memset, strcmp)");

    // Test 3: Multiboot2 handshake
    if (!g_boot_info.valid) {
        klog_error("Self-Test Failed: Multiboot2 information block invalid or missing.");
        return false;
    }
    klog_info(" [PASS] Bootloader Protocol Handshake (Multiboot2 valid)");

    // Test 4: Higher-half address boundaries
    uintptr_t virt_start = reinterpret_cast<uintptr_t>(_kernel_virtual_start);
    if (virt_start < KERNEL_VIRTUAL_BASE) {
        klog_error("Self-Test Failed: Kernel virtual address %p below KERNEL_VIRTUAL_BASE.", virt_start);
        return false;
    }
    klog_info(" [PASS] Higher-Half Virtual Memory Layout (%p >= %p)", virt_start, KERNEL_VIRTUAL_BASE);

    klog_info("All Phase 1 early self-tests passed successfully.");
    return true;
}

} // namespace llamaos

[[noreturn]] void kernel_main(uint64_t mb2_magic, uint64_t mb2_info_virt) {
    using namespace llamaos;

    // Step 1: Initialize baseline drivers (Serial COM1 + port 0xE9, VGA 80x25 text console)
    drivers::SerialPort::init(drivers::SerialPort::COM1_BASE);
    drivers::VgaConsole::init();

    // Step 2: Display official system banner
    print_banner();

    // Step 3: Parse Multiboot2 bootloader data structures
    klog_info("Parsing Multiboot2 boot information at virtual address %p...", mb2_info_virt);
    g_boot_info.parse(mb2_magic, mb2_info_virt);

    if (g_boot_info.valid) {
        klog_info("Bootloader identified : %s", g_boot_info.bootloader_name ? g_boot_info.bootloader_name : "Unknown");
        klog_info("Kernel command line   : %s", g_boot_info.command_line ? g_boot_info.command_line : "(none)");
        klog_info("Basic memory reported : Lower=%u KB, Upper=%u KB", g_boot_info.mem_lower_kb, g_boot_info.mem_upper_kb);
        klog_info("Total usable RAM      : %u MiB (%u bytes)",
                  static_cast<uint32_t>(g_boot_info.total_usable_ram_bytes / (1024 * 1024)),
                  static_cast<uint32_t>(g_boot_info.total_usable_ram_bytes));

        if (g_boot_info.framebuffer.available) {
            klog_info("Framebuffer detected  : %ux%u @ %u bpp (addr=%p, pitch=%u)",
                      g_boot_info.framebuffer.width,
                      g_boot_info.framebuffer.height,
                      g_boot_info.framebuffer.bpp,
                      g_boot_info.framebuffer.address,
                      g_boot_info.framebuffer.pitch);
        }
    } else {
        klog_warn("Multiboot2 information tag block was not valid or not detected.");
    }

    // Step 4: Perform detailed CPU reconnaissance
    klog_info("Performing CPUID hardware reconnaissance...");
    g_cpu_info.detect();
    klog_info("CPU Vendor            : %s", g_cpu_info.vendor);
    klog_info("CPU Model / Brand     : %s", g_cpu_info.brand);
    klog_info("CPU Topology          : Family=%u, Model=%u, Stepping=%u",
              g_cpu_info.family, g_cpu_info.model, g_cpu_info.stepping);
    klog_info("Key CPU Features      : FPU=%b, SSE=%b, SSE2=%b, AVX=%b, APIC=%b, NX=%b, LM=%b",
              g_cpu_info.features.fpu,
              g_cpu_info.features.sse,
              g_cpu_info.features.sse2,
              g_cpu_info.features.avx,
              g_cpu_info.features.apic,
              g_cpu_info.features.nx,
              g_cpu_info.features.lm);

    // Step 5: Report kernel section mapping
    klog_info("Kernel Memory Layout (Higher-Half x86-64):");
    klog_info("  Physical Load Range : [%p - %p]", _kernel_physical_start, _kernel_physical_end);
    klog_info("  Virtual Load Range  : [%p - %p]", _kernel_virtual_start, _kernel_virtual_end);
    klog_info("  .text   (Code)      : [%p - %p]", _text_start, _text_end);
    klog_info("  .rodata (Read-only) : [%p - %p]", _rodata_start, _rodata_end);
    klog_info("  .data   (Read/Write): [%p - %p]", _data_start, _data_end);
    klog_info("  .bss    (Zero-init) : [%p - %p]", _bss_start, _bss_end);
    klog_info("  Kernel Stack        : [%p - %p] (size: 64 KiB)", kernel_stack_bottom, kernel_stack_top);

    // Step 6: Log detected physical memory map
    if (g_boot_info.mmap_count > 0) {
        klog_info("Physical Memory Map (%u regions enumerated):", static_cast<uint32_t>(g_boot_info.mmap_count));
        for (size_t i = 0; i < g_boot_info.mmap_count && i < 8; ++i) {
            const auto& e = g_boot_info.mmap_entries[i];
            klog_info("  [%02u] Base: %p, Len: %p (%u MiB) - %s",
                      static_cast<uint32_t>(i),
                      e.base_addr,
                      e.length,
                      static_cast<uint32_t>(e.length / (1024 * 1024)),
                      g_boot_info.memory_type_to_string(static_cast<MemoryType>(e.type)));
        }
        if (g_boot_info.mmap_count > 8) {
            klog_info("  ... and %u more regions.", static_cast<uint32_t>(g_boot_info.mmap_count - 8));
        }
    }

    // Step 7: Run verification self-tests
    bool tests_passed = run_self_tests();
    if (!tests_passed) {
        KPANIC("Self-test suite failure detected during Phase 1 boot!");
    }

    // Step 8: Handle automated test harness termination
    bool automated_test_mode = false;
    if (g_boot_info.command_line &&
        (strchr(g_boot_info.command_line, 't') != nullptr ||
         strchr(g_boot_info.command_line, 's') != nullptr)) {
        automated_test_mode = true;
    }

    VgaConsole::set_color(VgaColor::LightGreen, VgaColor::Black);
    kprint("\n>>> LlamaOS/A Kernel Boot Milestone 1 Accomplished Successfully! <<<\n\n");
    VgaConsole::set_color(VgaColor::LightGray, VgaColor::Black);

    if (automated_test_mode) {
        klog_info("Automated test mode detected in command line. Signalling debug exit (0xF4 -> 0x10)...");
        // QEMU isa-debug-exit: write 0x10 to port 0xF4 generates QEMU exit code (0x10 << 1) | 1 = 33
        arch::x86_64::outb(0xF4, 0x10);
    }

    klog_info("System idle. CPU entering low-power halt loop.");

    // Step 9: Deterministic system idle halt loop
    while (true) {
        halt();
    }
}
