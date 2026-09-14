#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - Strongly-Typed Memory Architecture Primitives
// =============================================================================
// Establishes distinct, strongly-typed representations for physical addresses,
// virtual addresses, page frame numbers, page counts, and page table permission flags.
// Eliminates type-confusion bugs between physical and virtual address spaces.
// =============================================================================

namespace llamaos::memory {

class VirtualAddress;

// -----------------------------------------------------------------------------
// PhysicalAddress: Encapsulates an x86-64 hardware physical memory address
// -----------------------------------------------------------------------------
class PhysicalAddress {
public:
    explicit constexpr PhysicalAddress(uint64_t addr = 0) noexcept : m_addr(addr) {}

    [[nodiscard]] constexpr uint64_t value() const noexcept { return m_addr; }
    [[nodiscard]] constexpr bool is_null() const noexcept { return m_addr == 0; }
    [[nodiscard]] constexpr bool is_page_aligned() const noexcept { return (m_addr & (PAGE_SIZE - 1)) == 0; }
    [[nodiscard]] constexpr bool is_aligned(size_t alignment) const noexcept {
        return alignment != 0 && (m_addr % alignment) == 0;
    }

    [[nodiscard]] constexpr bool is_direct_mapped() const noexcept {
        return m_addr < 0x80000000ULL; // 2 GiB direct-mapped boot window
    }

    [[nodiscard]] constexpr PhysicalAddress align_up(size_t alignment) const noexcept {
        if (alignment == 0) return *this;
        uint64_t res = m_addr;
        if (!llamaos::align_up_checked(m_addr, static_cast<uint64_t>(alignment), res)) {
            return PhysicalAddress(UINT64_MAX);
        }
        return PhysicalAddress(res);
    }

    [[nodiscard]] constexpr PhysicalAddress align_down(size_t alignment) const noexcept {
        if (alignment == 0) return *this;
        return PhysicalAddress(m_addr - (m_addr % alignment));
    }

    [[nodiscard]] constexpr PhysicalAddress offset(int64_t off) const noexcept {
        return PhysicalAddress(static_cast<uint64_t>(static_cast<int64_t>(m_addr) + off));
    }

    // Direct higher-half mapping conversion (valid for physical addresses within boot 2 GiB window)
    [[nodiscard]] constexpr VirtualAddress to_higher_half() const noexcept;

    constexpr bool operator==(const PhysicalAddress& other) const noexcept { return m_addr == other.m_addr; }
    constexpr bool operator!=(const PhysicalAddress& other) const noexcept { return m_addr != other.m_addr; }
    constexpr bool operator<(const PhysicalAddress& other) const noexcept { return m_addr < other.m_addr; }
    constexpr bool operator<=(const PhysicalAddress& other) const noexcept { return m_addr <= other.m_addr; }
    constexpr bool operator>(const PhysicalAddress& other) const noexcept { return m_addr > other.m_addr; }
    constexpr bool operator>=(const PhysicalAddress& other) const noexcept { return m_addr >= other.m_addr; }

    constexpr PhysicalAddress operator+(uint64_t bytes) const noexcept { return PhysicalAddress(m_addr + bytes); }
    constexpr PhysicalAddress operator-(uint64_t bytes) const noexcept { return PhysicalAddress(m_addr - bytes); }
    constexpr int64_t operator-(const PhysicalAddress& other) const noexcept {
        return static_cast<int64_t>(m_addr - other.m_addr);
    }

    PhysicalAddress& operator+=(uint64_t bytes) noexcept {
        m_addr += bytes;
        return *this;
    }

    PhysicalAddress& operator-=(uint64_t bytes) noexcept {
        m_addr -= bytes;
        return *this;
    }

private:
    uint64_t m_addr{0};
};

// -----------------------------------------------------------------------------
// VirtualAddress: Encapsulates an x86-64 canonical virtual memory address
// -----------------------------------------------------------------------------
class VirtualAddress {
public:
    explicit constexpr VirtualAddress(uintptr_t addr = 0) noexcept : m_addr(addr) {}

    template <typename T>
    explicit VirtualAddress(T* ptr) noexcept : m_addr(reinterpret_cast<uintptr_t>(ptr)) {}

    [[nodiscard]] constexpr uintptr_t value() const noexcept { return m_addr; }
    [[nodiscard]] void* as_ptr() const noexcept { return reinterpret_cast<void*>(m_addr); }
    [[nodiscard]] const void* as_const_ptr() const noexcept { return reinterpret_cast<const void*>(m_addr); }

    template <typename T>
    [[nodiscard]] T* as() const noexcept {
        return reinterpret_cast<T*>(m_addr);
    }

    [[nodiscard]] constexpr bool is_null() const noexcept { return m_addr == 0; }

    // In 48-bit canonical addressing, bits 47 to 63 must match bit 47.
    [[nodiscard]] constexpr bool is_canonical() const noexcept {
        uint64_t high_bits = m_addr >> 47;
        return (high_bits == 0) || (high_bits == 0x1FFFFULL);
    }

    [[nodiscard]] constexpr bool is_higher_half() const noexcept {
        return m_addr >= 0xFFFF800000000000ULL;
    }

