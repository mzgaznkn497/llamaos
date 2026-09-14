#include "framebuffer.hpp"
#include "core/kprint.hpp"
#include "memory/memory_layout.hpp"
#include "memory/vmm.hpp"

// =============================================================================
// LlamaOS/A - Linear Framebuffer Driver Implementation
// =============================================================================

namespace llamaos::drivers {

bool Framebuffer::s_available{false};
uint64_t Framebuffer::s_paddr{0};
uintptr_t Framebuffer::s_vaddr{0};
uint32_t Framebuffer::s_width{0};
uint32_t Framebuffer::s_height{0};
uint32_t Framebuffer::s_pitch{0};
uint8_t Framebuffer::s_bpp{0};
uint64_t Framebuffer::s_total_size{0};
PixelFormat Framebuffer::s_format{PixelFormat::Unknown};

bool Framebuffer::validate_metadata(uint64_t addr, uint32_t pitch, uint32_t width,
                                    uint32_t height, uint8_t bpp, uint8_t type,
                                    uint64_t* out_total_size) noexcept {
    if (addr == 0) return false;
    if (width == 0 || width > 7680) return false;
    if (height == 0 || height > 4320) return false;
    if (bpp != 16 && bpp != 24 && bpp != 32) return false;
    if (type != 1) return false; // Only Direct RGB graphics mode supported

    const uint64_t min_pitch = (static_cast<uint64_t>(width) * bpp + 7) / 8;
    if (pitch < min_pitch) return false;

    if (pitch > 0 && height > (UINT64_MAX / pitch)) return false;
    const uint64_t total = static_cast<uint64_t>(pitch) * height;

    if (UINT64_MAX - addr < total) return false;

    if (out_total_size) {
        *out_total_size = total;
    }
    return true;
}

PixelFormat Framebuffer::detect_format(const boot::FramebufferInfo& info) noexcept {
    if (info.bpp == 32) {
        if (info.red.position == 16 && info.blue.position == 0) {
            return PixelFormat::BGRA8888;
        }
        if (info.red.position == 0 && info.blue.position == 16) {
            return PixelFormat::RGBA8888;
        }
        return PixelFormat::ARGB8888;
    }
    if (info.bpp == 24) {
        if (info.red.position == 16 && info.blue.position == 0) {
            return PixelFormat::BGR888;
        }
        return PixelFormat::RGB888;
    }
    return PixelFormat::Unknown;
}

Framebuffer::MappingPlan Framebuffer::calculate_mapping_plan(uint64_t paddr, uint64_t total_size) noexcept {
    MappingPlan plan{};
    if (paddr == 0 || total_size == 0) {
        return plan;
    }

    // Checked arithmetic: verify paddr + total_size does not wrap 64-bit space
    if (UINT64_MAX - paddr < total_size) {
        return plan;
    }

    // Direct physical map check (0 .. 2 GiB):
    if (paddr < memory::layout::DIRECT_MAP_PHYS_LIMIT &&
        (paddr + total_size) <= memory::layout::DIRECT_MAP_PHYS_LIMIT) {
        plan.uses_direct_map = true;
        plan.aligned_paddr = paddr & ~0xFFFULL;
        plan.page_offset = paddr & 0xFFFULL;
        plan.total_mapped_bytes = plan.page_offset + total_size;
        plan.num_pages = static_cast<size_t>((plan.total_mapped_bytes + 4095) / 4096);
        plan.base_vaddr = phys_to_virt(plan.aligned_paddr);
        plan.framebuffer_vaddr = phys_to_virt(paddr);
        plan.valid = true;
        return plan;
    }

    // MMIO Higher-Half Window mapping:
    plan.uses_direct_map = false;
    plan.page_offset = paddr & 0xFFFULL;
    plan.aligned_paddr = paddr & ~0xFFFULL;

    // Checked arithmetic: total mapped bytes = page_offset + total_size
    if (UINT64_MAX - plan.page_offset < total_size) {
        return plan;
    }
    plan.total_mapped_bytes = plan.page_offset + total_size;

    // Strict MMIO window capacity validation: entire mapping MUST fit without truncation
    if (plan.total_mapped_bytes > memory::layout::MMIO_WINDOW_SIZE) {
        return plan;
    }

    const uintptr_t base_vaddr = memory::layout::MMIO_WINDOW_START.value();
    // Checked arithmetic: verify base_vaddr + total_mapped_bytes does not overflow uintptr_t
    if (UINTPTR_MAX - base_vaddr < plan.total_mapped_bytes) {
        return plan;
    }

    // Checked arithmetic: verify page rounding does not overflow uint64
    if (UINT64_MAX - plan.total_mapped_bytes < 4095) {
        return plan;
    }
    plan.num_pages = static_cast<size_t>((plan.total_mapped_bytes + 4095) / 4096);

    // Double check that page count fits in MMIO window max pages
    if (plan.num_pages > memory::layout::MMIO_WINDOW_MAX_PAGES) {
        return plan;
    }

    plan.base_vaddr = base_vaddr;
    plan.framebuffer_vaddr = base_vaddr + plan.page_offset;
    plan.valid = true;
    return plan;
}

bool Framebuffer::init(const boot::FramebufferInfo& info) {
    s_available = false;
    s_paddr = 0;
    s_vaddr = 0;
    s_width = 0;
    s_height = 0;
    s_pitch = 0;
    s_bpp = 0;
    s_total_size = 0;
    s_format = PixelFormat::Unknown;

    if (!info.valid) {
        klog_info("Framebuffer: Metadata tag not present or invalid. Skipping graphical initialization.");
        return false;
    }

    if (!validate_metadata(info.address, info.pitch, info.width, info.height,
                           info.bpp, static_cast<uint8_t>(info.type), &s_total_size)) {
        klog_info("Framebuffer: Geometry validation rejected (Addr=%p, %ux%u @ %ubpp, Type=%u).",
                  info.address, info.width, info.height, info.bpp, static_cast<uint32_t>(info.type));
        return false;
    }

    MappingPlan plan = calculate_mapping_plan(info.address, s_total_size);
    if (!plan.valid) {
        klog_error("Framebuffer: Failed to calculate valid mapping plan (Size=%llu bytes exceeds MMIO capacity or overflows)!",
                   s_total_size);
        return false;
    }

    s_paddr = info.address;
    s_width = info.width;
    s_height = info.height;
    s_pitch = info.pitch;
    s_bpp = info.bpp;
    s_format = detect_format(info);

    if (plan.uses_direct_map) {
        s_vaddr = plan.framebuffer_vaddr;
    } else {
        // Map entire framebuffer into dedicated MMIO window without truncation
        for (size_t i = 0; i < plan.num_pages; ++i) {
            memory::VirtualAddress va(plan.base_vaddr + i * 4096);
            memory::PhysicalAddress pa(plan.aligned_paddr + i * 4096);
            memory::VmmStatus st = memory::g_vmm.map_page(va, pa, memory::PageFlags::Present | memory::PageFlags::Writable | memory::PageFlags::NoExecute);
            if (st != memory::VmmStatus::Success) {
                klog_error("Framebuffer: Failed to map page %zu/%zu (VA=%p, PA=%p, status=%d)!",
                           i + 1, plan.num_pages, va.value(), pa.value(), static_cast<int>(st));
                for (size_t j = 0; j < i; ++j) {
                    memory::g_vmm.unmap_page(memory::VirtualAddress(plan.base_vaddr + j * 4096));
                }
                return false;
            }
        }
        s_vaddr = plan.framebuffer_vaddr;
    }

    s_available = true;
    klog_info("Linear Framebuffer Initialized: %ux%u @ %ubpp, Pitch=%u, PA=%p, VA=%p, Size=%llu bytes, MappedPages=%zu",
              s_width, s_height, s_bpp, s_pitch, s_paddr, s_vaddr, s_total_size, plan.num_pages);
    return true;
}

bool Framebuffer::is_pixel_in_bounds(uint32_t x, uint32_t y, uint8_t bpp, uint32_t pitch,
                                     uint32_t width, uint32_t height, uint64_t total_size,
                                     uint64_t* out_offset) noexcept {
    if (bpp != 16 && bpp != 24 && bpp != 32) {
        return false;
    }
    if (x >= width || y >= height) {
        return false;
    }

    const uint64_t bpp_bytes = (bpp + 7) / 8;
    const uint64_t row_offset = static_cast<uint64_t>(y) * pitch;
    const uint64_t col_offset = static_cast<uint64_t>(x) * bpp_bytes;

    // Checked arithmetic: verify row_offset + col_offset does not wrap uint64
    if (UINT64_MAX - row_offset < col_offset) {
        return false;
    }
    const uint64_t pixel_offset = row_offset + col_offset;

    // Checked arithmetic: verify pixel_offset + bpp_bytes does not wrap uint64
    if (UINT64_MAX - pixel_offset < bpp_bytes) {
        return false;
    }

    // Strict boundary enforcement: pixel payload must be entirely within total_size
    if (pixel_offset + bpp_bytes > total_size) {
        return false;
    }

    if (out_offset) {
        *out_offset = pixel_offset;
    }
    return true;
}

void Framebuffer::put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!s_available) {
        return;
    }

    uint64_t pixel_offset = 0;
    if (!is_pixel_in_bounds(x, y, s_bpp, s_pitch, s_width, s_height, s_total_size, &pixel_offset)) {
        return;
    }

    uint8_t* dst = reinterpret_cast<uint8_t*>(s_vaddr) + pixel_offset;

    if (s_bpp == 32) {
        *reinterpret_cast<volatile uint32_t*>(dst) = color;
    } else if (s_bpp == 24) {
        dst[0] = static_cast<uint8_t>(color & 0xFF);         // Blue
        dst[1] = static_cast<uint8_t>((color >> 8) & 0xFF);  // Green
        dst[2] = static_cast<uint8_t>((color >> 16) & 0xFF); // Red
    } else if (s_bpp == 16) {
        uint16_t r = static_cast<uint16_t>((color >> 19) & 0x1F);
        uint16_t g = static_cast<uint16_t>((color >> 10) & 0x3F);
        uint16_t b = static_cast<uint16_t>((color >> 3) & 0x1F);
        *reinterpret_cast<volatile uint16_t*>(dst) = static_cast<uint16_t>((r << 11) | (g << 5) | b);
    }
}

void Framebuffer::fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!s_available || x >= s_width || y >= s_height || w == 0 || h == 0) {
        return;
    }

    if (w > s_width - x)  w = s_width - x;
    if (h > s_height - y) h = s_height - y;

    for (uint32_t cy = y; cy < y + h; ++cy) {
        for (uint32_t cx = x; cx < x + w; ++cx) {
            put_pixel(cx, cy, color);
        }
    }
}

void Framebuffer::clear(uint32_t color) {
    if (!s_available) return;
    fill_rect(0, 0, s_width, s_height, color);
}

} // namespace llamaos::drivers
