#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - VirtIO 0.9.5 / 1.0 Hardware Specification Definitions
// =============================================================================
// Defines registers, device IDs, status flags, virtqueue structures, and block
// request formats compliant with the OASIS VirtIO specification.
// =============================================================================

namespace llamaos::drivers::virtio {

// Standard PCI Identifiers
inline constexpr uint16_t VIRTIO_PCI_VENDOR_ID     = 0x1AF4;
inline constexpr uint16_t VIRTIO_PCI_DEV_NET_LEGACY= 0x1000;
inline constexpr uint16_t VIRTIO_PCI_DEV_BLK_LEGACY= 0x1001;
inline constexpr uint16_t VIRTIO_PCI_DEV_NET_MODERN= 0x1041;
inline constexpr uint16_t VIRTIO_PCI_DEV_BLK_MODERN= 0x1042;

// Standard VirtIO Device Status Bits
inline constexpr uint8_t VIRTIO_STATUS_RESET       = 0x00;
inline constexpr uint8_t VIRTIO_STATUS_ACKNOWLEDGE = 0x01;
inline constexpr uint8_t VIRTIO_STATUS_DRIVER      = 0x02;
inline constexpr uint8_t VIRTIO_STATUS_DRIVER_OK   = 0x04;
inline constexpr uint8_t VIRTIO_STATUS_FEATURES_OK = 0x08;
inline constexpr uint8_t VIRTIO_STATUS_FAILED      = 0x80;

// Legacy PCI I/O Register Offsets (relative to I/O BAR base)
inline constexpr uint16_t VIRTIO_PCI_HOST_FEATURES = 0x00; // 32-bit R
inline constexpr uint16_t VIRTIO_PCI_GUEST_FEATURES= 0x04; // 32-bit R/W
inline constexpr uint16_t VIRTIO_PCI_QUEUE_PFN     = 0x08; // 32-bit R/W (4 KiB page frame)
inline constexpr uint16_t VIRTIO_PCI_QUEUE_SIZE    = 0x0C; // 16-bit R
inline constexpr uint16_t VIRTIO_PCI_QUEUE_SEL     = 0x0E; // 16-bit R/W
inline constexpr uint16_t VIRTIO_PCI_QUEUE_NOTIFY  = 0x10; // 16-bit R/W
inline constexpr uint16_t VIRTIO_PCI_STATUS        = 0x12; // 8-bit R/W
inline constexpr uint16_t VIRTIO_PCI_ISR           = 0x13; // 8-bit R
inline constexpr uint16_t VIRTIO_PCI_CONFIG_BASE   = 0x14; // Device-specific config

// Virtqueue Descriptor Flags
inline constexpr uint16_t VIRTQ_DESC_F_NEXT     = 1;
inline constexpr uint16_t VIRTQ_DESC_F_WRITE    = 2;
inline constexpr uint16_t VIRTQ_DESC_F_INDIRECT = 4;

// Virtqueue Structures
struct alignas(16) VirtqDesc {
    uint64_t addr;   // Guest Physical Address
    uint32_t len;    // Length in bytes
    uint16_t flags;  // Descriptor flags (NEXT, WRITE)
    uint16_t next;   // Next descriptor index
};

struct VirtqAvail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[]; // Array of size [queue_size]
};

struct VirtqUsedElem {
    uint32_t id;     // Index of start of descriptor chain
    uint32_t len;    // Total bytes transferred
};

struct VirtqUsed {
    uint16_t flags;
    uint16_t idx;
    VirtqUsedElem ring[]; // Array of size [queue_size]
};

// VirtIO Block Feature Flags
inline constexpr uint32_t VIRTIO_BLK_F_RO          = (1U << 5);
inline constexpr uint32_t VIRTIO_BLK_F_BLK_SIZE    = (1U << 6);
inline constexpr uint32_t VIRTIO_BLK_F_FLUSH       = (1U << 9);
inline constexpr uint32_t VIRTIO_BLK_F_TOPOLOGY    = (1U << 10);

// VirtIO Block Request Types
inline constexpr uint32_t VIRTIO_BLK_T_IN          = 0; // Read
inline constexpr uint32_t VIRTIO_BLK_T_OUT         = 1; // Write
inline constexpr uint32_t VIRTIO_BLK_T_FLUSH       = 4; // Flush cache
inline constexpr uint32_t VIRTIO_BLK_T_GET_ID      = 8; // Get device ID string

// VirtIO Block Request Status
inline constexpr uint8_t VIRTIO_BLK_S_OK           = 0;
inline constexpr uint8_t VIRTIO_BLK_S_IOERR        = 1;
inline constexpr uint8_t VIRTIO_BLK_S_UNSUPP       = 2;

// Header for block requests (16 bytes)
struct alignas(16) VirtioBlockReqHeader {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
};

// VirtIO Network Feature Flags
inline constexpr uint32_t VIRTIO_NET_F_CSUM        = (1U << 0);
inline constexpr uint32_t VIRTIO_NET_F_MAC         = (1U << 5);
inline constexpr uint32_t VIRTIO_NET_F_STATUS      = (1U << 16);

// VirtIO Net Header (10 bytes for legacy VirtIO)
struct alignas(2) VirtioNetHdr {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
};

} // namespace llamaos::drivers::virtio