    [[nodiscard]] constexpr bool is_page_aligned() const noexcept {
        return (m_addr & (PAGE_SIZE - 1)) == 0;
    }

    [[nodiscard]] constexpr bool is_aligned(size_t alignment) const noexcept {
        return alignment != 0 && (m_addr % alignment) == 0;
    }

    [[nodiscard]] constexpr VirtualAddress align_up(size_t alignment) const noexcept {
        if (alignment == 0) return *this;
        uintptr_t res = m_addr;
        if (!llamaos::align_up_checked(m_addr, static_cast<uintptr_t>(alignment), res)) {
            return VirtualAddress(UINTPTR_MAX);
        }
        return VirtualAddress(res);
    }

    [[nodiscard]] constexpr VirtualAddress align_down(size_t alignment) const noexcept {
        if (alignment == 0) return *this;
        return VirtualAddress(m_addr - (m_addr % alignment));
    }

    [[nodiscard]] constexpr VirtualAddress offset(int64_t off) const noexcept {
        return VirtualAddress(static_cast<uintptr_t>(static_cast<int64_t>(m_addr) + off));
    }

    // Direct translation to physical address for higher-half boot window
    [[nodiscard]] constexpr PhysicalAddress to_physical() const noexcept {
        return PhysicalAddress(m_addr - KERNEL_VIRTUAL_BASE);
    }

    // x86-64 4-Level Page Directory Indices
    [[nodiscard]] constexpr size_t pml4_index() const noexcept { return (m_addr >> 39) & 0x1FFULL; }
    [[nodiscard]] constexpr size_t pdpt_index() const noexcept { return (m_addr >> 30) & 0x1FFULL; }
    [[nodiscard]] constexpr size_t pd_index() const noexcept   { return (m_addr >> 21) & 0x1FFULL; }
    [[nodiscard]] constexpr size_t pt_index() const noexcept   { return (m_addr >> 12) & 0x1FFULL; }
    [[nodiscard]] constexpr uint64_t page_offset() const noexcept { return m_addr & 0xFFFULL; }

    constexpr bool operator==(const VirtualAddress& other) const noexcept { return m_addr == other.m_addr; }
    constexpr bool operator!=(const VirtualAddress& other) const noexcept { return m_addr != other.m_addr; }
    constexpr bool operator<(const VirtualAddress& other) const noexcept { return m_addr < other.m_addr; }
    constexpr bool operator<=(const VirtualAddress& other) const noexcept { return m_addr <= other.m_addr; }
    constexpr bool operator>(const VirtualAddress& other) const noexcept { return m_addr > other.m_addr; }
    constexpr bool operator>=(const VirtualAddress& other) const noexcept { return m_addr >= other.m_addr; }

    constexpr VirtualAddress operator+(uintptr_t bytes) const noexcept { return VirtualAddress(m_addr + bytes); }
    constexpr VirtualAddress operator-(uintptr_t bytes) const noexcept { return VirtualAddress(m_addr - bytes); }
    constexpr intptr_t operator-(const VirtualAddress& other) const noexcept {
        return static_cast<intptr_t>(m_addr - other.m_addr);
    }

    VirtualAddress& operator+=(uintptr_t bytes) noexcept {
        m_addr += bytes;
        return *this;
    }

    VirtualAddress& operator-=(uintptr_t bytes) noexcept {
        m_addr -= bytes;
        return *this;
    }

private:
    uintptr_t m_addr{0};
};

inline constexpr VirtualAddress PhysicalAddress::to_higher_half() const noexcept {
    return VirtualAddress(m_addr + KERNEL_VIRTUAL_BASE);
}

inline constexpr VirtualAddress phys_to_virt(PhysicalAddress phys) noexcept {
    return phys.to_higher_half();
}

inline constexpr PhysicalAddress virt_to_phys(VirtualAddress virt) noexcept {
    return virt.to_physical();
}

// -----------------------------------------------------------------------------
// PageFrameNumber: Encapsulates a 4 KiB physical page frame index
// -----------------------------------------------------------------------------
class PageFrameNumber {
public:
    explicit constexpr PageFrameNumber(uint64_t pfn = 0) noexcept : m_pfn(pfn) {}

    static constexpr PageFrameNumber from_address(PhysicalAddress addr) noexcept {
        return PageFrameNumber(addr.value() / PAGE_SIZE);
    }

    [[nodiscard]] constexpr PhysicalAddress to_address() const noexcept {
        return PhysicalAddress(m_pfn * PAGE_SIZE);
    }

    [[nodiscard]] constexpr uint64_t value() const noexcept { return m_pfn; }

    constexpr bool operator==(const PageFrameNumber& other) const noexcept { return m_pfn == other.m_pfn; }
    constexpr bool operator!=(const PageFrameNumber& other) const noexcept { return m_pfn != other.m_pfn; }
    constexpr bool operator<(const PageFrameNumber& other) const noexcept { return m_pfn < other.m_pfn; }
    constexpr bool operator<=(const PageFrameNumber& other) const noexcept { return m_pfn <= other.m_pfn; }
    constexpr bool operator>(const PageFrameNumber& other) const noexcept { return m_pfn > other.m_pfn; }
    constexpr bool operator>=(const PageFrameNumber& other) const noexcept { return m_pfn >= other.m_pfn; }

