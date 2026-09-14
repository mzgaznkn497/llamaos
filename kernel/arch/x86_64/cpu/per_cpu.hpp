#pragma once

#include "core/types.hpp"
#include "sync/spinlock.hpp"

// =============================================================================
// LlamaOS/A - Per-CPU State & Multi-Core Architecture (Blocker 10)
// =============================================================================
// Architectural Foundation for CPU-Local Data and Multiprocessing:
// - Encapsulates per-CPU execution state, LAPIC identification, and scheduler context
// - Classifies current production capability explicitly: SINGLE-CPU ONLY
// - Provides the architectural interface for future SMP expansion (INIT/SIPI/IPI)
// =============================================================================

namespace llamaos::arch::x86_64 {

inline constexpr size_t MAX_CPUS = 32;

struct CpuLocalData {
    uint32_t cpu_id{0};
    uint32_t apic_id{0};
    bool     is_bsp{false};
    bool     online{false};
    uint64_t current_thread_id{0};
    uint64_t scheduler_ticks{0};
    uint64_t irq_count{0};
    uintptr_t kernel_stack_top{0};
    uint64_t active_cr3{0};
};

class PerCpuManager {
public:
    static void init_bsp();

    // Query active CPU
    [[nodiscard]] static uint32_t current_cpu_id() noexcept;
    [[nodiscard]] static CpuLocalData& current() noexcept;
    [[nodiscard]] static CpuLocalData& get_cpu(size_t index) noexcept;

    [[nodiscard]] static size_t online_cpu_count() noexcept { return s_online_cpus; }
    [[nodiscard]] static bool is_smp_active() noexcept { return false; } // SINGLE-CPU ONLY

    static void record_tick(uint32_t cpu_id = 0) noexcept;
    static void dump_topology();

private:
    static CpuLocalData s_cpus[MAX_CPUS];
    static size_t       s_online_cpus;
    static sync::Spinlock s_lock;
};

} // namespace llamaos::arch::x86_64
