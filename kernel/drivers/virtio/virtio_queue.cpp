#include "virtio_queue.hpp"
#include "arch/x86_64/cpu/io.hpp"
#include "arch/x86_64/cpu/cpu.hpp"
#include "memory/kalloc.hpp"
#include "memory/pmm.hpp"
#include "core/string.hpp"
#include "core/kprint.hpp"

namespace llamaos::drivers::virtio {

Virtqueue::~Virtqueue() {
    cleanup();
}

void Virtqueue::cleanup() {
    if (m_initialized && m_virt_addr) {
        memory::free_kernel_pages(m_virt_addr, memory::PageCount(m_total_pages));
        m_virt_addr = nullptr;
        m_phys_addr = memory::PhysicalAddress(0);
        m_desc = nullptr;
        m_avail = nullptr;
        m_used = nullptr;
        m_initialized = false;
    }
}

bool Virtqueue::init(uint16_t queue_index, uint16_t queue_size, uint16_t io_base) {
    if (queue_size == 0 || (queue_size & (queue_size - 1)) != 0) {
        klog_error("Virtqueue: invalid queue size %u (must be power of 2)!", queue_size);
        return false;
    }

    cleanup();

    m_queue_index = queue_index;
    m_queue_size = queue_size;
    m_io_base = io_base;

    // Calculate layout according to VirtIO legacy specification:
    // Descriptors + Available ring, padded to 4096 bytes, followed by Used ring
    size_t desc_size = sizeof(VirtqDesc) * queue_size;
    size_t avail_size = sizeof(uint16_t) * (3 + queue_size);
    size_t part1_size = align_up<size_t>(desc_size + avail_size, PAGE_SIZE);
    size_t used_size = sizeof(uint16_t) * 3 + sizeof(VirtqUsedElem) * queue_size;
    size_t total_bytes = part1_size + align_up<size_t>(used_size, PAGE_SIZE);

    m_total_pages = total_bytes / PAGE_SIZE;

    void* mem = memory::alloc_kernel_pages(memory::PageCount(m_total_pages));
    if (!mem) {
        klog_error("Virtqueue: failed to allocate %u physical pages for queue %u!",
                   static_cast<uint32_t>(m_total_pages), queue_index);
        return false;
    }

    memset(mem, 0, total_bytes);
    m_virt_addr = mem;
    m_phys_addr = memory::virt_to_phys(memory::VirtualAddress(mem));

    uint8_t* base = static_cast<uint8_t*>(mem);
    m_desc = reinterpret_cast<VirtqDesc*>(base);
    m_avail = reinterpret_cast<VirtqAvail*>(base + desc_size);
    m_used = reinterpret_cast<VirtqUsed*>(base + part1_size);

    // Initialize free descriptor freelist
    for (uint16_t i = 0; i + 1 < queue_size; ++i) {
        m_desc[i].next = i + 1;
    }
    m_desc[queue_size - 1].next = 0xFFFF;
    m_free_head = 0;
    m_num_free = queue_size;
    m_last_used_idx = 0;

    // Tell hardware where queue is:
    // 1. Select queue
    arch::x86_64::outw(static_cast<uint16_t>(m_io_base + VIRTIO_PCI_QUEUE_SEL), m_queue_index);
    // 2. Write Page Frame Number (PFN)
    uint32_t pfn = static_cast<uint32_t>(m_phys_addr.value() / PAGE_SIZE);
    arch::x86_64::outl(static_cast<uint16_t>(m_io_base + VIRTIO_PCI_QUEUE_PFN), pfn);

    m_initialized = true;
    return true;
}

int16_t Virtqueue::alloc_descriptor() {
    if (m_num_free == 0 || m_free_head == 0xFFFF) {
        return -1;
    }
    uint16_t idx = m_free_head;
    m_free_head = m_desc[idx].next;
    m_num_free--;

    m_desc[idx].addr = 0;
    m_desc[idx].len = 0;
    m_desc[idx].flags = 0;
    m_desc[idx].next = 0;

    return static_cast<int16_t>(idx);
}

void Virtqueue::free_descriptor_chain(uint16_t head) {
    if (head >= m_queue_size) return;

    uint16_t cur = head;
    while (true) {
        uint16_t next = m_desc[cur].next;
        bool has_next = (m_desc[cur].flags & VIRTQ_DESC_F_NEXT) != 0;

        m_desc[cur].flags = 0;
        m_desc[cur].addr = 0;
        m_desc[cur].len = 0;
        m_desc[cur].next = m_free_head;
        m_free_head = cur;
        m_num_free++;

        if (!has_next || next >= m_queue_size) break;
        cur = next;
    }
}

void Virtqueue::set_descriptor(uint16_t index, uint64_t paddr, uint32_t len, uint16_t flags, uint16_t next) {
    if (index >= m_queue_size) return;
    m_desc[index].addr = paddr;
    m_desc[index].len = len;
    m_desc[index].flags = flags;
    m_desc[index].next = next;
}

void Virtqueue::submit_descriptor_chain(uint16_t head_index) {
    if (!m_initialized || head_index >= m_queue_size) return;

    uint16_t avail_idx = m_avail->idx;
    m_avail->ring[avail_idx % m_queue_size] = head_index;

    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    m_avail->idx = avail_idx + 1;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    // Notify hypervisor of queue activity
    arch::x86_64::outw(static_cast<uint16_t>(m_io_base + VIRTIO_PCI_QUEUE_NOTIFY), m_queue_index);
}

bool Virtqueue::wait_for_completion(uint16_t head_index, uint32_t max_polls) {
    if (!m_initialized) return false;

    uint32_t polls = 0;
    while (polls < max_polls) {
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (m_last_used_idx != m_used->idx) {
            // New entry completed in used ring
            uint16_t cur_idx = m_last_used_idx % m_queue_size;
            uint32_t completed_id = m_used->ring[cur_idx].id;
            m_last_used_idx++;

            if (completed_id == head_index) {
                return true;
            }
        }

        arch::x86_64::pause();
        polls++;
    }

    klog_error("Virtqueue: Timeout waiting for request %u completion!", head_index);
    return false;
}

bool Virtqueue::has_used_buffer() const noexcept {
    if (!m_initialized) return false;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return m_last_used_idx != m_used->idx;
}

bool Virtqueue::pop_used_buffer(uint16_t* out_head, uint32_t* out_len) noexcept {
    if (!has_used_buffer()) return false;

    uint16_t cur_idx = m_last_used_idx % m_queue_size;
    if (out_head) *out_head = static_cast<uint16_t>(m_used->ring[cur_idx].id);
    if (out_len) *out_len = m_used->ring[cur_idx].len;
    m_last_used_idx++;
    return true;
}

} // namespace llamaos::drivers::virtio
