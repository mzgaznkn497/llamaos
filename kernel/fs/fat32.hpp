#pragma once

#include "core/types.hpp"
#include "storage/block_device.hpp"
#include "fs/vfs.hpp"
#include "sync/spinlock.hpp"

// =============================================================================
// LlamaOS/A - FAT32 Persistent Filesystem Driver
// =============================================================================
// Compliant with Microsoft FAT32 specification.
// Supports cluster chain traversal, FAT allocation/freeing, 8.3 directory
// enumeration, file lookup, creation, reading, writing, seeking, truncate,
// and file deletion.
// =============================================================================

namespace llamaos::fs {

struct [[gnu::packed]] Fat32Bpb {
    uint8_t  jmp_boot[3];
    char     oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t table_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;

    // Extended FAT32 fields
    uint32_t table_size_32;
    uint16_t extended_flags;
    uint16_t fat_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
};

static_assert(sizeof(Fat32Bpb) == 90, "Fat32Bpb must be 90 bytes");

struct [[gnu::packed]] Fat32DirEntry {
    char     name[11];      // 8.3 filename (8 name + 3 ext, space padded)
    uint8_t  attr;          // 0x10 = Dir, 0x20 = Archive, etc.
    uint8_t  nt_reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
};

static_assert(sizeof(Fat32DirEntry) == 32, "Fat32DirEntry must be 32 bytes");

struct [[gnu::packed]] Fat32LfnEntry {
    uint8_t  order;          // Sequence number (with 0x40 bit set for last logical entry)
    uint16_t name1[5];       // Characters 0..4 (UTF-16LE)
    uint8_t  attr;           // Always 0x0F (FAT_ATTR_LFN)
    uint8_t  type;           // Always 0x00
    uint8_t  checksum;       // 8.3 checksum
    uint16_t name2[6];       // Characters 5..10 (UTF-16LE)
    uint16_t first_cluster;  // Always 0
    uint16_t name3[2];       // Characters 11..12 (UTF-16LE)
};

static_assert(sizeof(Fat32LfnEntry) == 32, "Fat32LfnEntry must be 32 bytes");

inline constexpr uint8_t FAT_ATTR_READ_ONLY = 0x01;
inline constexpr uint8_t FAT_ATTR_HIDDEN    = 0x02;
inline constexpr uint8_t FAT_ATTR_SYSTEM    = 0x04;
inline constexpr uint8_t FAT_ATTR_VOLUME_ID = 0x08;
inline constexpr uint8_t FAT_ATTR_DIRECTORY = 0x10;
inline constexpr uint8_t FAT_ATTR_ARCHIVE   = 0x20;
inline constexpr uint8_t FAT_ATTR_LFN       = 0x0F;

inline constexpr uint32_t FAT32_EOC_MIN     = 0x0FFFFFF8U;
inline constexpr uint32_t FAT32_BAD_CLUSTER = 0x0FFFFFF7U;

class Fat32Filesystem;

class Fat32VNode : public VNode {
public:
    Fat32VNode() noexcept = default;
    Fat32VNode(Fat32Filesystem* fs,
               NodeType type,
               uint32_t first_cluster,
               uint32_t file_size,
               uint32_t dir_cluster = 0,
               uint32_t dir_offset = 0) noexcept;

    [[nodiscard]] NodeType type() const noexcept override { return m_type; }
    [[nodiscard]] uint64_t size() const noexcept override { return m_file_size; }
    [[nodiscard]] uint32_t first_cluster() const noexcept { return m_first_cluster; }

    int64_t read(uint64_t offset, size_t count, void* dst) override;
    int64_t write(uint64_t offset, size_t count, const void* src) override;
    int truncate(uint64_t new_size) override;

    int lookup(const char* name, VNode** out_child) override;
    int create(const char* name, NodeType type, VNode** out_created) override;
    int mkdir(const char* name, VNode** out_created) override;
    int unlink(const char* name) override;
    int readdir(uint32_t index, DirEntry* entry) override;

    int stat(FileStat* st) override;

    void update_dir_entry(uint32_t new_size, uint32_t new_first_cluster);

private:
    Fat32Filesystem* m_fs{nullptr};
    NodeType         m_type{NodeType::Unknown};
    uint32_t         m_first_cluster{0};
    uint32_t         m_file_size{0};
    uint32_t         m_dir_cluster{0}; // Cluster where this node's 32-byte dir entry lives
    uint32_t         m_dir_offset{0};  // Byte offset within dir cluster
};

class Fat32Filesystem : public Filesystem {
public:
    static constexpr size_t MAX_CACHED_VNODES = 64;

    Fat32Filesystem() = default;
    ~Fat32Filesystem() override;

    [[nodiscard]] const char* name() const noexcept override { return "fat32"; }

    int mount(storage::BlockDevice* dev, VNode** out_root) override;
    int unmount() override;

    // Cluster arithmetic
    [[nodiscard]] uint64_t cluster_to_lba(uint32_t cluster) const noexcept;
    [[nodiscard]] uint32_t bytes_per_cluster() const noexcept {
        return m_bpb.sectors_per_cluster * m_bpb.bytes_per_sector;
    }
    [[nodiscard]] uint32_t total_clusters() const noexcept { return m_total_clusters; }

    // FAT operations
    [[nodiscard]] uint32_t read_fat_entry(uint32_t cluster);
    int write_fat_entry(uint32_t cluster, uint32_t value);
    [[nodiscard]] uint32_t alloc_cluster(uint32_t prev_cluster = 0);
    void free_cluster_chain(uint32_t start_cluster);

    // Cluster read/write
    int read_cluster(uint32_t cluster, void* dst);
    int write_cluster(uint32_t cluster, const void* src);

    // VNode pool management
    Fat32VNode* alloc_vnode(NodeType type, uint32_t first_cluster, uint32_t file_size, uint32_t dir_cluster = 0, uint32_t dir_offset = 0);

    // 8.3 conversion helpers
    static void format_to_83(const char* in_name, char out_83[11]);
    static void format_from_83(const char in_83[11], char* out_name, size_t max_len);

    [[nodiscard]] storage::BlockDevice* device() const noexcept { return m_dev; }

private:
    storage::BlockDevice* m_dev{nullptr};
    Fat32Bpb              m_bpb{};
    uint64_t              m_fat_begin_lba{0};
    uint64_t              m_cluster_begin_lba{0};
    uint32_t              m_total_clusters{0};
    sync::Spinlock        m_fs_lock;

    Fat32VNode            m_vnode_pool[MAX_CACHED_VNODES];
    bool                  m_vnode_in_use[MAX_CACHED_VNODES]{false};
    size_t                m_vnode_count{0};
};

} // namespace llamaos::fs
