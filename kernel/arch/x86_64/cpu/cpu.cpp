#include "cpu.hpp"
#include "core/string.hpp"

// =============================================================================
// LlamaOS/A - x86-64 CPU Identification and Feature Enumeration
// =============================================================================

namespace llamaos::arch::x86_64 {

CpuInfo g_cpu_info{};

void CpuInfo::detect() {
    memset(this, 0, sizeof(CpuInfo));

    // 1. Vendor string (CPUID leaf 0)
    CpuidResult leaf0 = cpuid(0);
    uint32_t max_leaf = leaf0.eax;
    *reinterpret_cast<uint32_t*>(&vendor[0]) = leaf0.ebx;
    *reinterpret_cast<uint32_t*>(&vendor[4]) = leaf0.edx;
    *reinterpret_cast<uint32_t*>(&vendor[8]) = leaf0.ecx;
    vendor[12] = '\0';

    // 2. Family, model, stepping & standard features (CPUID leaf 1)
    if (max_leaf >= 1) {
        CpuidResult leaf1 = cpuid(1);
        stepping = leaf1.eax & 0x0F;
        uint32_t base_model = (leaf1.eax >> 4) & 0x0F;
        uint32_t base_family = (leaf1.eax >> 8) & 0x0F;
        uint32_t ext_model = (leaf1.eax >> 16) & 0x0F;
        uint32_t ext_family = (leaf1.eax >> 20) & 0xFF;

        family = base_family;
        if (base_family == 0x0F) {
            family += ext_family;
        }

        model = base_model;
        if (base_family == 0x06 || base_family == 0x0F) {
            model += (ext_model << 4);
        }

        // Standard features (EDX)
        features.fpu    = (leaf1.edx & (1 << 0)) != 0;
        features.vme    = (leaf1.edx & (1 << 1)) != 0;
        features.de     = (leaf1.edx & (1 << 2)) != 0;
        features.pse    = (leaf1.edx & (1 << 3)) != 0;
        features.tsc    = (leaf1.edx & (1 << 4)) != 0;
        features.msr    = (leaf1.edx & (1 << 5)) != 0;
        features.pae    = (leaf1.edx & (1 << 6)) != 0;
        features.mce    = (leaf1.edx & (1 << 7)) != 0;
        features.cx8    = (leaf1.edx & (1 << 8)) != 0;
        features.apic   = (leaf1.edx & (1 << 9)) != 0;
        features.sep    = (leaf1.edx & (1 << 11)) != 0;
        features.mtrr   = (leaf1.edx & (1 << 12)) != 0;
        features.pge    = (leaf1.edx & (1 << 13)) != 0;
        features.mca    = (leaf1.edx & (1 << 14)) != 0;
        features.cmov   = (leaf1.edx & (1 << 15)) != 0;
        features.pat    = (leaf1.edx & (1 << 16)) != 0;
        features.pse36  = (leaf1.edx & (1 << 17)) != 0;
        features.clfsh  = (leaf1.edx & (1 << 19)) != 0;
        features.mmx    = (leaf1.edx & (1 << 23)) != 0;
        features.fxsr   = (leaf1.edx & (1 << 24)) != 0;
        features.sse    = (leaf1.edx & (1 << 25)) != 0;
        features.sse2   = (leaf1.edx & (1 << 26)) != 0;

        // Standard features (ECX)
        features.sse3         = (leaf1.ecx & (1 << 0)) != 0;
        features.pclmul       = (leaf1.ecx & (1 << 1)) != 0;
        features.ssse3        = (leaf1.ecx & (1 << 9)) != 0;
        features.fma          = (leaf1.ecx & (1 << 12)) != 0;
        features.cx16         = (leaf1.ecx & (1 << 13)) != 0;
        features.sse4_1       = (leaf1.ecx & (1 << 19)) != 0;
        features.sse4_2       = (leaf1.ecx & (1 << 20)) != 0;
        features.x2apic       = (leaf1.ecx & (1 << 21)) != 0;
        features.movbe        = (leaf1.ecx & (1 << 22)) != 0;
        features.popcnt       = (leaf1.ecx & (1 << 23)) != 0;
        features.aes          = (leaf1.ecx & (1 << 25)) != 0;
        features.xsave        = (leaf1.ecx & (1 << 26)) != 0;
        features.osxsave      = (leaf1.ecx & (1 << 27)) != 0;
        features.avx          = (leaf1.ecx & (1 << 28)) != 0;
        features.f16c         = (leaf1.ecx & (1 << 29)) != 0;
        features.rdrand       = (leaf1.ecx & (1 << 30)) != 0;
    }

    // 3. Structured Extended Features (CPUID leaf 7, subleaf 0)
    if (max_leaf >= 7) {
        CpuidResult leaf7 = cpuid(7, 0);
        features.fsgsbase = (leaf7.ebx & (1 << 0)) != 0;
        features.smep     = (leaf7.ebx & (1 << 7)) != 0;
        features.smap     = (leaf7.ebx & (1 << 20)) != 0;
    }

    // 4. Extended functions (CPUID leaf 0x80000000)
    CpuidResult ext_leaf0 = cpuid(0x80000000);
    uint32_t max_ext_leaf = ext_leaf0.eax;

    if (max_ext_leaf >= 0x80000001) {
        CpuidResult ext_leaf1 = cpuid(0x80000001);
        features.nx      = (ext_leaf1.edx & (1 << 20)) != 0; // NX bit (No-Execute)
        features.page1gb = (ext_leaf1.edx & (1 << 26)) != 0; // 1 GiB pages
        features.lm      = (ext_leaf1.edx & (1 << 29)) != 0; // Long Mode (64-bit)
    }

    // Brand string (CPUID leaves 0x80000002 .. 0x80000004)
    if (max_ext_leaf >= 0x80000004) {
        auto* brand_u32 = reinterpret_cast<uint32_t*>(brand);
        CpuidResult b0 = cpuid(0x80000002);
        brand_u32[0] = b0.eax; brand_u32[1] = b0.ebx; brand_u32[2] = b0.ecx; brand_u32[3] = b0.edx;
        CpuidResult b1 = cpuid(0x80000003);
        brand_u32[4] = b1.eax; brand_u32[5] = b1.ebx; brand_u32[6] = b1.ecx; brand_u32[7] = b1.edx;
        CpuidResult b2 = cpuid(0x80000004);
        brand_u32[8] = b2.eax; brand_u32[9] = b2.ebx; brand_u32[10] = b2.ecx; brand_u32[11] = b2.edx;
        brand[48] = '\0';

        // Trim leading spaces from brand string
        char* p = brand;
        while (*p == ' ') p++;
        if (p != brand) {
            memmove(brand, p, strlen(p) + 1);
        }
    } else {
        strcpy(brand, "Generic x86-64 Processor");
    }
}

} // namespace llamaos::arch::x86_64
