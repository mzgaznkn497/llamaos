#pragma once

#include "core/types.hpp"
#include "memory/memory_types.hpp"

// =============================================================================
// LlamaOS/A - Virtual Address Space Architectural Layout
// =============================================================================
// Formally defines the canonical 48-bit virtual address space map for LlamaOS/A.
//
// 0x0000000000000000 - 0x00007FFFFFFFFFFF : User Address Space (128 TiB)
// 0x0000800000000000 - 0xFFFF7FFFFFFFFFFF : Non-Canonical Addressing Hole (Invalid)
// 0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF : Higher-Half Kernel Space (128 TiB)
//
// Within Top-Level Higher-Half (-2 GiB Window):
// 0xFFFFFFFF80000000 - 0xFFFFFFFF80200000 : Kernel Image & Low Memory Direct Map (2 MiB)
//   0xFFFFFFFF80000000 - 0xFFFFFFFF800FFFFF : Real Mode IVT/BDA/EBDA/VRAM Direct Map (1 MiB)
//   0xFFFFFFFF80100000 - 0xFFFFFFFF80108000 : Early Boot Code/Data (.boot sections)
//   0xFFFFFFFF80108000 - 0xFFFFFFFF8010E000 : Kernel Code (.text, RX)
//   0xFFFFFFFF8010E000 - 0xFFFFFFFF80111000 : Kernel Read-Only Data (.rodata, R)
//   0xFFFFFFFF80111000 - 0xFFFFFFFF80112000 : Kernel Initialized Data (.data, RW NX)
//   0xFFFFFFFF80112000 - 0xFFFFFFFF80123000 : Kernel Zero-Init BSS & Stack (.bss, RW NX)
//   0xFFFFFFFF80123000 - 0xFFFFFFFF80200000 : PMM Allocation Bitmap & Initial Page Pool
// 0xFFFFFFFF80200000 - 0xFFFFFFFF80400000 : Guard Page Region (2 MiB Non-Present)
// 0xFFFFFFFF80400000 - 0xFFFFFFFF90000000 : Kernel Dynamic Heap Region (252 MiB, Phase 3)
// 0xFFFFFFFF90000000 - 0xFFFFFFFFA0000000 : VMM Page Table Dynamic Pool (256 MiB)
// 0xFFFFFFFFA0000000 - 0xFFFFFFFFC0000000 : Device MMIO / Framebuffer Window (512 MiB)
// 0xFFFFFFFFC0000000 - 0xFFFFFFFFFFFFFFFF : Extended Physical Direct Map Window (1 GiB)
// =============================================================================

namespace llamaos::memory::layout {

// Canonical boundary limits (48-bit paging)
inline constexpr VirtualAddress USER_SPACE_START       {0x0000000000000000ULL};
inline constexpr VirtualAddress USER_SPACE_END         {0x0000800000000000ULL}; // Exclusive
inline constexpr uint64_t       USER_SPACE_SIZE_BYTES  {0x0000800000000000ULL}; // 128 TiB

inline constexpr uint64_t       NON_CANONICAL_START    {0x0000800000000000ULL};
inline constexpr uint64_t       NON_CANONICAL_END      {0xFFFF800000000000ULL};

inline constexpr VirtualAddress HIGHER_HALF_BASE       {0xFFFF800000000000ULL};
inline constexpr VirtualAddress KERNEL_SPACE_START     {0xFFFF800000000000ULL};
inline constexpr VirtualAddress KERNEL_SPACE_END       {0x0000000000000000ULL}; // Top of 64-bit space (wraps)

// Higher-Half Kernel -2 GiB Window Definitions
inline constexpr VirtualAddress KERNEL_BASE_VIRTUAL    {0xFFFFFFFF80000000ULL};
inline constexpr PhysicalAddress KERNEL_LOAD_PHYSICAL  {0x0000000000100000ULL}; // 1 MiB physical

// Direct-mapped physical memory boundaries for initial boot paging (0 .. 512 MiB)
inline constexpr uint64_t       DIRECT_MAP_PHYS_LIMIT  {0x20000000ULL};         // 512 MiB

// Specialized Subsystem Windows
inline constexpr VirtualAddress KERNEL_GUARD_REGION    {0xFFFFFFFF80200000ULL};
inline constexpr uint64_t       KERNEL_GUARD_SIZE      {0x0000000000200000ULL}; // 2 MiB

inline constexpr VirtualAddress KERNEL_HEAP_START      {0xFFFFFFFF80400000ULL};
inline constexpr VirtualAddress KERNEL_HEAP_END        {0xFFFFFFFF90000000ULL};
inline constexpr uint64_t       KERNEL_HEAP_MAX_SIZE   {0x000000000FC00000ULL}; // 252 MiB

inline constexpr VirtualAddress VMM_TABLE_POOL_START   {0xFFFFFFFF90000000ULL};
inline constexpr VirtualAddress VMM_TABLE_POOL_END     {0xFFFFFFFFA0000000ULL};
inline constexpr uint64_t       VMM_TABLE_POOL_SIZE    {0x0000000010000000ULL}; // 256 MiB

inline constexpr VirtualAddress MMIO_WINDOW_START      {0xFFFFFFFFA0000000ULL};
inline constexpr VirtualAddress MMIO_WINDOW_END        {0xFFFFFFFFC0000000ULL};
inline constexpr uint64_t       MMIO_WINDOW_SIZE       {0x0000000020000000ULL}; // 512 MiB
inline constexpr size_t         MMIO_WINDOW_MAX_PAGES  {static_cast<size_t>(MMIO_WINDOW_SIZE / 4096)}; // 131,072 pages (4 KiB each)

} // namespace llamaos::memory::layout
