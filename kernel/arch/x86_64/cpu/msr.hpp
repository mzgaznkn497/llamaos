#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - x86-64 Model Specific Registers (MSR)
// =============================================================================

namespace llamaos::arch::x86_64 {

enum class Msr : uint32_t {
    ApicBase       = 0x0000001B,
    Efer           = 0xC0000080,
    Star           = 0xC0000081,
    Lstar          = 0xC0000082,
    Cstar          = 0xC0000083,
    Sfmask         = 0xC0000084,
    FsBase         = 0xC0000100,
    GsBase         = 0xC0000101,
    KernelGsBase   = 0xC0000102
};

inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return (static_cast<uint64_t>(high) << 32) | low;
}

inline uint64_t rdmsr(Msr msr) {
    return rdmsr(static_cast<uint32_t>(msr));
}

inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t low = static_cast<uint32_t>(val);
    uint32_t high = static_cast<uint32_t>(val >> 32);
    asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

inline void wrmsr(Msr msr, uint64_t val) {
    wrmsr(static_cast<uint32_t>(msr), val);
}

} // namespace llamaos::arch::x86_64
