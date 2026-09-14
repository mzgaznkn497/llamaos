#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - System Power Management (Reboot, Shutdown, Reset)
// =============================================================================
// Implements multi-tier reboot (PS/2 8042, PCI 0xCF9, triple fault) and
// shutdown (ACPI poweroff, QEMU 0x604/0xB004, debug exit 0xF4).
// =============================================================================

namespace llamaos::core {

class PowerManager {
public:
    // Flush storage and restart system
    [[noreturn]] static void reboot();

    // Flush storage and power down system
    [[noreturn]] static void poweroff();

    // Emergency halt without flushing
    [[noreturn]] static void emergency_halt();
};

} // namespace llamaos::core