    constexpr PageFrameNumber operator+(uint64_t n) const noexcept { return PageFrameNumber(m_pfn + n); }
    constexpr PageFrameNumber operator-(uint64_t n) const noexcept { return PageFrameNumber(m_pfn - n); }
    PageFrameNumber& operator++() noexcept { ++m_pfn; return *this; }
    PageFrameNumber operator++(int) noexcept { PageFrameNumber tmp = *this; ++m_pfn; return tmp; }
    PageFrameNumber& operator--() noexcept { --m_pfn; return *this; }
    PageFrameNumber operator--(int) noexcept { PageFrameNumber tmp = *this; --m_pfn; return tmp; }

private:
    uint64_t m_pfn{0};
};

// -----------------------------------------------------------------------------
// PageCount: Represents a typed quantity of 4 KiB memory pages
// -----------------------------------------------------------------------------
class PageCount {
public:
    explicit constexpr PageCount(size_t count = 0) noexcept : m_count(count) {}

    static constexpr PageCount from_bytes(uint64_t bytes) noexcept {
        if (bytes == 0) return PageCount(0);
        uint64_t whole_pages = bytes / PAGE_SIZE;
        uint64_t rem = bytes % PAGE_SIZE;
        uint64_t total = whole_pages + (rem != 0 ? 1ULL : 0ULL);
        return PageCount(static_cast<size_t>(total));
    }

    [[nodiscard]] constexpr size_t value() const noexcept { return m_count; }
    [[nodiscard]] constexpr uint64_t to_bytes() const noexcept { return static_cast<uint64_t>(m_count) * PAGE_SIZE; }

    constexpr bool operator==(const PageCount& other) const noexcept { return m_count == other.m_count; }
    constexpr bool operator!=(const PageCount& other) const noexcept { return m_count != other.m_count; }
    constexpr bool operator<(const PageCount& other) const noexcept { return m_count < other.m_count; }
    constexpr bool operator<=(const PageCount& other) const noexcept { return m_count <= other.m_count; }
    constexpr bool operator>(const PageCount& other) const noexcept { return m_count > other.m_count; }
    constexpr bool operator>=(const PageCount& other) const noexcept { return m_count >= other.m_count; }

    constexpr PageCount operator+(const PageCount& other) const noexcept { return PageCount(m_count + other.m_count); }
    constexpr PageCount operator-(const PageCount& other) const noexcept { return PageCount(m_count - other.m_count); }

    PageCount& operator+=(const PageCount& other) noexcept {
        m_count += other.m_count;
        return *this;
    }

    PageCount& operator-=(const PageCount& other) noexcept {
        m_count -= other.m_count;
        return *this;
    }

private:
    size_t m_count{0};
};

// -----------------------------------------------------------------------------
// PageFlags: Architectural Page Table Entry Attribute Flags (x86-64)
// -----------------------------------------------------------------------------
enum class PageFlags : uint64_t {
    None         = 0ULL,
    Present      = 1ULL << 0,   // P   - Present in memory
    Writable     = 1ULL << 1,   // R/W - Read/Write (1 = Writable, 0 = Read-Only)
    User         = 1ULL << 2,   // U/S - User/Supervisor (1 = User, 0 = Ring 0 only)
    WriteThrough = 1ULL << 3,   // PWT - Page-level write-through
    CacheDisable = 1ULL << 4,   // PCD - Page-level cache disable
    Accessed     = 1ULL << 5,   // A   - Page was accessed by CPU
    Dirty        = 1ULL << 6,   // D   - Page was written to by CPU
    HugePage     = 1ULL << 7,   // PS  - 2 MiB / 1 GiB page size
    Global       = 1ULL << 8,   // G   - Translation global across CR3 switches
    NoExecute    = 1ULL << 63   // NX  - Execute-Disable bit (IA32_EFER.NXE)
};

inline constexpr PageFlags operator|(PageFlags a, PageFlags b) noexcept {
    return static_cast<PageFlags>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}

inline constexpr PageFlags operator&(PageFlags a, PageFlags b) noexcept {
    return static_cast<PageFlags>(static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
}

inline constexpr PageFlags operator^(PageFlags a, PageFlags b) noexcept {
    return static_cast<PageFlags>(static_cast<uint64_t>(a) ^ static_cast<uint64_t>(b));
}

inline constexpr PageFlags operator~(PageFlags a) noexcept {
    return static_cast<PageFlags>(~static_cast<uint64_t>(a));
}

inline constexpr PageFlags& operator|=(PageFlags& a, PageFlags b) noexcept {
    a = a | b;
    return a;
}

inline constexpr PageFlags& operator&=(PageFlags& a, PageFlags b) noexcept {
    a = a & b;
    return a;
}

inline constexpr bool test_flag(PageFlags val, PageFlags flag) noexcept {
    return (static_cast<uint64_t>(val) & static_cast<uint64_t>(flag)) == static_cast<uint64_t>(flag);
}

} // namespace llamaos::memory
