#include "gdt.hpp"
#include "cpu.hpp"
#include "core/kprint.hpp"
#include "memory/vmm.hpp"

// =============================================================================
// LlamaOS/A - Permanent GDT Implementation
// =============================================================================

namespace llamaos::arch::x86_64 {

alignas(16) GdtTable PermanentGdt::s_table{};
bool PermanentGdt::s_user_descriptors_installed{false};

void PermanentGdt::init(uint64_t tss_base, uint32_t tss_limit) {
    // 1. Entry 0: Null Descriptor
    s_table.entries[0].raw = 0;

    // 2. Entry 1: 64-bit Kernel Code Descriptor (0x08)
    s_table.entries[1] = encode_code_descriptor(0);

    // 3. Entry 2: 64-bit Kernel Data Descriptor (0x10)
    s_table.entries[2] = encode_data_descriptor(0);

    // 4. Entry 3 & 4: 64-bit TSS Descriptor (0x18)
    s_table.tss = encode_tss_descriptor(tss_base, tss_limit, 0);

    // 5. Initial GDTR limit covers kernel descriptors and TSS (slots 0..4, 40 bytes)
    // Preserves isolated fault verification (#GP on selector 0x0028)
    s_user_descriptors_installed = false;
    Gdtr gdtr{};
    gdtr.limit = 39;
    gdtr.base = reinterpret_cast<uint64_t>(&s_table);
    lgdt(gdtr);

    // 6. Reload segment registers (CS, DS, ES, SS, FS, GS)
    reload_segments(Selector::KernelCode, Selector::KernelData);

    // 7. Load Task Register (TR) with TSS Selector
    load_task_register(Selector::Tss);

    klog_info("Permanent GDT initialized and loaded successfully:");
    klog_info("  GDTR Limit    : 0x%04x (size: %u bytes)", gdtr.limit, static_cast<uint32_t>(gdtr.limit + 1));
    klog_info("  GDTR Base     : %p", gdtr.base);
    klog_info("  Kernel Code   : Selector 0x%04x (Raw: 0x%016llx)", Selector::KernelCode, s_table.entries[1].raw);
    klog_info("  Kernel Data   : Selector 0x%04x (Raw: 0x%016llx)", Selector::KernelData, s_table.entries[2].raw);
    klog_info("  TSS Selector  : Selector 0x%04x (Low: 0x%016llx, High: 0x%016llx)",
              Selector::Tss, s_table.tss.low, s_table.tss.high);
}

void PermanentGdt::install_user_descriptors() {
    // Slot 5: 64-bit User Data Descriptor (0x28, DPL 3 -> 0x2B)
    s_table.user_data = encode_data_descriptor(3);

    // Slot 6: 64-bit User Code Descriptor (0x30, DPL 3 -> 0x33)
    s_table.user_code = encode_code_descriptor(3);

    // Expand GDT limit to cover user descriptors (56 bytes, limit 55)
    Gdtr gdtr{};
    gdtr.limit = sizeof(GdtTable) - 1;
    gdtr.base = reinterpret_cast<uint64_t>(&s_table);
    lgdt(gdtr);

    s_user_descriptors_installed = true;

    klog_info("Permanent GDT expanded for Phase 7 Userland (Ring 3):");
    klog_info("  GDTR Limit    : 0x%04x (size: %u bytes)", gdtr.limit, static_cast<uint32_t>(sizeof(GdtTable)));
    klog_info("  User Data     : Selector 0x%04x (DPL 3, Raw: 0x%016llx)", Selector::UserData | 3, s_table.user_data.raw);
    klog_info("  User Code     : Selector 0x%04x (DPL 3, Raw: 0x%016llx)", Selector::UserCode | 3, s_table.user_code.raw);
}

bool PermanentGdt::verify() {
    Gdtr current_gdtr{};
    sgdt(current_gdtr);

    // Verify GDTR base and limit
    uint64_t expected_base = reinterpret_cast<uint64_t>(&s_table);
    uint16_t expected_limit = s_user_descriptors_installed ? (sizeof(GdtTable) - 1) : 39;

    if (current_gdtr.base != expected_base || current_gdtr.limit != expected_limit) {
        klog_error("GDT Verify Failed: GDTR mismatch (base: %p != %p, limit: %x != %x)!",
                   current_gdtr.base, expected_base, current_gdtr.limit, expected_limit);
        return false;
    }

    // Verify segment registers
    uint16_t cs = read_cs();
    uint16_t ds = read_ds();
    uint16_t ss = read_ss();
    if (cs != Selector::KernelCode) {
        klog_error("GDT Verify Failed: CS is 0x%04x, expected 0x%04x!", cs, Selector::KernelCode);
        return false;
    }
    if (ds != Selector::KernelData) {
        klog_error("GDT Verify Failed: DS is 0x%04x, expected 0x%04x!", ds, Selector::KernelData);
        return false;
    }
    if (ss != Selector::KernelData) {
        klog_error("GDT Verify Failed: SS is 0x%04x, expected 0x%04x!", ss, Selector::KernelData);
        return false;
    }

    // Verify Task Register (TR)
    uint16_t tr = read_task_register();
    if (tr != Selector::Tss) {
        klog_error("GDT Verify Failed: TR is 0x%04x, expected 0x%04x!", tr, Selector::Tss);
        return false;
    }

    // Verify CPU set the TSS Busy bit (Bit 41 of s_table.tss.low)
    if ((s_table.tss.low & (1ULL << 41)) == 0) {
        klog_error("GDT Verify Failed: TSS descriptor Busy bit (bit 41) not set by CPU!");
        return false;
    }

    // Verify virtual mapping in active VMM
    memory::PhysicalAddress pa;
    memory::PageFlags flags;
    if (!memory::g_vmm.translate(memory::VirtualAddress(expected_base), &pa, &flags)) {
        klog_error("GDT Verify Failed: GDT table address %p is not mapped in VMM!", expected_base);
        return false;
    }
    if (!memory::test_flag(flags, memory::PageFlags::Present) ||
        !memory::test_flag(flags, memory::PageFlags::Writable) ||
        !memory::test_flag(flags, memory::PageFlags::NoExecute) ||
        memory::test_flag(flags, memory::PageFlags::User)) {
        klog_error("GDT Verify Failed: GDT table page flags invalid (must be RW NX Kernel)!");
        return false;
    }

    return true;
}

} // namespace llamaos::arch::x86_64
