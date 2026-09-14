#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - x86-64 CPU Control and Feature Detection
// =============================================================================

namespace llamaos::arch::x86_64 {

struct CpuidResult {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};

inline CpuidResult cpuid(uint32_t leaf, uint32_t subleaf = 0) {
    CpuidResult res{};
    asm volatile("cpuid"
                 : "=a"(res.eax), "=b"(res.ebx), "=c"(res.ecx), "=d"(res.edx)
                 : "a"(leaf), "c"(subleaf));
    return res;
}

inline uint64_t read_cr0() {
    uint64_t val;
    asm volatile("mov %%cr0, %0" : "=r"(val));
    return val;
}

inline void write_cr0(uint64_t val) {
    asm volatile("mov %0, %%cr0" : : "r"(val));
}

inline uint64_t read_cr2() {
    uint64_t val;
    asm volatile("mov %%cr2, %0" : "=r"(val));
    return val;
}

inline uint64_t read_cr3() {
    uint64_t val;
    asm volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

inline void write_cr3(uint64_t val) {
    asm volatile("mov %0, %%cr3" : : "r"(val));
}

inline uint64_t read_cr4() {
    uint64_t val;
    asm volatile("mov %%cr4, %0" : "=r"(val));
    return val;
}

inline void write_cr4(uint64_t val) {
    asm volatile("mov %0, %%cr4" : : "r"(val));
}

inline uint64_t read_rflags() {
    uint64_t flags;
    asm volatile("pushfq; popq %0" : "=r"(flags));
    return flags;
}

inline uint64_t read_msr(uint32_t msr) {
    uint32_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return (static_cast<uint64_t>(high) << 32) | low;
}

inline void write_msr(uint32_t msr, uint64_t val) {
    uint32_t low = static_cast<uint32_t>(val);
    uint32_t high = static_cast<uint32_t>(val >> 32);
    asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

inline uint64_t rdtsc() {
    uint32_t low, high;
    asm volatile("rdtsc" : "=a"(low), "=d"(high));
    return (static_cast<uint64_t>(high) << 32) | low;
}

struct [[gnu::packed]] Gdtr {
    uint16_t limit;
    uint64_t base;
};

inline void sgdt(Gdtr& gdtr) {
    asm volatile("sgdt %0" : "=m"(gdtr));
}

inline void lgdt(const Gdtr& gdtr) {
    asm volatile("lgdt %0" : : "m"(gdtr));
}

inline void halt() {
    asm volatile("hlt");
}

inline void pause() {
    asm volatile("pause");
}

inline void cli() {
    asm volatile("cli");
}

inline void sti() {
    asm volatile("sti");
}

struct CpuFeatures {
    bool fpu;
    bool vme;
    bool de;
    bool pse;
    bool tsc;
    bool msr;
    bool pae;
    bool mce;
    bool cx8;
    bool apic;
    bool sep;
    bool mtrr;
    bool pge;
    bool mca;
    bool cmov;
    bool pat;
    bool pse36;
    bool clfsh;
    bool mmx;
    bool fxsr;
    bool sse;
    bool sse2;
    bool sse3;
    bool pclmul;
    bool ssse3;
    bool fma;
    bool cx16;
    bool sse4_1;
    bool sse4_2;
    bool x2apic;
    bool movbe;
    bool popcnt;
    bool aes;
    bool xsave;
    bool osxsave;
    bool avx;
    bool f16c;
    bool rdrand;
    bool fsgsbase;
    bool smep;
    bool smap;
    bool nx;
    bool page1gb;
    bool lm;
};

struct CpuInfo {
    char vendor[13];
    char brand[49];
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    CpuFeatures features;

    void detect();
};

extern CpuInfo g_cpu_info;

} // namespace llamaos::arch::x86_64
