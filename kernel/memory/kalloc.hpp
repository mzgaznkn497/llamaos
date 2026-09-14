#pragma once

#include "core/types.hpp"
#include "memory/memory_types.hpp"

// =============================================================================
// LlamaOS/A - Kernel Dynamic Allocation Foundation Interfaces
// =============================================================================
// Provides early page-granularity kernel virtual allocations and interface stubs
// for future byte-granularity heap management (kmalloc / kfree) scheduled for Phase 3.
// Enforces complete architectural separation between physical page frame management
// and kernel virtual heap allocations.
// =============================================================================

namespace llamaos::memory {

// Page-granularity virtual memory allocation from kernel space
[[nodiscard]] void* alloc_kernel_pages(PageCount count, PageFlags flags = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute);
bool free_kernel_pages(void* virt_addr, PageCount count);

// General-purpose dynamic memory interface (Phase 3 Heap Subsystem Preparation)
[[nodiscard]] void* kmalloc(size_t size);
void kfree(void* ptr);

} // namespace llamaos::memory
