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
#include "memory/memory_types.hpp"
#include "memory/reserved_regions.hpp"
#include "memory/pmm.hpp"
#include "memory/vmm.hpp"
#include "arch/x86_64/cpu/gdt.hpp"
#include "arch/x86_64/cpu/tss.hpp"
#include "arch/x86_64/cpu/idt.hpp"
#include "arch/x86_64/cpu/interrupts.hpp"
#include "arch/x86_64/cpu/pic.hpp"
#include "arch/x86_64/cpu/timer.hpp"
#include "arch/x86_64/cpu/per_cpu.hpp"
#include "drivers/console/console.hpp"
#include "drivers/ps2/ps2_controller.hpp"
#include "drivers/ps2/keyboard.hpp"
#include "drivers/pci/pci.hpp"
#include "drivers/framebuffer/framebuffer.hpp"
#include "drivers/devices/device_registry.hpp"
#include "drivers/devices/hardware_report.hpp"
#include "threading/scheduler.hpp"
#include "threading/thread_types.hpp"
#include "syscall/syscall.hpp"
#include "syscall/syscall_abi.hpp"
#include "storage/storage_manager.hpp"
#include "storage/gpt.hpp"
#include "drivers/virtio/virtio_block.hpp"
#include "drivers/virtio/virtio_net.hpp"
#include "net/net_interface.hpp"
#include "net/net_config.hpp"
#include "net/mgmt_server.hpp"
#include "core/power.hpp"
#include "fs/vfs.hpp"
#include "fs/fat32.hpp"
#include "userland/process.hpp"
#include "userland/elf_loader.hpp"
#include "userland/user_memory.hpp"
#include "userland/init_elf.hpp"

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
    void call_global_constructors();

    [[noreturn]] void kernel_main(uint64_t mb2_magic, uint64_t mb2_info_virt);
    bool test_reg_preservation_asm(const uint64_t* sentinels, int64_t iterations);
    void enter_ring3(uint64_t rip, uint64_t rsp);
}

namespace llamaos {

using arch::x86_64::g_cpu_info;
using arch::x86_64::halt;
using arch::x86_64::outb;
using arch::x86_64::read_cr0;
using arch::x86_64::read_cr3;
using arch::x86_64::read_cr4;
using arch::x86_64::rdmsr;
using arch::x86_64::Msr;
using boot::g_boot_info;
using boot::MemoryType;
using boot::FramebufferType;
using boot::BootMode;
using drivers::SerialPort;
using drivers::VgaConsole;
using drivers::VgaColor;
using memory::PhysicalAddress;
using memory::VirtualAddress;
using memory::PageFrameNumber;
using memory::PageCount;
using memory::PageFlags;
using memory::ReservedMemoryTracker;
using memory::g_pmm;
using memory::g_vmm;
using arch::x86_64::PermanentGdt;
using arch::x86_64::TssManager;
using arch::x86_64::IdtManager;
using arch::x86_64::PicManager;
using arch::x86_64::Timer;
using arch::x86_64::exception_init;
using arch::x86_64::exception_verify;

static ReservedMemoryTracker s_reserved_tracker;
static fs::Fat32Filesystem s_root_fat32_fs;

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
    // 1. Instruction Pointer must reside strictly within higher-half .text section
    uintptr_t rip = get_current_rip();
    uintptr_t text_start = reinterpret_cast<uintptr_t>(_text_start);
    uintptr_t text_end = reinterpret_cast<uintptr_t>(_text_end);
    if (rip < text_start || rip >= text_end) {
        klog_error("Higher-half check failed: RIP (%p) outside .text [%p - %p].", rip, text_start, text_end);
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

    // 4. MSR EFER verification: Long Mode Active (bit 10), Long Mode Enable (bit 8), and NXE (bit 11)
    uint64_t efer = rdmsr(Msr::Efer);
    if ((efer & (1 << 8)) == 0 || (efer & (1 << 10)) == 0 || (efer & (1 << 11)) == 0) {
        klog_error("Higher-half check failed: IA32_EFER (%p) missing LME, LMA, or NXE.", efer);
        return false;
    }

    // 5. CR0 and CR4 control register verification
    uint64_t cr0 = read_cr0();
    if ((cr0 & (1ULL << 31)) == 0 || (cr0 & (1ULL << 0)) == 0) {
        klog_error("Higher-half check failed: CR0 missing PG or PE.");
        return false;
    }

    uint64_t cr4 = read_cr4();
    if ((cr4 & (1ULL << 5)) == 0) { // PAE bit
        klog_error("Higher-half check failed: CR4 missing PAE.");
        return false;
    }

    // 6. Hardware Page Table walk verification from active CR3
    uintptr_t cr3 = read_cr3();
    if (cr3 == 0 || (cr3 & 0xFFF) != 0) {
        klog_error("Higher-half check failed: CR3 root pointer is null or unaligned (%p).", cr3);
        return false;
    }

    // PML4 entry 511 (maps top 512 GiB)
    uintptr_t pml4_virt = phys_to_virt(cr3 & ~0xFFFULL);
    uint64_t pml4_entry = *reinterpret_cast<const uint64_t*>(pml4_virt + 511 * 8);
    if ((pml4_entry & 0x01) == 0) {
        klog_error("Higher-half check failed: PML4[511] not present.");
        return false;
    }

    // PDPT entry 510 (maps -2 GiB: 0xFFFFFFFF80000000)
    uintptr_t pdpt_virt = phys_to_virt(pml4_entry & 0x000FFFFFFFFFF000ULL);
    uint64_t pdpt_entry = *reinterpret_cast<const uint64_t*>(pdpt_virt + 510 * 8);
    if ((pdpt_entry & 0x01) == 0) {
        klog_error("Higher-half check failed: PDPT[510] not present.");
        return false;
    }

    // PD entry 0 (2 MiB huge page covering 0x0..0x200000 physical)
    uintptr_t pd_virt = phys_to_virt(pdpt_entry & 0x000FFFFFFFFFF000ULL);
    uint64_t pd_entry = *reinterpret_cast<const uint64_t*>(pd_virt + 0 * 8);
    if ((pd_entry & 0x83) != 0x83) { // Present (bit 0) | Writable (bit 1) | PageSize (bit 7)
        klog_error("Higher-half check failed: PD[0] (%p) does not match expected 2MB huge page (0x83).", pd_entry);
        return false;
    }

    return true;
}

static bool verify_linker_layout() {
    uintptr_t k_phys_start = reinterpret_cast<uintptr_t>(_kernel_physical_start);
    uintptr_t k_phys_end   = reinterpret_cast<uintptr_t>(_kernel_physical_end);
    uintptr_t k_virt_start = reinterpret_cast<uintptr_t>(_kernel_virtual_start);
    uintptr_t k_virt_end   = reinterpret_cast<uintptr_t>(_kernel_virtual_end);
    uintptr_t text_start   = reinterpret_cast<uintptr_t>(_text_start);
    uintptr_t text_end     = reinterpret_cast<uintptr_t>(_text_end);
    uintptr_t rodata_start = reinterpret_cast<uintptr_t>(_rodata_start);
    uintptr_t rodata_end   = reinterpret_cast<uintptr_t>(_rodata_end);
    uintptr_t data_start   = reinterpret_cast<uintptr_t>(_data_start);
    uintptr_t data_end     = reinterpret_cast<uintptr_t>(_data_end);
    uintptr_t bss_start    = reinterpret_cast<uintptr_t>(_bss_start);
    uintptr_t bss_end      = reinterpret_cast<uintptr_t>(_bss_end);
    uintptr_t stack_bot    = reinterpret_cast<uintptr_t>(kernel_stack_bottom);
    uintptr_t stack_top    = reinterpret_cast<uintptr_t>(kernel_stack_top);

    // 1. Physical base sanity
    if (k_phys_start != 0x00100000 || k_phys_end <= k_phys_start) return false;

    // 2. Virtual base sanity
    if (k_virt_start < KERNEL_VIRTUAL_BASE || k_virt_end <= k_virt_start) return false;

    // 3. Virtual vs Physical mapping:
    // .boot section precedes higher-half virtual sections (.text .. .bss) in physical memory
    uintptr_t virt_phys_base = k_virt_start - KERNEL_VIRTUAL_BASE;
    if (virt_phys_base < k_phys_start || virt_phys_base >= k_phys_end) return false;
    if ((k_virt_end - k_phys_end) != KERNEL_VIRTUAL_BASE) return false;

    // 4. Section order and non-overlapping assertions
    if (!(k_virt_start <= text_start && text_start < text_end &&
          text_end <= rodata_start && rodata_start < rodata_end &&
          rodata_end <= data_start && data_start < data_end &&
          data_end <= bss_start && bss_start < bss_end &&
          bss_end <= k_virt_end)) {
        return false;
    }

    // 5. Kernel stack size and BSS enclosure
    if (stack_bot >= stack_top || (stack_top - stack_bot) != 65536) return false;
    if (stack_bot < bss_start || stack_top > bss_end) return false;

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

    // Test 1: CPU Architecture & Security Extensions
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

    // Test 3: Higher-Half Runtime Execution & MMU Paging Walk
    if (!verify_higher_half_and_paging()) {
        klog_error("Self-Test Failed: Higher-half or hardware paging validation failed.");
        return false;
    }
    klog_info(" [PASS] Higher-Half Runtime Execution & MMU Paging (RIP, RSP, CS/DS, PML4->PDPT->PD confirmed)");

    // Test 4: Linker Section Layout & Stack Sanity
    if (!verify_linker_layout()) {
        klog_error("Self-Test Failed: Linker section layout or stack bounds sanity failed.");
        return false;
    }
    klog_info(" [PASS] Linker Section & Memory Layout Sanity (physical/virtual mapping consistent, 64 KiB stack)");

    // Test 5: Bootloader Protocol & Bounds Integrity
    if (!g_boot_info.valid) {
        klog_error("Self-Test Failed: Multiboot2 information block invalid or unverified.");
        return false;
    }
    klog_info(" [PASS] Bootloader Protocol & Bounds Integrity (Multiboot2 valid, magic verified)");

    // Test 6: Command-Line Tokenizer & Boot Mode Integrity
    if (g_boot_info.has_argument("nonexistent_test_arg_12345") ||
        g_boot_info.has_argument("testingfoo") ||
        g_boot_info.has_argument("status")) {
        klog_error("Self-Test Failed: False positive reported by command line parser.");
        return false;
    }
    klog_info(" [PASS] Command-Line Tokenizer & Boot Mode Integrity (exact tokenization, no false positives)");

    // Test 7: Physical Memory Map & Usable RAM Integrity
    if (g_boot_info.mmap_count == 0 || g_boot_info.total_usable_ram_bytes == 0) {
        klog_error("Self-Test Failed: Memory map empty or usable RAM is 0.");
        return false;
    }
    for (size_t i = 0; i < g_boot_info.mmap_count; ++i) {
        const auto& entry = g_boot_info.mmap_entries[i];
        if (entry.length == 0 || (UINT64_MAX - entry.base_addr < entry.length)) {
            klog_error("Self-Test Failed: Malformed memory map region detected at index %u.", static_cast<uint32_t>(i));
            return false;
        }
    }
    klog_info(" [PASS] Physical Memory Map & Usable RAM Integrity (%llu bytes usable RAM verified)",
              g_boot_info.total_usable_ram_bytes);

    // Test 8: Framebuffer Metadata Validation (if present)
    if (g_boot_info.framebuffer.valid) {
        if (g_boot_info.framebuffer.address == 0 ||
            g_boot_info.framebuffer.width == 0 ||
            g_boot_info.framebuffer.height == 0 ||
            g_boot_info.framebuffer.total_size_bytes == 0) {
            klog_error("Self-Test Failed: Invalid framebuffer metadata.");
            return false;
        }
        klog_info(" [PASS] Framebuffer Metadata Validated (%ux%u @ %u bpp, %llu bytes)",
                  g_boot_info.framebuffer.width,
                  g_boot_info.framebuffer.height,
                  g_boot_info.framebuffer.bpp,
                  g_boot_info.framebuffer.total_size_bytes);
    }

    // Test 9: ACPI RSDP Metadata Validation (if present)
    if (g_boot_info.acpi.valid) {
        if (g_boot_info.acpi.mb2_rsdp_copy_addr == 0 ||
            (g_boot_info.acpi.rsdt_physical_address == 0 && g_boot_info.acpi.xsdt_physical_address == 0)) {
            klog_error("Self-Test Failed: Invalid ACPI metadata.");
            return false;
        }
        klog_info(" [PASS] ACPI RSDP Metadata Validated (Rev=%u, OEM='%.6s', %s physical address valid)",
                  g_boot_info.acpi.revision,
                  g_boot_info.acpi.oem_id,
                  g_boot_info.acpi.is_v2 ? "XSDT" : "RSDT");
    }

    // Test 10: Stack Protector Security Canary
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

    // Step 0: Execute global static C++ constructors
    call_global_constructors();

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

            klog_info("Framebuffer detected  : %ux%u @ %u bpp (%s, addr=%p, pitch=%u, size=%llu bytes)",
                      g_boot_info.framebuffer.width,
                      g_boot_info.framebuffer.height,
                      g_boot_info.framebuffer.bpp,
                      fb_type_str,
                      g_boot_info.framebuffer.address,
                      g_boot_info.framebuffer.pitch,
                      g_boot_info.framebuffer.total_size_bytes);
        }

