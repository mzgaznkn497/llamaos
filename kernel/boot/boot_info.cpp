#include "boot/boot_info.hpp"
#include "core/string.hpp"

// =============================================================================
// LlamaOS/A - Boot Information Abstraction Implementation (Blocker 7)
// =============================================================================

namespace llamaos::boot {

// Boot information utility functions and subsystem queries

bool BootInformation::is_in_kernel_image(uint64_t paddr, uint64_t length) const {
    extern uint8_t _kernel_physical_start[] __attribute__((weak));
    extern uint8_t _kernel_physical_end[] __attribute__((weak));

    uint64_t kstart = reinterpret_cast<uint64_t>(_kernel_physical_start);
    uint64_t kend   = reinterpret_cast<uint64_t>(_kernel_physical_end);

    uint64_t rstart = paddr;
    uint64_t rend   = paddr + length;

    return (rstart < kend) && (rend > kstart);
}

bool BootInformation::is_in_multiboot_info(uint64_t paddr, uint64_t length) const {
    if (mb2_info_paddr == 0 || mb2_info_total_size == 0) return false;

    uint64_t mstart = mb2_info_paddr;
    uint64_t mend   = mb2_info_paddr + mb2_info_total_size;

    uint64_t rstart = paddr;
    uint64_t rend   = paddr + length;

    return (rstart < mend) && (rend > mstart);
}

} // namespace llamaos::boot
