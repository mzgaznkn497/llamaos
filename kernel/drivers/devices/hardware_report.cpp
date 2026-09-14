#include "drivers/devices/hardware_report.hpp"
#include "drivers/devices/device_registry.hpp"
#include "drivers/console/console.hpp"
#include "drivers/pci/pci.hpp"
#include "drivers/ps2/ps2_controller.hpp"
#include "drivers/ps2/keyboard.hpp"
#include "drivers/framebuffer/framebuffer.hpp"
#include "arch/x86_64/cpu/timer.hpp"
#include "core/kprint.hpp"

namespace llamaos::drivers {

void HardwareReport::display() noexcept {
    kprint("\n============================================================\n");
    kprint("           LLAMAOS/A HARDWARE DISCOVERY REPORT              \n");
    kprint("============================================================\n");

    // 1. Console & Display
    kprint("[HW] Console: Unified Serial (COM1 115200) + VGA (0xB8000)\n");

    // 2. Framebuffer
    if (Framebuffer::is_available()) {
        kprintf("[HW] Framebuffer: %ux%u@%ubpp, pitch %u, paddr %p\n",
                Framebuffer::width(), Framebuffer::height(),
                static_cast<uint32_t>(Framebuffer::bpp()), Framebuffer::pitch(),
                Framebuffer::physical_address());
    } else {
        kprint("[HW] Framebuffer: None / Text Mode Active\n");
    }

    // 3. Interrupt Controller & Timer
    kprintf("[HW] Interrupt Controller: Dual 8259A PIC (Base 0x20/0x28)\n");
    kprintf("[HW] System Timer: PIT 8254 @ 100 Hz (IRQ0, ticks: %u)\n",
            static_cast<uint32_t>(arch::x86_64::Timer::ticks()));

    // 4. PS/2 Subsystem
    if (Ps2Controller::is_initialized()) {
        kprintf("[HW] PS/2 Controller: 8042 (Status: OK, Config: 0x%02x, Port2: %s)\n",
                static_cast<uint32_t>(Ps2Controller::config_byte()),
                Ps2Controller::has_second_channel() ? "Present" : "Disabled");
    } else {
        kprint("[HW] PS/2 Controller: Not Present / Disabled\n");
    }

    if (Keyboard::is_initialized()) {
        kprint("[HW] PS/2 Keyboard: Active (IRQ1, Vector 0x21, Set 1 Decoder)\n");
    } else {
        kprint("[HW] PS/2 Keyboard: Uninitialized\n");
    }

    // 5. PCI Devices
    size_t pci_count = PciManager::device_count();
    kprintf("[HW] PCI Bus: Discovered %u device(s)\n", static_cast<uint32_t>(pci_count));
    const PciDevice* pci_devices = PciManager::devices();
    for (size_t i = 0; i < pci_count; ++i) {
        const PciDevice& d = pci_devices[i];
        kprintf("  - [%02x:%02x.%u] %04x:%04x %s\n",
                static_cast<uint32_t>(d.bus), static_cast<uint32_t>(d.device), static_cast<uint32_t>(d.function),
                static_cast<uint32_t>(d.vendor_id), static_cast<uint32_t>(d.device_id),
                d.class_string());
        for (size_t b = 0; b < 6; ++b) {
            if (d.bars[b].valid) {
                kprintf("      BAR%u: %s %s addr 0x%08x\n",
                        static_cast<uint32_t>(b),
                        d.bars[b].is_io ? "I/O" : "MEM",
                        d.bars[b].is_64bit ? "64-bit" : "32-bit",
                        static_cast<uint32_t>(d.bars[b].base_address));
            }
        }
    }

    // 6. Registered devices total
    kprintf("[HW] Total Registered Subsystem Devices: %u\n",
            static_cast<uint32_t>(DeviceRegistry::device_count()));
    kprint("============================================================\n\n");
}

} // namespace llamaos::drivers