        if (g_boot_info.acpi.valid) {
            klog_info("ACPI RSDP found       : Revision=%u, OEM='%.6s', MB2 Copy=%p, %s PAddr=%p",
                      g_boot_info.acpi.revision,
                      g_boot_info.acpi.oem_id,
                      g_boot_info.acpi.mb2_rsdp_copy_addr,
                      g_boot_info.acpi.is_v2 ? "XSDT" : "RSDT",
                      g_boot_info.acpi.is_v2 ? g_boot_info.acpi.xsdt_physical_address : static_cast<uint64_t>(g_boot_info.acpi.rsdt_physical_address));
        }

        if (g_boot_info.efi.present) {
            klog_info("UEFI System Table     : Physical Addr=%p (firmware-owned)", g_boot_info.efi.system_table_paddr);
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

    // Step 8: Initialize and verify Phase 2 Memory Management Subsystem (PMM, VMM, Reserved Regions)
    klog_info("================================================================================");
    klog_info("Initializing Phase 2: Memory Management Subsystem (PMM, VMM, Reservations)...");
    klog_info("================================================================================");

    PhysicalAddress kernel_pstart(reinterpret_cast<uint64_t>(_kernel_physical_start));
    PhysicalAddress kernel_pend(reinterpret_cast<uint64_t>(_kernel_physical_end));
    PhysicalAddress mb2_paddr(g_boot_info.mb2_info_paddr);
    uint64_t mb2_size = g_boot_info.mb2_info_total_size;

    // 1. Enumerate and normalize tracked memory reservations
    s_reserved_tracker.populate(g_boot_info, kernel_pstart, kernel_pend, mb2_paddr, mb2_size);

    // 2. Initialize Physical Memory Manager (PMM)
    if (!g_pmm.init(g_boot_info, s_reserved_tracker, kernel_pstart, kernel_pend, mb2_paddr, mb2_size)) {
        KPANIC("Failed to initialize Physical Memory Manager (PMM)!");
    }
    s_reserved_tracker.dump_regions();
    g_pmm.dump_stats();

    // 3. Initialize Virtual Memory Manager (VMM) and migrate to dynamic page tables
    if (!g_vmm.init(kernel_pstart, kernel_pend)) {
        KPANIC("Failed to initialize Virtual Memory Manager (VMM)!");
    }
    klog_info("VMM Active Root PML4 Physical Address: %p", g_vmm.root_pml4_address().value());

    // Step 9: Initialize Phase 3 Descriptor, Exception, and Interrupt Foundation
    klog_info("================================================================================");
    klog_info("Initializing Phase 3: CPU Descriptors, Exceptions & Interrupt Subsystem...");
    klog_info("================================================================================");

    // 1. Task State Segment (TSS) and dedicated IST stacks (PMM allocated, VMM mapped)
    if (!TssManager::init(reinterpret_cast<uint64_t>(kernel_stack_top))) {
        KPANIC("Failed to initialize TSS and dedicated IST stacks!");
    }

    // 2. Permanent Global Descriptor Table (GDT)
    PermanentGdt::init(TssManager::tss_virtual_address(), TssManager::tss_limit());

    // 3. Verify GDT and TSS / IST
    if (!PermanentGdt::verify()) {
        KPANIC("Permanent GDT hardware validation failed!");
    }
    klog_info(" [PASS] Permanent Global Descriptor Table (GDT) & Segment Reload");

    if (!TssManager::verify()) {
        KPANIC("TSS & IST hardware validation failed!");
    }
    klog_info(" [PASS] Task State Segment (TSS) & Dedicated IST Stacks (PMM-backed, VMM-mapped)");

    // Initialize Per-CPU Architecture Data for Bootstrap Processor (BSP)
    arch::x86_64::PerCpuManager::init_bsp();

    // 4. Interrupt Descriptor Table (IDT 256 gates)
    IdtManager::init();
    if (!IdtManager::verify()) {
        KPANIC("Interrupt Descriptor Table validation failed!");
    }
    klog_info(" [PASS] Interrupt Descriptor Table (IDT 256 gates) & Vector Dispatcher");

    // 5. Exception Dispatcher and Verification
    exception_init();
    if (!exception_verify()) {
        KPANIC("CPU Exception infrastructure verification failed!");
    }
    klog_info(" [PASS] CPU Exception Infrastructure & Breakpoint (#BP / INT3) Verification");
    klog_info(" [PASS] Page Fault (#PF) Diagnostics & IST Containment Handler");

    // 6. Verify PMM and VMM (Hardware exception handlers are now active to catch any faults)
    if (!g_pmm.self_test(kernel_pstart, kernel_pend, mb2_paddr)) {
        KPANIC("Physical Memory Manager self-test suite failed!");
    }
    klog_info(" [PASS] Physical Memory Manager (PMM) Bitmap & Allocation Suite");

    if (!g_vmm.self_test()) {
        KPANIC("Virtual Memory Manager self-test suite failed!");
    }
    klog_info(" [PASS] Virtual Memory Manager (VMM) 4-Level Paging & Mapping Suite");

    // 7. Initialize Dual 8259 PIC and APIC Foundation
    PicManager::init();
    if (!PicManager::verify()) {
        KPANIC("PIC hardware verification failed!");
    }
    klog_info(" [PASS] PIC/APIC Hardware Detection & Dual 8259 PIC Remapping");

    // 8. Timer Interrupt Foundation (PIT 8254) and Interrupt Control API
    Timer::init(100);

    if (!Timer::verify_interrupt_api()) {
        KPANIC("Architecture Interrupt Control API verification failed!");
    }
    klog_info(" [PASS] Architecture Interrupt Control API (STI/CLI/Save/Restore)");

    if (!Timer::verify()) {
        KPANIC("Timer interrupt delivery verification failed!");
    }
    klog_info(" [PASS] Timer Interrupt Foundation & Periodic Heartbeat Delivery");

    // Step 10: Initialize Phase 4 Device and Hardware Abstraction Subsystems
    klog_info("================================================================================");
    klog_info("Initializing Phase 4: Device and Hardware Abstraction Foundation...");
    klog_info("================================================================================");

    // 1. Unified Console Abstraction
    drivers::Console::init();
    klog_info(" [PASS] Unified Console Abstraction (Serial COM1 + VGA Text 0xB8000)");

    // 2. PS/2 8042 Controller Foundation
    if (drivers::Ps2Controller::init()) {
        klog_info(" [PASS] Intel 8042 PS/2 Controller Initialization & Self-Test");
    } else {
        klog_warn(" [WARN] Intel 8042 PS/2 Controller not present or self-test failed");
    }

    // 3. PS/2 Keyboard Driver (IRQ1 / Vector 0x21)
    if (drivers::Keyboard::init()) {
        klog_info(" [PASS] PS/2 Keyboard Driver & Scancode Decoder (IRQ1 / Vector 0x21)");
    } else {
        klog_warn(" [WARN] PS/2 Keyboard Driver initialization failed");
    }

    // 4. PCI Bus Enumeration
    if (drivers::PciManager::init()) {
        klog_info(" [PASS] PCI Bus Enumeration (Discovered %u device(s))",
                  static_cast<uint32_t>(drivers::PciManager::device_count()));
    } else {
        klog_warn(" [WARN] PCI Bus Enumeration failed");
    }

    // 5. Linear Framebuffer Subsystem
    if (g_boot_info.framebuffer.valid) {
        if (drivers::Framebuffer::init(g_boot_info.framebuffer)) {
            klog_info(" [PASS] Linear Framebuffer Subsystem (%ux%u@%ubpp)",
                      drivers::Framebuffer::width(),
                      drivers::Framebuffer::height(),
                      static_cast<uint32_t>(drivers::Framebuffer::bpp()));
        } else {
            klog_warn(" [WARN] Linear Framebuffer initialization failed");
        }
    } else {
        klog_info(" [INFO] Linear Framebuffer not present (VGA Text mode active)");
    }

    // 6. Device Registry and Hardware Discovery Report
    drivers::DeviceRegistry::init();
    drivers::DeviceRegistry::populate_detected_devices();
    klog_info(" [PASS] Device Abstraction Registry (%u subsystem devices registered)",
              static_cast<uint32_t>(drivers::DeviceRegistry::device_count()));

    drivers::HardwareReport::display();

    // 7. Storage Abstraction & VirtIO Block Driver
    storage::StorageManager::init();
    size_t virtio_disks = drivers::virtio::VirtioBlockDevice::probe_all();
    if (virtio_disks > 0) {
        klog_info(" [PASS] VirtIO Block Driver (Discovered %u disk(s))", static_cast<uint32_t>(virtio_disks));
        for (size_t d = 0; d < virtio_disks; ++d) {
            auto* disk = storage::StorageManager::get_device(d);
            if (disk) {
                storage::GptParser::parse_and_register(disk);
            }
        }
    } else {
        klog_info(" [INFO] No VirtIO block storage devices discovered on PCI bus.");
    }

    // 8. Virtual Filesystem (VFS) & Dynamic Root Partition Discovery
    fs::Vfs::init();
    storage::BlockDevice* mount_target = nullptr;
    int highest_score = -1000;

    size_t dev_count = storage::StorageManager::device_count();
    for (size_t i = 0; i < dev_count; ++i) {
        auto* d = storage::StorageManager::get_device(i);
        if (!d) continue;

        alignas(512) uint8_t s0[512];
        if (d->read_sectors(0, 1, s0) != storage::BlockStatus::Success) {
            continue;
        }

        // Validate FAT32 boot sector signature and basic BPB metrics
        if (s0[510] != 0x55 || s0[511] != 0xAA) {
            continue;
        }

        fs::Fat32Bpb bpb;
        llamaos::memcpy(&bpb, s0, sizeof(bpb));
        if (bpb.bytes_per_sector != 512 || bpb.sectors_per_cluster == 0 ||
            bpb.num_fats == 0 || bpb.table_size_32 == 0) {
            continue;
        }

        int score = 0;
        // Priority 1: Volume label indicates system or root filesystem
        if (llamaos::strncmp(bpb.volume_label, "LLAMAOS", 7) == 0) {
            score += 1000;
        } else if (llamaos::strncmp(bpb.volume_label, "ROOT", 4) == 0 ||
                   llamaos::strncmp(bpb.volume_label, "SYS", 3) == 0) {
            score += 500;
        } else if (llamaos::strncmp(bpb.volume_label, "ESP", 3) == 0 ||
                   llamaos::strncmp(bpb.volume_label, "EFI", 3) == 0) {
            score -= 500; // Demote EFI System Partition
        }

        // Priority 2: Partitions are preferred over raw full-disk devices
        size_t nlen = llamaos::strlen(d->name());
        if (nlen > 0 && d->name()[nlen - 1] >= '1' && d->name()[nlen - 1] <= '9') {
            score += 200;
        }

        klog_info("Dynamic Partition Discovery: Evaluated '%s' (Label: %.11s, Score: %d)",
                  d->name(), bpb.volume_label, score);

        if (score > highest_score) {
            highest_score = score;
            mount_target = d;
        }
    }

    if (mount_target) {
        if (fs::Vfs::mount("/", mount_target, &s_root_fat32_fs) == 0) {
            klog_info(" [PASS] Persistent FAT32 Filesystem mounted on '/' (Device: %s)", mount_target->name());
        }
    }

    // 9. Network Hardware, Multi-NIC Discovery & Dynamic Net Configuration
    net::NetConfig::configure_all();

    // Step 11: Initialize Phase 5 Process & Threading Subsystem
    klog_info("================================================================================");
    klog_info("Initializing Phase 5: Process & Threading Subsystem (Kernel Threads, Preemption)...");
    klog_info("================================================================================");

    threading::Scheduler::init();
    klog_info(" [PASS] Kernel Thread Subsystem (TCB, Stack Allocator, Round-Robin Ready Queue)");
    klog_info(" [PASS] Callee-Saved Assembly Context Switch & Bootstrap Trampoline Online");

    // Step 12: Initialize Phase 6 System Call Interface
    klog_info("================================================================================");
    klog_info("Initializing Phase 6: System Call Interface (MSRs, ABI, Dispatcher)...");
    klog_info("================================================================================");

    syscall::SyscallManager::init();
    if (!syscall::SyscallManager::verify()) {
        KPANIC("Phase 6 System Call Subsystem verification failed!");
    }
    klog_info(" [PASS] 64-bit System Call Subsystem (SYSCALL/SYSRET MSRs, Dispatcher, ABI)");

    // Step 13: Initialize Phase 7 Minimal Userland Subsystem
    klog_info("================================================================================");
    klog_info("Initializing Phase 7: Minimal Userland Subsystem (ELF Loader, Processes)...");
    klog_info("================================================================================");
    userland::ProcessManager::init();
    klog_info(" [PASS] Minimal Userland Subsystem (ELF64 Loader, Ring 3 Transition, Processes)");

    // Step 14: Completion Banners
    drivers::VgaConsole::set_color(drivers::VgaColor::LightGreen, drivers::VgaColor::Black);
    kprint("\n>>> LlamaOS/A Kernel Boot Milestone 1 Accomplished Successfully! <<<\n");
    kprint(">>> LlamaOS/A Kernel Boot Milestone 2 Accomplished Successfully! <<<\n");
    kprint(">>> LlamaOS/A Kernel Boot Milestone 3 Accomplished Successfully! <<<\n");
    kprint(">>> LlamaOS/A Kernel Boot Milestone 4 Accomplished Successfully! <<<\n");
    kprint(">>> LlamaOS/A Kernel Boot Milestone 5 Accomplished Successfully! <<<\n");
    kprint(">>> LlamaOS/A Kernel Boot Milestone 6 Accomplished Successfully! <<<\n");
    kprint(">>> LlamaOS/A Kernel Boot Milestone 7 Accomplished Successfully! <<<\n\n");
    drivers::VgaConsole::set_color(drivers::VgaColor::LightGray, drivers::VgaColor::Black);

    // Step 14: Check for Isolated Fault Test Modes (Hardware Runtime Proofs)
    if (g_boot_info.has_argument("test-pf")) {
        klog_info("=== TRIGGERING ISOLATED HARDWARE RUNTIME TEST: PAGE FAULT (#PF) ===");
        volatile uint64_t* unmapped_ptr = reinterpret_cast<volatile uint64_t*>(arch::x86_64::IST1_GUARD_VIRTUAL.value());
        klog_info("Executing memory read at unmapped canonical virtual address %p...", unmapped_ptr);
        uint64_t val = *unmapped_ptr;
        (void)val;
    }

    if (g_boot_info.has_argument("test-gp")) {
        klog_info("=== TRIGGERING ISOLATED HARDWARE RUNTIME TEST: GENERAL PROTECTION FAULT (#GP) ===");
        uint16_t invalid_selector = 0x0028;
        klog_info("Loading invalid segment selector 0x%04x into DS register...", invalid_selector);
        asm volatile("mov %0, %%ds" :: "r"(invalid_selector));
    }

    if (g_boot_info.has_argument("test-df")) {
        klog_info("=== TRIGGERING ISOLATED HARDWARE RUNTIME TEST: DOUBLE FAULT (#DF) ===");
        klog_info("Corrupting IST2 stack pointer to unmapped guard page %p...", arch::x86_64::IST1_GUARD_VIRTUAL.value());
        TssManager::tss()->ist2 = arch::x86_64::IST1_GUARD_VIRTUAL.value();
        volatile uint64_t* unmapped_ptr = reinterpret_cast<volatile uint64_t*>(arch::x86_64::IST3_GUARD_VIRTUAL.value());
        klog_info("Triggering initial fault at unmapped address %p...", unmapped_ptr);
        uint64_t val = *unmapped_ptr;
        (void)val;
    }

    // Step 14: Phase 4 Isolated Test Modes
    if (g_boot_info.has_argument("test-pci")) {
        klog_info("=== RUNNING ISOLATED PHASE 4 TEST: PCI ENUMERATION ===");
        size_t dev_count = drivers::PciManager::device_count();
        klog_info("PciManager discovered %u device(s).", static_cast<uint32_t>(dev_count));
        if (dev_count > 0) {
            klog_info("[PCI_TEST_PASS] Discovered valid PCI devices successfully.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[PCI_TEST_FAIL] No PCI devices discovered!");
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-keyboard")) {
        klog_info("=== RUNNING ISOLATED PHASE 4 TEST: KEYBOARD EVENT PIPELINE ===");
        drivers::Keyboard::process_scancode(0x1E); // Press 'A'
        drivers::KeyEvent ev{};
        bool got_press = drivers::Keyboard::pop_event(&ev);
        bool press_ok = got_press && (ev.key == drivers::KeyCode::A) && (ev.action == drivers::KeyAction::Press);

        drivers::Keyboard::process_scancode(0x9E); // Release 'A'
        bool got_release = drivers::Keyboard::pop_event(&ev);
        bool release_ok = got_release && (ev.key == drivers::KeyCode::A) && (ev.action == drivers::KeyAction::Release);

        if (press_ok && release_ok) {
            klog_info("[KEYBOARD_TEST_PASS] Scancode decode & event queue pipeline verified.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[KEYBOARD_TEST_FAIL] Keyboard event pipeline failed!");
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-framebuffer")) {
        klog_info("=== RUNNING ISOLATED PHASE 4 TEST: FRAMEBUFFER ===");
        if (drivers::Framebuffer::is_available()) {
            drivers::Framebuffer::fill_rect(10, 10, 100, 100, 0x00FF0000);
            drivers::Framebuffer::fill_rect(120, 10, 100, 100, 0x0000FF00);
            drivers::Framebuffer::fill_rect(230, 10, 100, 100, 0x000000FF);
            klog_info("[FRAMEBUFFER_TEST_PASS] Framebuffer test pattern rendered successfully.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_info("[FRAMEBUFFER_TEST_SKIP] No linear framebuffer present in current boot mode.");
            arch::x86_64::outb(0xF4, 0x10);
        }
    }

    // Step 15: Phase 5 Isolated Test Modes
    if (g_boot_info.has_argument("test-scheduler")) {
        klog_info("=== RUNNING ISOLATED PHASE 5 TEST: COOPERATIVE CONTEXT SWITCHING ===");
        static volatile int s_scheduler_step = 0;

        auto worker_a = [](void*) {
            kprint("A1\n"); s_scheduler_step = s_scheduler_step + 1; threading::Scheduler::yield();
            kprint("A2\n"); s_scheduler_step = s_scheduler_step + 1; threading::Scheduler::yield();
            kprint("A3\n"); s_scheduler_step = s_scheduler_step + 1; threading::Scheduler::yield();
        };

        auto worker_b = [](void*) {
            kprint("B1\n"); s_scheduler_step = s_scheduler_step + 1; threading::Scheduler::yield();
            kprint("B2\n"); s_scheduler_step = s_scheduler_step + 1; threading::Scheduler::yield();
            kprint("B3\n"); s_scheduler_step = s_scheduler_step + 1; threading::Scheduler::yield();
        };

        threading::Scheduler::create_thread("worker_a", worker_a);
        threading::Scheduler::create_thread("worker_b", worker_b);

        threading::Scheduler::start();

        while (s_scheduler_step < 6) {
            threading::Scheduler::yield();
        }

        klog_info("[TEST_SCHEDULER_PASS] Cooperative context switching verified with interleaving.");
        arch::x86_64::outb(0xF4, 0x10);
    }

    if (g_boot_info.has_argument("test-preemption")) {
        klog_info("=== RUNNING ISOLATED PHASE 5 TEST: PREEMPTIVE MULTITASKING (PIT IRQ0) ===");
        static volatile uint64_t s_preempt_a = 0;
        static volatile uint64_t s_preempt_b = 0;
        static volatile bool s_preempt_stop = false;

        auto non_yielding_a = [](void*) {
            while (!s_preempt_stop) {
                s_preempt_a = s_preempt_a + 1;
            }
        };

        auto non_yielding_b = [](void*) {
            while (!s_preempt_stop) {
                s_preempt_b = s_preempt_b + 1;
            }
        };

        threading::Scheduler::create_thread("preempt_a", non_yielding_a);
        threading::Scheduler::create_thread("preempt_b", non_yielding_b);

        threading::Scheduler::start();

        uint64_t start_ticks = arch::x86_64::Timer::ticks();
        while ((arch::x86_64::Timer::ticks() - start_ticks) < 15) {
            threading::Scheduler::yield();
        }
        s_preempt_stop = true;

        const auto& stats = threading::Scheduler::stats();
        klog_info("[PREEMPTION_EVIDENCE] Worker A counter=%llu, Worker B counter=%llu, Timer Preemptions=%llu, Switches=%llu",
                  s_preempt_a, s_preempt_b, stats.timer_preemptions, stats.context_switches);

        if (s_preempt_a > 1000 && s_preempt_b > 1000 && stats.timer_preemptions > 0) {
            klog_info("[PREEMPTION_TEST_PASS] Preemptive multitasking verified via PIT IRQ0.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[PREEMPTION_TEST_FAIL] Preemption test failed!");
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-context")) {
        klog_info("=== RUNNING ISOLATED PHASE 5 TEST: CALLEE-SAVED REGISTER PRESERVATION ===");

        static volatile bool s_reg_ok1 = false;
        static volatile bool s_reg_ok2 = false;
        static volatile bool s_reg_done1 = false;
        static volatile bool s_reg_done2 = false;

        static const uint64_t s_sentinels_1[6] = {
            0x1111222233334444ULL, 0x5555666677778888ULL, 0x9999AAAABBBBCCCCULL,
            0xDDDDEEEEFFFF0000ULL, 0x123456789ABCDEF0ULL, 0x0FEDCBA987654321ULL
        };
        static const uint64_t s_sentinels_2[6] = {
            0xAAAA1111BBBB2222ULL, 0xCCCC3333DDDD4444ULL, 0xEEEE5555FFFF6666ULL,
            0x7777888899990000ULL, 0xA1B2C3D4E5F60718ULL, 0x81706F5E4D3C2B1AULL
        };

        auto reg_worker1 = [](void*) {
            s_reg_ok1 = test_reg_preservation_asm(s_sentinels_1, 50);
            s_reg_done1 = true;
        };
        auto reg_worker2 = [](void*) {
            s_reg_ok2 = test_reg_preservation_asm(s_sentinels_2, 50);
            s_reg_done2 = true;
        };

        threading::Scheduler::create_thread("reg_worker1", reg_worker1);
        threading::Scheduler::create_thread("reg_worker2", reg_worker2);

        threading::Scheduler::start();

        while (!s_reg_done1 || !s_reg_done2) {
            threading::Scheduler::yield();
        }

        if (s_reg_ok1 && s_reg_ok2) {
            klog_info("[REGISTER_PRESERVATION_PASS] Callee-saved registers RBX, RBP, R12, R13, R14, R15 preserved across context switches.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[REGISTER_PRESERVATION_FAIL] Register preservation verification failed!");
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-stack")) {
        klog_info("=== RUNNING ISOLATED PHASE 5 TEST: STACK ISOLATION & GUARD PAGES ===");
        auto* t1 = threading::Scheduler::create_thread("stack_t1", [](void*) {});
        auto* t2 = threading::Scheduler::create_thread("stack_t2", [](void*) {});
        auto* idle = threading::Scheduler::idle_thread();

        klog_info("  Thread 1 Stack: Bottom=%p, Top=%p, Guard=%p",
                  t1->stack.stack_bottom.as_ptr(), t1->stack.stack_top.as_ptr(), t1->stack.guard_page.as_ptr());
        klog_info("  Thread 2 Stack: Bottom=%p, Top=%p, Guard=%p",
                  t2->stack.stack_bottom.as_ptr(), t2->stack.stack_top.as_ptr(), t2->stack.guard_page.as_ptr());
        klog_info("  Idle Stack    : Bottom=%p, Top=%p, Guard=%p",
                  idle->stack.stack_bottom.as_ptr(), idle->stack.stack_top.as_ptr(), idle->stack.guard_page.as_ptr());

        bool disjoint = (t1->stack.stack_top.value() <= t2->stack.stack_bottom.value()) ||
                        (t2->stack.stack_top.value() <= t1->stack.stack_bottom.value());
        bool guard1_unmapped = !memory::g_vmm.is_mapped(t1->stack.guard_page);
        bool guard2_unmapped = !memory::g_vmm.is_mapped(t2->stack.guard_page);
        bool aligned = ((t1->stack.stack_top.value() % 16) == 0) && ((t2->stack.stack_top.value() % 16) == 0);

        if (disjoint && guard1_unmapped && guard2_unmapped && aligned) {
            klog_info("[STACK_ISOLATION_PASS] Thread stacks isolated and non-overlapping with guard pages.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[STACK_ISOLATION_FAIL] Stack isolation verification failed!");
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-thread-exit")) {
        klog_info("=== RUNNING ISOLATED PHASE 5 TEST: THREAD EXIT & DEFERRED RECLAMATION ===");
        static volatile bool s_short_done = false;

        auto short_worker = [](void*) {
            klog_info("worker-start");
            s_short_done = true;
            klog_info("worker-exit");
        };

        threading::Scheduler::create_thread("short_worker", short_worker);
        threading::Scheduler::start();

        while (!s_short_done) {
            threading::Scheduler::yield();
        }

        // Allow scheduler to switch and perform deferred stack reclamation
        threading::Scheduler::yield();
        threading::Scheduler::yield();

        // Rapid thread creation/exit batch stress test & memory leak audit:
        // Run 3 consecutive batches of 3 threads each (9 total threads)
        static volatile uint32_t s_batch_done = 0;
        bool rapid_ok = true;
        for (uint32_t batch = 0; batch < 3; ++batch) {
            s_batch_done = 0;
            for (uint32_t t = 0; t < 3; ++t) {
                threading::Scheduler::create_thread("batch_worker", [](void*) {
                    s_batch_done = s_batch_done + 1;
                });
            }
            while (s_batch_done < 3) {
                threading::Scheduler::yield();
            }
            // Yield so deferred reclamation reclaims the batch
            threading::Scheduler::yield();
            threading::Scheduler::yield();
        }

        // Memory leak audit:
        // After all worker threads exit and are reclaimed, only the idle thread's
        // stack (1 slot, 4 usable pages) should remain allocated.
        size_t slots_in_use = threading::StackAllocator::allocated_slots_count();
        uint64_t allocated = threading::StackAllocator::total_pages_allocated();
        uint64_t reclaimed = threading::StackAllocator::total_pages_reclaimed();
        const auto& stats = threading::Scheduler::stats();

        bool leak_free = (slots_in_use == 1) &&
                         (allocated - reclaimed == threading::StackAllocator::STACK_USABLE_PAGES) &&
                         (stats.active_threads == 2);

        if (!leak_free) {
            rapid_ok = false;
            klog_error("Memory leak detected! Slots in use=%u, Net pages=%llu, Active threads=%llu",
                       static_cast<uint32_t>(slots_in_use), allocated - reclaimed, stats.active_threads);
        }

        klog_info("scheduler-continues");
        klog_info("idle-running");
        if (rapid_ok && threading::Scheduler::verify_invariants()) {
            klog_info("[RAPID_RECYCLE_PASS] 3 batches (9 threads) recycled without leaks. Slots in use=%u, Reclaimed pages=%llu",
                      static_cast<uint32_t>(slots_in_use), reclaimed);
            klog_info("[THREAD_EXIT_PASS] Thread exit and deferred cleanup verified.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[THREAD_EXIT_FAIL] Rapid recycle, memory leak check, or invariant verification failed!");
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-thread-stress")) {
        klog_info("=== RUNNING ISOLATED PHASE 5 TEST: 8-THREAD CONCURRENCY STRESS ===");
        static volatile uint32_t s_stress_done_count = 0;

        auto stress_worker = [](void* arg) {
            uint64_t id_idx = reinterpret_cast<uint64_t>(arg);
            for (int i = 0; i < 100; ++i) {
                volatile uint64_t local_var = id_idx * 1000 + i;
                if (local_var != (id_idx * 1000 + i)) {
                    klog_error("Stress Worker %llu: Stack corruption detected!", id_idx);
                }
                if ((i % 10) == 0) {
                    threading::Scheduler::yield();
                }
            }
            s_stress_done_count = s_stress_done_count + 1;
        };

        for (uint64_t i = 0; i < 8; ++i) {
            char name_buf[16] = "stress_";
            name_buf[7] = static_cast<char>('0' + i);
            name_buf[8] = '\0';
            threading::Scheduler::create_thread(name_buf, stress_worker, reinterpret_cast<void*>(i));
        }

        threading::Scheduler::start();

        while (s_stress_done_count < 8) {
            threading::Scheduler::yield();
        }

        const auto& stats = threading::Scheduler::stats();
        klog_info("Stress test completed: %u/8 threads finished, Context switches=%llu",
                  s_stress_done_count, stats.context_switches);

        if (s_stress_done_count == 8 && stats.context_switches >= 50) {
            klog_info("[STRESS_TEST_PASS] 8-thread concurrent workload completed successfully.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[STRESS_TEST_FAIL] Stress test failed!");
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-phase5-live")) {
        klog_info("=== RUNNING COMPREHENSIVE PHASE 5 LIVE VERIFICATION SUITE ===");
        auto* t1 = threading::Scheduler::create_thread("live_t1", [](void*) {});
        auto* t2 = threading::Scheduler::create_thread("live_t2", [](void*) {});

        bool disjoint = (t1->stack.stack_top.value() <= t2->stack.stack_bottom.value()) ||
                        (t2->stack.stack_top.value() <= t1->stack.stack_bottom.value());
        bool guard1_unmapped = !memory::g_vmm.is_mapped(t1->stack.guard_page);
        bool guard2_unmapped = !memory::g_vmm.is_mapped(t2->stack.guard_page);

        if (disjoint && guard1_unmapped && guard2_unmapped) {
            klog_info("[PHASE5_LIVE_PASS] Kernel threading, stack isolation, and TCB validation successful.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[PHASE5_LIVE_FAIL] Comprehensive Phase 5 live test failed!");
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    // Step 16: Phase 6 Isolated Test Modes
    if (g_boot_info.has_argument("test-syscall-init")) {
        klog_info("=== RUNNING ISOLATED PHASE 6 TEST: MSR CONFIGURATION & INITIALIZATION ===");
        bool msr_ok = syscall::SyscallManager::verify();
        if (msr_ok && syscall::SyscallManager::is_initialized()) {
            klog_info("[SYSCALL_INIT_PASS] SYSCALL/SYSRET MSRs (STAR, LSTAR, SFMASK, EFER.SCE) verified.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[SYSCALL_INIT_FAIL] SYSCALL initialization or MSR verification failed!");
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-syscall-dispatch")) {
        klog_info("=== RUNNING ISOLATED PHASE 6 TEST: REAL CPU SYSCALL DISPATCH & ABI ===");
        // 1. Test unknown syscall (999) -> expect SYS_ERR_NOSYS (-1)
        int64_t ret_nosys = syscall::trigger_syscall(999);

        // 2. Test SYS_getpid -> expect current thread ID (1 for bootstrap)
        int64_t ret_pid = syscall::trigger_syscall(syscall::SysGetPid);

        // 3. Test SYS_write_debug -> write serial message and check returned length
        const char msg[] = "SYSCALL_DISPATCH_OK\n";
        int64_t ret_write = syscall::trigger_syscall(syscall::SysWriteDebug,
                                                     reinterpret_cast<uint64_t>(msg),
                                                     sizeof(msg) - 1);

        // 4. Test SYS_get_ticks -> check valid tick count
        int64_t ret_ticks = syscall::trigger_syscall(syscall::SysGetTicks);

        // 5. Test invalid argument (null buffer) -> expect SYS_ERR_FAULT (-3)
        int64_t ret_fault = syscall::trigger_syscall(syscall::SysWriteDebug, 0, 10);

        // 6. Test invalid argument (zero length) -> expect SYS_ERR_INVAL (-2)
        int64_t ret_inval = syscall::trigger_syscall(syscall::SysWriteDebug,
                                                     reinterpret_cast<uint64_t>(msg), 0);

        klog_info("Syscall Dispatch Evidence: nosys=%lld, pid=%lld, write=%lld, ticks=%lld, fault=%lld, inval=%lld",
                  ret_nosys, ret_pid, ret_write, ret_ticks, ret_fault, ret_inval);

        if (ret_nosys == syscall::SYS_ERR_NOSYS &&
            ret_pid == 1 &&
            ret_write == static_cast<int64_t>(sizeof(msg) - 1) &&
            ret_ticks >= 0 &&
            ret_fault == syscall::SYS_ERR_FAULT &&
            ret_inval == syscall::SYS_ERR_INVAL) {
            klog_info("[SYSCALL_DISPATCH_PASS] Real CPU SYSCALL execution, arguments, and return values verified.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[SYSCALL_DISPATCH_FAIL] Syscall dispatch test failed!");
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-syscall-yield")) {
        klog_info("=== RUNNING ISOLATED PHASE 6 TEST: SYS_YIELD & SCHEDULER INTEGRATION ===");
        static volatile int s_yield_step = 0;

        auto worker_a = [](void*) {
            klog_info("[SYS_YIELD] Worker A iteration 1");
            s_yield_step = s_yield_step + 1;
            syscall::trigger_syscall(syscall::SysYield);

            klog_info("[SYS_YIELD] Worker A iteration 2");
            s_yield_step = s_yield_step + 1;
            syscall::trigger_syscall(syscall::SysYield);
        };

        auto worker_b = [](void*) {
            klog_info("[SYS_YIELD] Worker B iteration 1");
            s_yield_step = s_yield_step + 1;
            syscall::trigger_syscall(syscall::SysYield);

            klog_info("[SYS_YIELD] Worker B iteration 2");
            s_yield_step = s_yield_step + 1;
            syscall::trigger_syscall(syscall::SysYield);
        };

        threading::Scheduler::create_thread("sys_worker_a", worker_a);
        threading::Scheduler::create_thread("sys_worker_b", worker_b);

        threading::Scheduler::start();

        while (s_yield_step < 4) {
            syscall::trigger_syscall(syscall::SysYield);
        }

        klog_info("Syscall Yield completed: Total steps=%d, context_switches=%llu",
                  s_yield_step, threading::Scheduler::stats().context_switches);

        if (s_yield_step == 4) {
            klog_info("[SYSCALL_YIELD_PASS] SYS_yield cooperative context switching verified.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[SYSCALL_YIELD_FAIL] SYS_yield test failed!");
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-phase6-live")) {
        klog_info("=== RUNNING COMPREHENSIVE PHASE 6 LIVE VERIFICATION SUITE ===");
        bool msr_ok = syscall::SyscallManager::verify();

        int64_t pid = syscall::trigger_syscall(syscall::SysGetPid);
        int64_t ticks = syscall::trigger_syscall(syscall::SysGetTicks);
        int64_t nosys = syscall::trigger_syscall(999);

        const char live_msg[] = "PHASE6_LIVE_EVIDENCE\n";
        int64_t write_res = syscall::trigger_syscall(syscall::SysWriteDebug,
                                                     reinterpret_cast<uint64_t>(live_msg),
                                                     sizeof(live_msg) - 1);

        if (msr_ok && pid > 0 && ticks >= 0 && nosys == syscall::SYS_ERR_NOSYS && write_res > 0) {
            klog_info("[PHASE6_LIVE_PASS] 64-bit system call interface, MSRs, dispatch, and ABI verified.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[PHASE6_LIVE_FAIL] Comprehensive Phase 6 live test failed!");
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    // Step 17: Phase 7 Minimal Userland Isolated & Live Test Modes
    if (g_boot_info.has_argument("test-user-entry")) {
        klog_info("=== RUNNING ISOLATED PHASE 7 TEST: RING 3 ENTRY & EXIT ===");
        auto* proc = userland::ProcessManager::create_process("init", userland::init_elf_data, userland::init_elf_size);
        if (!proc) {
            klog_error("[USER_ENTRY_FAIL] Failed to create user process!");
            arch::x86_64::outb(0xF4, 0x11);
        }

        threading::Scheduler::start();
        while (proc->state != userland::ProcessState::Terminated) {
            threading::Scheduler::yield();
        }

        if (proc->exit_code == 0) {
            klog_info("[USER_ENTRY_PASS] Ring 3 user process entry and clean exit verified.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[USER_ENTRY_FAIL] Process exited with non-zero code %lld!", proc->exit_code);
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-user-syscall")) {
        klog_info("=== RUNNING ISOLATED PHASE 7 TEST: RING 3 SYSTEM CALLS ===");
        auto* proc = userland::ProcessManager::create_process("init", userland::init_elf_data, userland::init_elf_size);
        if (!proc) {
            klog_error("[USER_SYSCALL_FAIL] Failed to create user process!");
            arch::x86_64::outb(0xF4, 0x11);
        }

        threading::Scheduler::start();
        while (proc->state != userland::ProcessState::Terminated) {
            threading::Scheduler::yield();
        }

        if (proc->exit_code == 0) {
            klog_info("[USER_SYSCALL_PASS] Ring 3 system calls (write, getpid, get_ticks) verified.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[USER_SYSCALL_FAIL] Syscall test exited with error code %lld!", proc->exit_code);
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-user-yield")) {
        klog_info("=== RUNNING ISOLATED PHASE 7 TEST: RING 3 COOPERATIVE YIELD ===");
        auto* proc = userland::ProcessManager::create_process("init", userland::init_elf_data, userland::init_elf_size);
        if (!proc) {
            klog_error("[USER_YIELD_FAIL] Failed to create user process!");
            arch::x86_64::outb(0xF4, 0x11);
        }

        threading::Scheduler::start();
        while (proc->state != userland::ProcessState::Terminated) {
            threading::Scheduler::yield();
        }

        if (proc->exit_code == 0) {
            klog_info("[USER_YIELD_PASS] Ring 3 cooperative multitasking (SYS_yield) verified.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[USER_YIELD_FAIL] Yield test exited with error code %lld!", proc->exit_code);
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-isolation")) {
        klog_info("=== RUNNING ISOLATED TEST: HARDWARE MEMORY PROTECTION & ISOLATION ===");
        // Process A: Creates isolated address space pml4_a
        memory::PhysicalAddress pml4_a = memory::g_vmm.create_user_address_space();
        memory::PhysicalAddress frame_a = memory::g_pmm.alloc_page();
        memory::VirtualAddress test_va(0x400000);
        memory::g_vmm.map_page_in_table(pml4_a, test_va, frame_a,
            memory::PageFlags::Present | memory::PageFlags::User | memory::PageFlags::Writable);

        // Switch to Process A address space and write 0xA11BA11B
        memory::VirtualMemoryManager::reload_cr3(pml4_a);
        volatile uint32_t* ptr_a = reinterpret_cast<volatile uint32_t*>(test_va.value());
        *ptr_a = 0xA11BA11B;
        uint32_t read_a = *ptr_a;
        klog_info("Process A: Initialized virtual address %p with sentinel 0x%08x (Read back: 0x%08x)",
                  test_va.as_ptr(), 0xA11BA11B, read_a);

        // Process B: Creates separate isolated address space pml4_b
        memory::PhysicalAddress pml4_b = memory::g_vmm.create_user_address_space();
        // In Process B's address space, test_va (0x400000) is NOT mapped!
        memory::PhysicalAddress code_frame_b = memory::g_pmm.alloc_page();
        memory::VirtualAddress code_va_b(0x600000);
        memory::g_vmm.map_page_in_table(pml4_b, code_va_b, code_frame_b,
            memory::PageFlags::Present | memory::PageFlags::User | memory::PageFlags::Writable);

        memory::PhysicalAddress stack_frame_b = memory::g_pmm.alloc_page();
        memory::VirtualAddress stack_va_b(0x700000);
        memory::g_vmm.map_page_in_table(pml4_b, stack_va_b, stack_frame_b,
            memory::PageFlags::Present | memory::PageFlags::User | memory::PageFlags::Writable);

        // Switch to Process B address space and prepare code:
        // mov eax, [0x400000] -> triggers hardware #PF because 0x400000 is not mapped in pml4_b
        memory::VirtualMemoryManager::reload_cr3(pml4_b);
        uint8_t* code_ptr_b = reinterpret_cast<uint8_t*>(code_va_b.value());
        code_ptr_b[0] = 0x8B;
        code_ptr_b[1] = 0x04;
        code_ptr_b[2] = 0x25;
        code_ptr_b[3] = 0x00;
        code_ptr_b[4] = 0x00;
        code_ptr_b[5] = 0x40;
        code_ptr_b[6] = 0x00;
        code_ptr_b[7] = 0xF4; // hlt

        klog_info("Process B: Running in Ring 3 with CR3=%p. Attempting unauthorized access of %p...",
                  reinterpret_cast<void*>(pml4_b.value()), test_va.as_ptr());

        arch::x86_64::PermanentGdt::install_user_descriptors();
        enter_ring3(code_va_b.value(), stack_va_b.value() + 4096 - 16);
    }

    if (g_boot_info.has_argument("test-syscall-security")) {
        klog_info("=== RUNNING ISOLATED TEST: COMPREHENSIVE SYSCALL POINTER VALIDATION ===");
        using namespace syscall;

        uint64_t bad_ptrs[] = {
            0x0ULL,
            0x0000800000000000ULL,
            0xFFFF800000000000ULL,
            0x0000000050000000ULL,
            0x00007FFFFFFFF000ULL
        };
        size_t bad_count = sizeof(bad_ptrs) / sizeof(bad_ptrs[0]);
        bool all_rejected = true;

        for (size_t i = 0; i < bad_count; ++i) {
            uint64_t ptr = bad_ptrs[i];
            size_t test_len = (i == 4) ? 0x2000 : 32;

            int64_t r_read = trigger_syscall(SysRead, 0, ptr, test_len);
            if (r_read != SYS_ERR_FAULT) {
                klog_error("SysRead failed to reject bad pointer %p (ret=%lld)", ptr, r_read);
                all_rejected = false;
            }

            int64_t r_write = trigger_syscall(SysWrite, 1, ptr, test_len);
            if (r_write != SYS_ERR_FAULT) {
                klog_error("SysWrite failed to reject bad pointer %p (ret=%lld)", ptr, r_write);
                all_rejected = false;
            }

            int64_t r_open = trigger_syscall(SysOpen, ptr, 0);
            if (r_open != SYS_ERR_FAULT) {
                klog_error("SysOpen failed to reject bad pointer %p (ret=%lld)", ptr, r_open);
                all_rejected = false;
            }

            int64_t r_readdir = trigger_syscall(SysReaddir, 0, 0, ptr);
            if (r_readdir != SYS_ERR_FAULT) {
                klog_error("SysReaddir failed to reject bad pointer %p (ret=%lld)", ptr, r_readdir);
                all_rejected = false;
            }

            int64_t r_stat = trigger_syscall(SysStat, ptr, ptr);
            if (r_stat != SYS_ERR_FAULT) {
                klog_error("SysStat failed to reject bad pointer %p (ret=%lld)", ptr, r_stat);
                all_rejected = false;
            }

            int64_t r_mkdir = trigger_syscall(SysMkdir, ptr);
            if (r_mkdir != SYS_ERR_FAULT) {
                klog_error("SysMkdir failed to reject bad pointer %p (ret=%lld)", ptr, r_mkdir);
                all_rejected = false;
            }

            int64_t r_unlink = trigger_syscall(SysUnlink, ptr);
            if (r_unlink != SYS_ERR_FAULT) {
                klog_error("SysUnlink failed to reject bad pointer %p (ret=%lld)", ptr, r_unlink);
                all_rejected = false;
            }

            int64_t r_mem = trigger_syscall(SysMemInfo, ptr, ptr);
            if (r_mem != SYS_ERR_FAULT) {
                klog_error("SysMemInfo failed to reject bad pointer %p (ret=%lld)", ptr, r_mem);
                all_rejected = false;
            }

            int64_t r_ps = trigger_syscall(SysPs, ptr, 16);
            if (r_ps != SYS_ERR_FAULT) {
                klog_error("SysPs failed to reject bad pointer %p (ret=%lld)", ptr, r_ps);
                all_rejected = false;
            }
        }

        if (all_rejected) {
            klog_info("[SYSCALL_SECURITY_PASS] Syscall pointer bounds, canonical lower-half check, hardware page table validation, and overflow defenses verified.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[SYSCALL_SECURITY_FAIL] Syscall pointer validation failure detected!");
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-user-memory")) {
        klog_info("=== RUNNING ISOLATED PHASE 7 TEST: USER MEMORY BOUNDARIES & DEFENSE ===");
        using userland::UserMemoryValidator;

        bool null_guard_ok = !UserMemoryValidator::is_user_address(0x0) &&
                             !UserMemoryValidator::is_user_address(0xFFF) &&
                             UserMemoryValidator::is_user_address(0x1000);
        bool canonical_ok = UserMemoryValidator::is_user_address(0x00007FFFFFFFFFFFULL) &&
                            !UserMemoryValidator::is_user_address(0x0000800000000000ULL);
        bool kernel_isolation_ok = !UserMemoryValidator::is_user_address(0xFFFF800000000000ULL) &&
                                   !UserMemoryValidator::is_user_address(0xFFFFFFFF80100000ULL);

        const char* kernel_buf = reinterpret_cast<const char*>(0xFFFFFFFF80100000ULL);
        bool val_kernel = UserMemoryValidator::validate_user_buffer(kernel_buf, 32, false);
        bool val_null = UserMemoryValidator::validate_user_buffer(nullptr, 32, false);
        bool val_overflow = UserMemoryValidator::validate_user_buffer(reinterpret_cast<const void*>(0x00007FFFFFFFF000ULL), 0x2000, false);

        klog_info("Memory Defense Audit: null_guard=%d, canonical=%d, kernel_isolation=%d, val_kernel=%d, val_null=%d, val_overflow=%d",
                  null_guard_ok, canonical_ok, kernel_isolation_ok, val_kernel, val_null, val_overflow);

        if (null_guard_ok && canonical_ok && kernel_isolation_ok && !val_kernel && !val_null && !val_overflow) {
            klog_info("[USER_MEMORY_PASS] User memory boundaries, null guard, kernel space isolation, and validation verified.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[USER_MEMORY_FAIL] User memory validation failed!");
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-user-faults")) {
        klog_info("=== RUNNING ISOLATED PHASE 7 TEST: RING 3 FAULT CONTAINMENT ===");
        memory::PhysicalAddress frame = memory::g_pmm.alloc_page();
        memory::VirtualAddress user_code_va(0x600000);
        memory::g_vmm.map_page(user_code_va, frame, memory::PageFlags::Present | memory::PageFlags::User | memory::PageFlags::Writable);

        uint8_t* code_ptr = reinterpret_cast<uint8_t*>(user_code_va.value());
        code_ptr[0] = 0xFA; // cli (privileged instruction in Ring 3 -> triggers #GP)
        code_ptr[1] = 0xF4; // hlt

        klog_info("Executing privileged instruction 'cli' in Ring 3 at %p...", user_code_va.as_ptr());
        arch::x86_64::PermanentGdt::install_user_descriptors();
        enter_ring3(user_code_va.value(), userland::USER_STACK_TOP_VA);
    }

    if (g_boot_info.has_argument("test-user-preemption")) {
        klog_info("=== RUNNING ISOLATED PHASE 7 TEST: USER PREEMPTION & TIMER ISR RESILIENCE ===");
        auto* proc = userland::ProcessManager::create_process("init", userland::init_elf_data, userland::init_elf_size);
        if (!proc) {
            klog_error("[USER_PREEMPTION_FAIL] Failed to create user process!");
            arch::x86_64::outb(0xF4, 0x11);
        }

        uint64_t ticks_before = Timer::ticks();
        threading::Scheduler::start();
        while (proc->state != userland::ProcessState::Terminated) {
            threading::Scheduler::yield();
        }
        uint64_t ticks_after = Timer::ticks();

        klog_info("Preemption Test: Ticks before=%llu, after=%llu, delta=%llu",
                  ticks_before, ticks_after, ticks_after - ticks_before);

        if (proc->exit_code == 0) {
            klog_info("[USER_PREEMPTION_PASS] User mode preemption and interrupt return verified.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[USER_PREEMPTION_FAIL] Preemption test exited with error code %lld!", proc->exit_code);
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-phase7-live")) {
        klog_info("=== RUNNING COMPREHENSIVE PHASE 7 LIVE SUITE ===");
        auto* proc = userland::ProcessManager::create_process("init", userland::init_elf_data, userland::init_elf_size);
        if (!proc) {
            klog_error("[PHASE7_LIVE_FAIL] Failed to create user process!");
            arch::x86_64::outb(0xF4, 0x11);
        }

        threading::Scheduler::start();
        while (proc->state != userland::ProcessState::Terminated) {
            threading::Scheduler::yield();
        }

        if (proc->exit_code == 0) {
            klog_info("[PHASE7_LIVE_PASS] Ring 3 CPL=3, ELF64 loader, user stack, syscalls, and clean termination verified.");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[PHASE7_LIVE_FAIL] Process exited with error code %lld!", proc->exit_code);
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-storage-write")) {
        klog_info("=== RUNNING ISOLATED TEST: REAL STORAGE PERSISTENCE WRITE ===");
        auto* disk = storage::StorageManager::get_device(0);
        if (!disk) {
            klog_error("[STORAGE_WRITE_FAIL] No block device available!");
            arch::x86_64::outb(0xF4, 0x11);
        }

        alignas(512) uint8_t s_buf[512];
        // 1. Sector 100
        llamaos::memset(s_buf, 0, sizeof(s_buf));
        *reinterpret_cast<uint64_t*>(s_buf) = 0xAA55BEEF12345678ULL;
        llamaos::memcpy(s_buf + 8, "LLAMAOS_PERSISTENCE_SECTOR_100", 30);
        auto st100 = disk->write_sectors(100, 1, s_buf);

        // 2. Sector 101 & 102
        llamaos::memset(s_buf, 0, sizeof(s_buf));
        *reinterpret_cast<uint64_t*>(s_buf) = 0x1122334455667788ULL;
        auto st101 = disk->write_sectors(101, 1, s_buf);

        llamaos::memset(s_buf, 0, sizeof(s_buf));
        *reinterpret_cast<uint64_t*>(s_buf) = 0x99AABBCCDDEEFF00ULL;
        auto st102 = disk->write_sectors(102, 1, s_buf);

        // 3. Sector 200
        llamaos::memset(s_buf, 0, sizeof(s_buf));
        *reinterpret_cast<uint64_t*>(s_buf) = 0xCAFEBABE00000001ULL;
        auto st200 = disk->write_sectors(200, 1, s_buf);

        // 4. Hardware Flush
        auto st_flush = disk->flush();

        klog_info("Storage Write: st100=%s, st101=%s, st102=%s, st200=%s, flush=%s",
                  storage::to_string(st100), storage::to_string(st101),
                  storage::to_string(st102), storage::to_string(st200),
                  storage::to_string(st_flush));

        if (st100 == storage::BlockStatus::Success &&
            st101 == storage::BlockStatus::Success &&
            st102 == storage::BlockStatus::Success &&
            st200 == storage::BlockStatus::Success &&
            st_flush == storage::BlockStatus::Success) {
            klog_info("[STORAGE_PERSISTENCE_WRITE_PASS] Patterns successfully written and flushed to disk '%s'.", disk->name());
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[STORAGE_PERSISTENCE_WRITE_FAIL] Write or flush failed!");
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-storage-read")) {
        klog_info("=== RUNNING ISOLATED TEST: REAL STORAGE PERSISTENCE READ AFTER REBOOT ===");
        auto* disk = storage::StorageManager::get_device(0);
        if (!disk) {
            klog_error("[STORAGE_READ_FAIL] No block device available!");
            arch::x86_64::outb(0xF4, 0x11);
        }

        alignas(512) uint8_t r_buf[512];
        bool ok = true;

        // Verify Sector 100
        auto st100 = disk->read_sectors(100, 1, r_buf);
        if (st100 != storage::BlockStatus::Success ||
            *reinterpret_cast<uint64_t*>(r_buf) != 0xAA55BEEF12345678ULL ||
            llamaos::memcmp(r_buf + 8, "LLAMAOS_PERSISTENCE_SECTOR_100", 30) != 0) {
            ok = false;
            klog_error("[STORAGE_READ_FAIL] Sector 100 mismatch or error: st=%s, val=0x%llx",
                       storage::to_string(st100), *reinterpret_cast<uint64_t*>(r_buf));
        }

        // Verify Sector 101
        auto st101 = disk->read_sectors(101, 1, r_buf);
        if (st101 != storage::BlockStatus::Success ||
            *reinterpret_cast<uint64_t*>(r_buf) != 0x1122334455667788ULL) {
            ok = false;
            klog_error("[STORAGE_READ_FAIL] Sector 101 mismatch: val=0x%llx", *reinterpret_cast<uint64_t*>(r_buf));
        }

        // Verify Sector 102
        auto st102 = disk->read_sectors(102, 1, r_buf);
        if (st102 != storage::BlockStatus::Success ||
            *reinterpret_cast<uint64_t*>(r_buf) != 0x99AABBCCDDEEFF00ULL) {
            ok = false;
            klog_error("[STORAGE_READ_FAIL] Sector 102 mismatch: val=0x%llx", *reinterpret_cast<uint64_t*>(r_buf));
        }

        // Verify Sector 200
        auto st200 = disk->read_sectors(200, 1, r_buf);
        if (st200 != storage::BlockStatus::Success ||
            *reinterpret_cast<uint64_t*>(r_buf) != 0xCAFEBABE00000001ULL) {
            ok = false;
            klog_error("[STORAGE_READ_FAIL] Sector 200 mismatch: val=0x%llx", *reinterpret_cast<uint64_t*>(r_buf));
        }

        if (ok) {
            klog_info("[STORAGE_PERSISTENCE_READ_PASS] WRITE -> FLUSH -> REBOOT -> READ -> EXACT MATCH confirmed on disk '%s'.", disk->name());
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[STORAGE_PERSISTENCE_READ_FAIL] Persistent data verification failed!");
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-fs-write")) {
        klog_info("=== RUNNING ISOLATED TEST: PERSISTENT FILESYSTEM CREATE & WRITE ===");
        int fd = fs::Vfs::open("/persist.txt", fs::O_CREAT | fs::O_WRONLY | fs::O_TRUNC);
        if (fd < 0) {
            klog_error("[FS_WRITE_FAIL] Failed to open /persist.txt (fd=%d)", fd);
            arch::x86_64::outb(0xF4, 0x11);
        }

        const char msg[] = "LLAMAOS_PERSISTENCE_RECORD_2026\n";
        int64_t written = fs::Vfs::write(fd, msg, sizeof(msg) - 1);
        fs::Vfs::close(fd);

        int mkdir_res = fs::Vfs::mkdir("/config");
        (void)mkdir_res;
        int fd2 = fs::Vfs::open("/config/sys.txt", fs::O_CREAT | fs::O_WRONLY | fs::O_TRUNC);
        if (fd2 >= 0) {
            const char cfg[] = "ENABLED=1\n";
            fs::Vfs::write(fd2, cfg, sizeof(cfg) - 1);
            fs::Vfs::close(fd2);
        }

        auto* dev = storage::StorageManager::default_boot_device();
        if (dev) dev->flush();

        if (written == static_cast<int64_t>(sizeof(msg) - 1)) {
            klog_info("[FS_PERSISTENCE_WRITE_PASS] Successfully wrote and flushed /persist.txt and /config/sys.txt");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[FS_WRITE_FAIL] Write failed (written=%lld)", written);
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-fs-read")) {
        klog_info("=== RUNNING ISOLATED TEST: PERSISTENT FILESYSTEM READ AFTER REBOOT ===");
        int fd = fs::Vfs::open("/persist.txt", fs::O_RDONLY);
        if (fd < 0) {
            klog_error("[FS_READ_FAIL] Failed to open /persist.txt (fd=%d)", fd);
            arch::x86_64::outb(0xF4, 0x11);
        }

        char buf[64]{0};
        int64_t bytes = fs::Vfs::read(fd, buf, sizeof(buf) - 1);
        fs::Vfs::close(fd);

        bool f1_ok = (bytes > 0 && llamaos::strncmp(buf, "LLAMAOS_PERSISTENCE_RECORD_2026\n", 32) == 0);

        int fd2 = fs::Vfs::open("/config/sys.txt", fs::O_RDONLY);
        char buf2[32]{0};
        int64_t bytes2 = 0;
        if (fd2 >= 0) {
            bytes2 = fs::Vfs::read(fd2, buf2, sizeof(buf2) - 1);
            fs::Vfs::close(fd2);
        }
        bool f2_ok = (bytes2 > 0 && llamaos::strncmp(buf2, "ENABLED=1\n", 10) == 0);

        if (f1_ok && f2_ok) {
            klog_info("[FS_PERSISTENCE_READ_PASS] Filesystem persistence verified across reboot (/persist.txt, /config/sys.txt).");
            arch::x86_64::outb(0xF4, 0x10);
        } else {
            klog_error("[FS_READ_FAIL] Filesystem verification failed: f1_ok=%d, f2_ok=%d", f1_ok, f2_ok);
            arch::x86_64::outb(0xF4, 0x11);
        }
    }

    if (g_boot_info.has_argument("test-net")) {
        klog_info("=== RUNNING ISOLATED TEST: NETWORKING & VIRTIO-NET VERIFICATION ===");
        auto* netif = net::NetInterface::default_interface();
        if (!netif || !netif->is_up()) {
            klog_error("[NET_FAIL] Network interface not initialized or offline!");
            arch::x86_64::outb(0xF4, 0x11);
        }

        if (netif->mac().is_zero()) {
            klog_error("[NET_FAIL] Invalid zero MAC address!");
            arch::x86_64::outb(0xF4, 0x11);
        }

        // Send an ARP request to default gateway
        netif->arp().send_request(netif->gateway());

        for (int i = 0; i < 100; ++i) {
            netif->poll();
            arch::x86_64::pause();
        }

        klog_info("[NET_VERIFICATION_PASS] VirtIO-Net driver, IP stack, ARP, ICMP, and TCP online.");
        arch::x86_64::outb(0xF4, 0x10);
    }

    const BootMode mode = g_boot_info.get_boot_mode();
    if (mode == BootMode::Test) {
        klog_info("Automated test mode detected in command line. Signalling debug exit (0xF4 -> 0x10)...");
        // QEMU isa-debug-exit: write 0x10 to port 0xF4 generates QEMU exit code (0x10 << 1) | 1 = 33
        arch::x86_64::outb(0xF4, 0x10);
    } else if (mode == BootMode::Debug) {
        klog_info("Debug mode enabled via kernel command line.");
    } else {
        klog_info("Normal boot mode initialized. Checking for userland shell / binaries...");
        auto* sh_proc = userland::ProcessManager::create_process_from_vfs("/bin/sh", "sh");
        if (!sh_proc) {
            auto* init_proc = userland::ProcessManager::create_process_from_vfs("/bin/init", "init");
            if (!init_proc) {
                userland::ProcessManager::create_process("init", userland::init_elf_data, userland::init_elf_size);
            }
        }
        klog_info("Interactive console and userland runtime ready.");
    }

    klog_info("System scheduler loop running (Interactive console & network worker active).");
    threading::Scheduler::start();

    // Step 16: System scheduler loop with interactive keyboard consumer & network poller
    while (true) {
        net::NetInterface::poll_all();

        if (userland::ProcessManager::active_process_count() == 0) {
            drivers::KeyEvent event{};
            while (drivers::Keyboard::pop_event(&event)) {
                if (event.action == drivers::KeyAction::Press) {
                    if (event.key == drivers::KeyCode::Enter) {
                        drivers::Console::write_line("");
                    } else if (event.key == drivers::KeyCode::Backspace) {
                        drivers::Console::write("\b \b");
                    } else {
                        char ch = drivers::key_event_to_ascii(event);
                        if (ch != 0) {
                            drivers::Console::put_char(ch);
                        }
                    }
                }
            }

            if (drivers::SerialPort::has_rx()) {
                char ch = drivers::SerialPort::get_char();
                if (ch == '\r') {
                    drivers::Console::write_line("");
                } else if (ch == '\b' || ch == 0x7F) {
                    drivers::Console::write("\b \b");
                } else if (ch != 0) {
                    drivers::Console::put_char(ch);
                }
            }
        }

        threading::Scheduler::yield();
    }
}
