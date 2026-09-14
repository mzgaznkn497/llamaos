#pragma once

#include "core/types.hpp"
#include "arch/x86_64/boot/multiboot2.hpp"

// =============================================================================
// LlamaOS/A - Linear Framebuffer Driver & Abstraction
// =============================================================================
// Encapsulates physical and virtual linear framebuffer access negotiated
// via Multiboot2 (UEFI GOP or BIOS VBE). Validates geometry, prevents overflow,
// establishes safe virtual memory mapping, and provides basic 2D drawing primitives.
// =============================================================================

namespace llamaos::drivers {

enum class PixelFormat : uint8_t {
    Unknown = 0,
    RGB888,    // 24-bit RGB
    BGR888,    // 24-bit BGR
    ARGB8888,  // 32-bit ARGB
    RGBA8888,  // 32-bit RGBA
    ABGR8888,  // 32-bit ABGR
    BGRA8888   // 32-bit BGRA
};

class Framebuffer {
public:
    // Initializes the framebuffer from boot information
    static bool init(const boot::FramebufferInfo& info);

    // Primitives
    static void put_pixel(uint32_t x, uint32_t y, uint32_t color);
    static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    static void clear(uint32_t color = 0x00000000);

    // Geometry queries
    static bool is_available() noexcept { return s_available; }
    static uint64_t physical_address() noexcept { return s_paddr; }
    static uintptr_t virtual_address() noexcept { return s_vaddr; }
    static uint32_t width() noexcept { return s_width; }
    static uint32_t height() noexcept { return s_height; }
    static uint32_t pitch() noexcept { return s_pitch; }
    static uint8_t bpp() noexcept { return s_bpp; }
    static uint64_t total_size_bytes() noexcept { return s_total_size; }
    static PixelFormat format() noexcept { return s_format; }

    // Pure validation helper for host tests
    static bool validate_metadata(uint64_t addr, uint32_t pitch, uint32_t width,
                                  uint32_t height, uint8_t bpp, uint8_t type,
                                  uint64_t* out_total_size = nullptr) noexcept;

    // Full mapping plan structure capturing non-truncated virtual memory layout
    struct MappingPlan {
        bool valid{false};
        uint64_t aligned_paddr{0};
        uint64_t page_offset{0};
        uint64_t total_mapped_bytes{0};
        size_t num_pages{0};
        uintptr_t base_vaddr{0};
        uintptr_t framebuffer_vaddr{0};
        bool uses_direct_map{false};
    };

    // Pure mapping plan calculator with checked arithmetic and MMIO capacity validation
    static MappingPlan calculate_mapping_plan(uint64_t paddr, uint64_t total_size) noexcept;

    // Pure bounds and overflow validator for pixel coordinate accesses
    static bool is_pixel_in_bounds(uint32_t x, uint32_t y, uint8_t bpp, uint32_t pitch,
                                   uint32_t width, uint32_t height, uint64_t total_size,
                                   uint64_t* out_offset = nullptr) noexcept;

private:
    static bool s_available;
    static uint64_t s_paddr;
    static uintptr_t s_vaddr;
    static uint32_t s_width;
    static uint32_t s_height;
    static uint32_t s_pitch;
    static uint8_t s_bpp;
    static uint64_t s_total_size;
    static PixelFormat s_format;

    static PixelFormat detect_format(const boot::FramebufferInfo& info) noexcept;
};

} // namespace llamaos::drivers
