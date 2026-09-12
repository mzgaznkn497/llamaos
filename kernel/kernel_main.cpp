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

    extern uintptr_t __stack_chk_guard;

    [[noreturn]] void kernel_main(uint64_t mb2_magic, uint64_t mb2_info_virt);
}

namespace llamaos {

using arch::x86_64::g_cpu_info;
using arch::x86_64::halt;
using arch::x86_64::outb;
using arch::x86_64::read_cr3;
using arch::x86_64::rdmsr;
using arch::x86_64::Msr;
using boot::g_boot_info;
using boot::MemoryType;
using boot::FramebufferType;
using drivers::SerialPort;
using drivers::VgaConsole;
using drivers::VgaColor;

static inline uintptr_t get_current_rip() {
    uintptr_t rip;
    asm volatile("lea (%%rip), %0" : "=r"(rip));
    return rip;
}

static inline uintptr_t get_current_rsp() {
    uintptr_t rsp;
    asm volatile("mov %%rsp, %0" : "=r"(rsp));
    return rsp;
}

static inline uint16_t get_cs() {
    uint16_t cs;
    asm volatile("mov %%cs, %0" : "=r"(cs));
    return cs;
}

static inline uint16_t get_ds() {
    uint16_t ds;
    asm volatile("mov %%ds, %0" : "=r"(ds));
    return ds;
}

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
    kprint(" LlamaOS/A Kernel [x86-64] - Milestone 1: Bootable Foundation (Audited)\n");
    kprint(" Build Target: x86_64-elf | Language: C++20 Freestanding\n");
    kprint(" Mode: IA-32e Long Mode (64-bit) | Paging: 4-Level Higher-Half (-2 GiB)\n");
    kprint("================================================================================\n\n");
    VgaConsole::set_color(VgaColor::LightGray, VgaColor::Black);
}

static bool verify_higher_half_and_paging() {
    // 1. Instruction Pointer must reside in higher half
    uintptr_t rip = get_current_rip();
    if (rip < KERNEL_VIRTUAL_BASE) {
        klog_error("Higher-half check failed: RIP (%p) below KERNEL_VIRTUAL_BASE (%p).", rip, KERNEL_VIRTUAL_BASE);
        return false;
    }

    // 2. Stack pointer must reside within the allocated kernel stack range
    uintptr_t rsp = get_current_rsp();
    uintptr_t stack_bottom = reinterpret_cast<uintptr_t>(kernel_stack_bottom);
    uintptr_t stack_top = reinterpret_cast<uintptr_t>(kernel_stack_top);
    if (rsp < stack_bottom || rsp > stack_top) {
        klog_error("Higher-half check failed: RSP (%p) outside kernel stack [%p - %p].", rsp, stack_bottom, stack_top);
        return false;
    }

    // 3. Segment selectors must correspond to 64-bit GDT entries
    uint16_t cs = get_cs();
    uint16_t ds = get_ds();
    if (cs != 0x08 || ds != 0x10) {
        klog_error("Higher-half check failed: CS (%04x != 0x08) or DS (%04x != 0x10) incorrect.", cs, ds);
        return false;
    }

    // 4. MSR EFER verification: Long Mode Active (bit 10) and NXE (bit 11)
    uint64_t efer = rdmsr(Msr::Efer);
    if ((efer & (1 << 10)) == 0 || (efer & (1 << 11)) == 0) {
        klog_error("Higher-half check failed: IA32_EFER (%p) missing LMA or NXE.", efer);
        return false;
    }

    // 5. Hardware Page Table walk verification
    uintptr_t cr3 = read_cr3();
    if (cr3 == 0) {
        klog_error("Higher-half check failed: CR3 root pointer is null.");
        return false;
    }

    // PML4 entry 511 (maps top 512 GiB)
    uintptr_t pml4_virt = phys_to_virt(cr3 & ~0xFFFULL);
    uint64_t pml4_entry = *reinterpret_cast<const uint64_t*>(pml4_virt + 511 * 8);
    if ((pml4_entry & 0x01) == 0) {
        klog_error("Higher-half check failed: PML4[511] not present.");
        return false;
    }

    // PDPT entry 510 (maps -2 GiB)
    uintptr_t pdpt_virt = phys_to_virt(pml4_entry & 0x000FFFFFFFFFF000ULL);
    uint64_t pdpt_entry = *reinterpret_cast<const uint64_t*>(pdpt_virt + 510 * 8);
    if ((pdpt_entry & 0x01) == 0) {
        klog_error("Higher-half check failed: PDPT[510] not present.");
        return false;
    }

    // PD entry 0 (2 MiB huge page)
    uintptr_t pd_virt = phys_to_virt(pdpt_entry & 0x000FFFFFFFFFF000ULL);
    uint64_t pd_entry = *reinterpret_cast<const uint64_t*>(pd_virt + 0 * 8);
    if ((pd_entry & 0x83) != 0x83) { // Present (bit 0) | Writable (bit 1) | PageSize (bit 7)
        klog_error("Higher-half check failed: PD[0] (%p) does not match expected 2MB huge page (0x83).", pd_entry);
        return false;
    }

    return true;
}

