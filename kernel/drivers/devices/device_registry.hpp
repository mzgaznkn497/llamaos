#pragma once

#include "core/types.hpp"

namespace llamaos::drivers {

enum class DeviceType : uint8_t {
    Unknown,
    Console,
    SerialPort,
    VgaDisplay,
    Framebuffer,
    Ps2Controller,
    Keyboard,
    PciBus,
    PciDevice,
    Timer,
    PicInterruptController
};

enum class DeviceState : uint8_t {
    Uninitialized,
    Active,
    Standby,
    Failed,
    Disabled
};

struct DeviceInfo {
    const char* name{""};
    DeviceType  type{DeviceType::Unknown};
    DeviceState state{DeviceState::Uninitialized};
    const char* driver_name{""};
    uint64_t    resource_addr{0};
    uint32_t    irq{0xFFFFFFFF};
};

class DeviceRegistry {
public:
    static constexpr size_t MAX_REGISTERED_DEVICES = 64;

    static void init() noexcept;
    static bool register_device(const DeviceInfo& info) noexcept;
    static size_t device_count() noexcept { return s_device_count; }
    static const DeviceInfo* get_device(size_t index) noexcept;
    static const DeviceInfo* find_by_name(const char* name) noexcept;

    // Populate registry with all detected hardware subsystems
    static void populate_detected_devices() noexcept;

    // Helper: string representation of device type and state
    static const char* type_to_string(DeviceType type) noexcept;
    static const char* state_to_string(DeviceState state) noexcept;

private:
    static DeviceInfo s_devices[MAX_REGISTERED_DEVICES];
    static size_t     s_device_count;
};

} // namespace llamaos::drivers
