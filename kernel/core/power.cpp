#include "power.hpp"
#include "arch/x86_64/cpu/io.hpp"
#include "storage/storage_manager.hpp"
#include "core/kprint.hpp"

// =============================================================================
// LlamaOS/A - System Power Management Implementation
// =============================================================================

namespace llamaos::core {

using namespace arch::x86_64;

[[noreturn]] void PowerManager::reboot() {
    klog_info("================================================================================");
    klog_info(" [SYSTEM_REBOOT] Flushing storage buffers and initiating hardware reset...");
    klog_info("================================================================================");

    // 1. Flush all persistent storage devices
    auto* dev = storage::StorageManager::default_boot_device();
    if (dev) {
        dev->flush();
    }

    // 2. Multi-tier reset sequence:
    // Tier 1: Fast PS/2 8042 Keyboard Controller Reset (Port 0x64 -> 0xFE)
    uint8_t good = 0x02;
    while (good & 0x02) {
        good = inb(0x64);
    }
    outb(0x64, 0xFE);

    // Tier 2: PCI Reset (Port 0xCF9 -> 0x06 or 0x0E)
    outb(0xCF9, 0x06);
    outb(0xCF9, 0x0E);

    // Tier 3: Triple Fault via invalid IDT
    struct [[gnu::packed]] {
        uint16_t limit{0};
        uint64_t base{0};
    } null_idt;

    asm volatile("lidt %0; int3" :: "m"(null_idt));

    // Fallback infinite halt loop
    while (true) {
        asm volatile("cli; hlt");
    }
}

[[noreturn]] void PowerManager::poweroff() {
    klog_info("================================================================================");
    klog_info(" [SYSTEM_POWEROFF] Flushing storage buffers and initiating ACPI shutdown...");
    klog_info("================================================================================");

    // 1. Flush all persistent storage devices
    auto* dev = storage::StorageManager::default_boot_device();
    if (dev) {
        dev->flush();
    }

    // 2. QEMU isa-debug-exit port (Port 0xF4 -> 0x10 exits QEMU cleanly with code 33)
    outb(0xF4, 0x10);

    // 3. QEMU ACPI shutdown port (Port 0x604 -> 0x2000)
    outw(0x604, 0x2000);

    // 4. Older QEMU/Bochs ACPI port (Port 0xB004 -> 0x2000)
    outw(0xB004, 0x2000);

    // 5. VirtualBox ACPI port (Port 0x4004 -> 0x3400)
    outw(0x4004, 0x3400);

    // Fallback infinite halt loop
    while (true) {
        asm volatile("cli; hlt");
    }
}

[[noreturn]] void PowerManager::emergency_halt() {
    klog_error(" [EMERGENCY_HALT] CPU halted by kernel.");
    while (true) {
        asm volatile("cli; hlt");
    }
}

} // namespace llamaos::core