static bool test_memory_primitives() {
    // Basic copy/set/cmp
    char buf1[32] = "LlamaOS/A Verification";
    char buf2[32];
    memset(buf2, 0, sizeof(buf2));
    memcpy(buf2, buf1, strlen(buf1) + 1);
    if (strcmp(buf1, buf2) != 0 || memcmp(buf1, buf2, strlen(buf1) + 1) != 0) {
        return false;
    }

    // Overlapping memmove forward
    char move_buf[16] = "abcdefghij";
    memmove(move_buf + 2, move_buf, 6); // "ababcdef"
    if (strncmp(move_buf, "ababcd", 6) != 0) {
        return false;
    }

    // Overlapping memmove backward
    char move_buf2[16] = "0123456789";
    memmove(move_buf2, move_buf2 + 2, 6); // "234567"
    if (strncmp(move_buf2, "234567", 6) != 0) {
        return false;
    }

    return true;
}

static bool run_audited_self_tests() {
    klog_info("Executing audited Phase 1 runtime verification suite...");

    // Test 1: CPU Architecture Verification
    if (!g_cpu_info.features.lm || !g_cpu_info.features.nx || !g_cpu_info.features.pae) {
        klog_error("Self-Test Failed: Essential CPU feature missing (LM=%b, NX=%b, PAE=%b).",
                   g_cpu_info.features.lm, g_cpu_info.features.nx, g_cpu_info.features.pae);
        return false;
    }
    klog_info(" [PASS] CPU Architecture & Security Extensions (LM, NX, PAE confirmed)");

    // Test 2: Freestanding Memory Primitives
    if (!test_memory_primitives()) {
        klog_error("Self-Test Failed: Memory primitive verification failed.");
        return false;
    }
    klog_info(" [PASS] Freestanding Memory Primitives (memcpy, memmove forward/back, memset, strcmp)");

    // Test 3: Higher-Half Runtime & MMU Verification
    if (!verify_higher_half_and_paging()) {
        klog_error("Self-Test Failed: Higher-half or hardware paging validation failed.");
        return false;
    }
    klog_info(" [PASS] Higher-Half Runtime Execution & MMU Paging (RIP, RSP, CS/DS, PML4->PDPT->PD confirmed)");

    // Test 4: Multiboot2 Parser & Memory Map Validation
    if (!g_boot_info.valid) {
        klog_error("Self-Test Failed: Multiboot2 information block invalid or unverified.");
        return false;
    }
    if (g_boot_info.mmap_count == 0 || g_boot_info.total_usable_ram_bytes == 0) {
        klog_error("Self-Test Failed: Memory map empty or usable RAM is 0.");
        return false;
    }
    klog_info(" [PASS] Bootloader Protocol & Memory Map Integrity (Multiboot2 valid, %llu bytes usable RAM)",
              g_boot_info.total_usable_ram_bytes);

    // Test 5: Command-Line Tokenizer Integrity
    // Verify that token checking is exact and robust
    if (g_boot_info.has_argument("nonexistent_test_arg_12345")) {
        klog_error("Self-Test Failed: False positive reported by command line parser.");
        return false;
    }
    klog_info(" [PASS] Command-Line Tokenizer Sanity");

    // Test 6: Stack Canary Security
    if (__stack_chk_guard == 0) {
        klog_error("Self-Test Failed: Stack canary guard is zero.");
        return false;
    }
    klog_info(" [PASS] Stack Protector Security Canary Active (%p)", __stack_chk_guard);

    klog_info("All Phase 1 audited self-tests passed successfully.");
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

    // Step 3: Parse and validate Multiboot2 bootloader data structures
    klog_info("Parsing Multiboot2 boot information at virtual address %p...", mb2_info_virt);
    const bool mb2_ok = g_boot_info.parse(mb2_magic, mb2_info_virt);

    if (mb2_ok) {
        klog_info("Bootloader identified : %s", g_boot_info.bootloader_name[0] ? g_boot_info.bootloader_name : "Unknown");
        klog_info("Kernel command line   : %s", g_boot_info.command_line[0] ? g_boot_info.command_line : "(none)");
        klog_info("Basic memory reported : Lower=%u KB, Upper=%u KB", g_boot_info.mem_lower_kb, g_boot_info.mem_upper_kb);
        klog_info("Total usable RAM      : %llu MiB (%llu bytes)",
                  g_boot_info.total_usable_ram_bytes / (1024 * 1024),
                  g_boot_info.total_usable_ram_bytes);

        if (g_boot_info.framebuffer.valid) {
            const char* fb_type_str = "Unknown";
            if (g_boot_info.framebuffer.type == FramebufferType::DirectRgb) fb_type_str = "Direct RGB";
            else if (g_boot_info.framebuffer.type == FramebufferType::Indexed) fb_type_str = "Indexed";
            else if (g_boot_info.framebuffer.type == FramebufferType::EgaText) fb_type_str = "EGA Text";

            klog_info("Framebuffer detected  : %ux%u @ %u bpp (%s, addr=%p, pitch=%u)",
                      g_boot_info.framebuffer.width,
                      g_boot_info.framebuffer.height,
                      g_boot_info.framebuffer.bpp,
                      fb_type_str,
                      g_boot_info.framebuffer.address,
                      g_boot_info.framebuffer.pitch);
        }

        if (g_boot_info.acpi.valid) {
            klog_info("ACPI RSDP found       : Revision=%u, OEM='%.6s', Addr=%p",
                      g_boot_info.acpi.revision,
                      g_boot_info.acpi.oem_id,
                      g_boot_info.acpi.rsdp_addr);
        }

        if (g_boot_info.efi.present) {
            klog_info("UEFI System Table     : Physical Addr=%p", g_boot_info.efi.system_table_paddr);
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
        klog_info("Physical Memory Map (%u regions captured, %u detected%s):",
                  static_cast<uint32_t>(g_boot_info.mmap_count),
                  static_cast<uint32_t>(g_boot_info.total_mmap_entries_detected),
                  g_boot_info.mmap_truncated ? " [TRUNCATED]" : "");

        for (size_t i = 0; i < g_boot_info.mmap_count && i < 8; ++i) {
            const auto& e = g_boot_info.mmap_entries[i];
            klog_info("  [%02u] Base: %p, Len: %p (%llu MiB) - %s",
                      static_cast<uint32_t>(i),
                      e.base_addr,
                      e.length,
                      e.length / (1024 * 1024),
                      g_boot_info.memory_type_to_string(static_cast<MemoryType>(e.type)));
        }
        if (g_boot_info.mmap_count > 8) {
            klog_info("  ... and %u more regions.", static_cast<uint32_t>(g_boot_info.mmap_count - 8));
        }
    }

    // Step 7: Run verification self-tests
    const bool tests_passed = run_audited_self_tests();
    if (!tests_passed) {
        KPANIC("Self-test suite failure detected during Phase 1 boot!");
    }

    // Step 8: Handle automated test harness termination via token parsing
    const bool automated_test_mode = g_boot_info.is_test_mode();

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
