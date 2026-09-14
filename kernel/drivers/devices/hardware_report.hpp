#pragma once

#include "core/types.hpp"

namespace llamaos::drivers {

class HardwareReport {
public:
    // Formats and prints a unified hardware discovery summary to the console
    static void display() noexcept;
};

} // namespace llamaos::drivers
