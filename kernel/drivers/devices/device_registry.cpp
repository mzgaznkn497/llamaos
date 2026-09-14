#include "drivers/devices/device_registry.hpp"
#include "drivers/console/console.hpp"
#include "drivers/ps2/ps2_controller.hpp"
#include "drivers/ps2/keyboard.hpp"
#include "drivers/pci/pci.hpp"
#include "drivers/framebuffer/framebuffer.hpp"
#include "core/string.hpp"

namespace llamaos::drivers {

DeviceInfo DeviceRegistry::s_devices[MAX_REGISTERED_DEVICES]{};
size_t     DeviceRegistry::s_device_count{0};

void DeviceRegistry::init() noexcept {
    s_device_count = 0;
}

bool DeviceRegistry::register_device(const DeviceInfo& info) noexcept {
    if (s_device_count >= MAX_REGISTERED_DEVICES) {
        return false;
    }
    s_devices[s_device_count++] = info;
    return true;
}

const DeviceInfo* DeviceRegistry::get_device(size_t index) noexcept {
    if (index < s_device_count) {
        return &s_devices[index];
    }
    return nullptr;
}

const DeviceInfo* DeviceRegistry::find_by_name(const char* name) noexcept {
    if (!name) return nullptr;
    for (size_t i = 0; i < s_device_count; ++i) {
        if (s_devices[i].name && strcmp(s_devices[i].name, name) == 0) {
            return &s_devices[i];
        }
    }
    return nullptr;
}

const char* DeviceRegistry::type_to_string(DeviceType type) noexcept {
    switch (type) {
    case DeviceType::Console:                return "Console";
    case DeviceType::SerialPort:             return "SerialPort";
    case DeviceType::VgaDisplay:             return "VgaDisplay";
    case DeviceType::Framebuffer:            return "Framebuffer";
    case DeviceType::Ps2Controller:          return "Ps2Controller";
    case DeviceType::Keyboard:               return "Keyboard";
    case DeviceType::PciBus:                 return "PciBus";
    case DeviceType::PciDevice:              return "PciDevice";
    case DeviceType::Timer:                  return "Timer";
    case DeviceType::PicInterruptController: return "PicInterruptController";
    default:                                 return "Unknown";
    }
}

const char* DeviceRegistry::state_to_string(DeviceState state) noexcept {
    switch (state) {
    case DeviceState::Active:        return "Active";
    case DeviceState::Standby:       return "Standby";
    case DeviceState::Failed:        return "Failed";
    case DeviceState::Disabled:      return "Disabled";
    case DeviceState::Uninitialized: return "Uninitialized";
    default:                         return "Unknown";
    }
}

void DeviceRegistry::populate_detected_devices() noexcept {
    s_device_count = 0;

    // 1. Console & Display
    register_device(DeviceInfo{
        .name = "System Unified Console",
        .type = DeviceType::Console,
        .state = DeviceState::Active,
        .driver_name = "Console",
        .resource_addr = 0,
        .irq = 0xFFFFFFFF
    });

    register_device(DeviceInfo{
        .name = "Serial COM1 Port",
        .type = DeviceType::SerialPort,
        .state = DeviceState::Active,
        .driver_name = "SerialPort",
        .resource_addr = 0x3F8,
        .irq = 4
    });

    register_device(DeviceInfo{
        .name = "VGA Text Display",
        .type = DeviceType::VgaDisplay,
        .state = DeviceState::Active,
        .driver_name = "VgaConsole",
        .resource_addr = 0xB8000,
        .irq = 0xFFFFFFFF
    });

    // 2. Core Chipset / Interrupt / Timer
    register_device(DeviceInfo{
        .name = "Dual 8259A PIC",
        .type = DeviceType::PicInterruptController,
        .state = DeviceState::Active,
        .driver_name = "PicManager",
        .resource_addr = 0x20,
        .irq = 0xFFFFFFFF
    });

    register_device(DeviceInfo{
        .name = "PIT 8254 Timer",
        .type = DeviceType::Timer,
        .state = DeviceState::Active,
        .driver_name = "PitTimer",
        .resource_addr = 0x40,
        .irq = 0
    });

    // 3. PS/2 Subsystem
    register_device(DeviceInfo{
        .name = "i8042 PS/2 Controller",
        .type = DeviceType::Ps2Controller,
        .state = Ps2Controller::is_initialized() ? DeviceState::Active : DeviceState::Disabled,
        .driver_name = "Ps2Controller",
        .resource_addr = 0x60,
        .irq = 0xFFFFFFFF
    });

    register_device(DeviceInfo{
        .name = "PS/2 Keyboard",
        .type = DeviceType::Keyboard,
        .state = Keyboard::is_initialized() ? DeviceState::Active : DeviceState::Disabled,
        .driver_name = "Keyboard",
        .resource_addr = 0x60,
        .irq = 1
    });

    // 4. Linear Framebuffer
    if (Framebuffer::is_available()) {
        register_device(DeviceInfo{
            .name = "Linear Framebuffer",
            .type = DeviceType::Framebuffer,
            .state = DeviceState::Active,
            .driver_name = "Framebuffer",
            .resource_addr = Framebuffer::physical_address(),
            .irq = 0xFFFFFFFF
        });
    }

    // 5. Discovered PCI Devices
    const size_t pci_count = PciManager::device_count();
    const PciDevice* pci_devs = PciManager::devices();
    for (size_t i = 0; i < pci_count; ++i) {
        const PciDevice& d = pci_devs[i];
        uint64_t primary_addr = 0;
        for (size_t b = 0; b < 6; ++b) {
            if (d.bars[b].valid) {
                primary_addr = d.bars[b].base_address;
                break;
            }
        }
        register_device(DeviceInfo{
            .name = d.class_string(),
            .type = DeviceType::PciDevice,
            .state = DeviceState::Active,
            .driver_name = "PciManager",
            .resource_addr = primary_addr,
            .irq = 0xFFFFFFFF
        });
    }
}

} // namespace llamaos::drivers
