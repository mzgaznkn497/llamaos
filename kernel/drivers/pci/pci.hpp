#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - PCI (Peripheral Component Interconnect) Bus Enumeration
// =============================================================================
// Implements configuration-space scanning via standard I/O ports 0xCF8/0xCFC.
// Safely discovers devices, parses headers, decodes I/O and 32/64-bit memory
// Base Address Registers (BARs) in read-only mode, and builds a device list.
// =============================================================================

namespace llamaos::drivers {

struct PciBar {
    bool valid{false};
    bool is_io{false};
    bool is_64bit{false};
    bool is_prefetchable{false};
    uint64_t base_address{0};
};

struct PciDevice {
    uint8_t  bus{0};
    uint8_t  device{0};
    uint8_t  function{0};

    uint16_t vendor_id{0xFFFF};
    uint16_t device_id{0xFFFF};
    uint8_t  class_code{0};
    uint8_t  subclass{0};
    uint8_t  prog_if{0};
    uint8_t  revision{0};
    uint8_t  header_type{0};

    PciBar   bars[6]{};
    size_t   bar_count{0};

    const char* class_string() const noexcept;
};

class PciManager {
public:
    static constexpr uint16_t CONFIG_ADDRESS_PORT = 0xCF8;
    static constexpr uint16_t CONFIG_DATA_PORT    = 0xCFC;
    static constexpr size_t   MAX_DEVICES         = 32;

    // Config space low-level primitives
    static uint32_t read_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) noexcept;
    static uint16_t read_config16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) noexcept;
    static uint8_t  read_config8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) noexcept;

    static void write_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) noexcept;
    static void write_config16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t value) noexcept;
    static void write_config8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint8_t value) noexcept;

    // Device control helpers
    static void enable_bus_mastering(const PciDevice& dev) noexcept;
    static void enable_io_space(const PciDevice& dev) noexcept;
    static void enable_memory_space(const PciDevice& dev) noexcept;

    // Address construction helper (pure, host testable)
    static constexpr uint32_t make_config_address(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) noexcept {
        return (1U << 31)
             | (static_cast<uint32_t>(bus) << 16)
             | (static_cast<uint32_t>(dev & 0x1F) << 11)
             | (static_cast<uint32_t>(func & 0x07) << 8)
             | (static_cast<uint32_t>(offset & 0xFC));
    }

    // Scans all buses and populates the device table
    static bool init();

    // Query discovered devices
    static size_t device_count() noexcept { return s_device_count; }
    static const PciDevice* devices() noexcept { return s_devices; }
    static const PciDevice* find_device(uint16_t vendor_id, uint16_t device_id) noexcept;
    static const PciDevice* find_by_class(uint8_t class_code, uint8_t subclass) noexcept;

    // Helper: decode BAR metadata without writing to hardware
    static PciBar decode_bar(uint32_t bar_low, uint32_t bar_high = 0) noexcept;

    // Helper: format class code to readable name
    static const char* format_class(uint8_t class_code, uint8_t subclass) noexcept;

private:
    static PciDevice s_devices[MAX_DEVICES];
    static size_t    s_device_count;
    static bool      s_initialized;

    static void probe_device(uint8_t bus, uint8_t dev, uint8_t func) noexcept;
};

} // namespace llamaos::drivers
