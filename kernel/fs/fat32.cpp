#include "fs/fat32.hpp"
#include "memory/kalloc.hpp"
#include "core/kprint.hpp"
#include "core/string.hpp"

namespace llamaos::fs {

namespace {

char to_upper(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

char to_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool strcasecmp_match(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (to_lower(*a) != to_lower(*b)) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

void extract_lfn_chars(const Fat32LfnEntry* lfn, char* lfn_buf, size_t buf_size) {
    uint8_t seq = lfn->order & 0x1F;
    if (seq >= 1 && seq <= 20) {
        size_t base_idx = (seq - 1) * 13;
        for (size_t c = 0; c < 5 && base_idx + c < buf_size - 1; ++c) {
            uint16_t ch = lfn->name1[c];
            if (ch == 0 || ch == 0xFFFF) { lfn_buf[base_idx + c] = '\0'; return; }
            lfn_buf[base_idx + c] = static_cast<char>(ch & 0x7F);
        }
        for (size_t c = 0; c < 6 && base_idx + 5 + c < buf_size - 1; ++c) {
            uint16_t ch = lfn->name2[c];
            if (ch == 0 || ch == 0xFFFF) { lfn_buf[base_idx + 5 + c] = '\0'; return; }
            lfn_buf[base_idx + 5 + c] = static_cast<char>(ch & 0x7F);
        }
        for (size_t c = 0; c < 2 && base_idx + 11 + c < buf_size - 1; ++c) {
            uint16_t ch = lfn->name3[c];
            if (ch == 0 || ch == 0xFFFF) { lfn_buf[base_idx + 11 + c] = '\0'; return; }
            lfn_buf[base_idx + 11 + c] = static_cast<char>(ch & 0x7F);
        }
        if (base_idx + 13 < buf_size) {
            lfn_buf[base_idx + 13] = '\0';
        }
    }
}

} // anonymous namespace

// -----------------------------------------------------------------------------
// 8.3 Filename Formatting Helpers
// -----------------------------------------------------------------------------

void Fat32Filesystem::format_to_83(const char* in_name, char out_83[11]) {
    for (size_t i = 0; i < 11; ++i) out_83[i] = ' ';
    if (!in_name || in_name[0] == '\0') return;

    // Special cases: "." and ".."
    if (in_name[0] == '.' && in_name[1] == '\0') {
        out_83[0] = '.';
        return;
    }
    if (in_name[0] == '.' && in_name[1] == '.' && in_name[2] == '\0') {
        out_83[0] = '.';
        out_83[1] = '.';
        return;
    }

    size_t len = llamaos::strlen(in_name);
    size_t dot = len;
    for (size_t i = 0; i < len; ++i) {
        if (in_name[i] == '.') {
            dot = i;
            break;
        }
    }

    // Name part (up to 8 chars)
    size_t n_len = (dot < 8) ? dot : 8;
    for (size_t i = 0; i < n_len; ++i) {
        out_83[i] = to_upper(in_name[i]);
    }

    // Ext part (up to 3 chars after dot)
    if (dot < len) {
        size_t ext_len = len - (dot + 1);
        if (ext_len > 3) ext_len = 3;
        for (size_t i = 0; i < ext_len; ++i) {
            out_83[8 + i] = to_upper(in_name[dot + 1 + i]);
        }
    }
}

void Fat32Filesystem::format_from_83(const char in_83[11], char* out_name, size_t max_len) {
    if (!in_83 || !out_name || max_len < 13) return;

    size_t out_idx = 0;

    // Trim spaces from name
    size_t n_end = 8;
    while (n_end > 0 && in_83[n_end - 1] == ' ') n_end--;

    for (size_t i = 0; i < n_end && out_idx + 1 < max_len; ++i) {
        out_name[out_idx++] = to_lower(in_83[i]);
    }

    // Check extension
    size_t e_end = 3;
    while (e_end > 0 && in_83[8 + e_end - 1] == ' ') e_end--;

    if (e_end > 0 && out_idx + 1 < max_len) {
        out_name[out_idx++] = '.';
        for (size_t i = 0; i < e_end && out_idx + 1 < max_len; ++i) {
            out_name[out_idx++] = to_lower(in_83[8 + i]);
        }
    }

    out_name[out_idx] = '\0';
}

// -----------------------------------------------------------------------------
// Fat32Filesystem Implementation
// -----------------------------------------------------------------------------

Fat32Filesystem::~Fat32Filesystem() {
    unmount();
}

int Fat32Filesystem::mount(storage::BlockDevice* dev, VNode** out_root) {
    if (!dev || !out_root) return -1;

    m_dev = dev;

    // Read BPB from sector 0
    alignas(512) uint8_t sector[512];
    storage::BlockStatus st = m_dev->read_sectors(0, 1, sector);
    if (st != storage::BlockStatus::Success) {
        klog_error("FAT32: Failed to read BPB from sector 0!");
        return -2;
    }

    llamaos::memcpy(&m_bpb, sector, sizeof(Fat32Bpb));

    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        klog_error("FAT32: Missing 0x55AA boot sector signature!");
        return -3;
    }

    if (m_bpb.bytes_per_sector != 512) {
        klog_error("FAT32: Unsupported sector size %u (must be 512)!", m_bpb.bytes_per_sector);
        return -4;
    }

    if (m_bpb.sectors_per_cluster == 0 || m_bpb.num_fats == 0 || m_bpb.table_size_32 == 0 || m_bpb.root_cluster < 2) {
        klog_error("FAT32: Corrupt BPB parameters!");
        return -5;
    }

    m_fat_begin_lba = m_bpb.reserved_sector_count;
    m_cluster_begin_lba = m_fat_begin_lba + (static_cast<uint64_t>(m_bpb.num_fats) * m_bpb.table_size_32);

    uint64_t data_sectors = (m_bpb.total_sectors_32 > 0 ? m_bpb.total_sectors_32 : m_dev->total_sectors()) - m_cluster_begin_lba;
    m_total_clusters = static_cast<uint32_t>(data_sectors / m_bpb.sectors_per_cluster);

    for (size_t i = 0; i < MAX_CACHED_VNODES; ++i) {
        m_vnode_in_use[i] = false;
    }
    m_vnode_count = 0;

    // Allocate Root VNode
    Fat32VNode* root = alloc_vnode(NodeType::Directory, m_bpb.root_cluster, 0);
    if (!root) return -5;

    *out_root = root;

    char label_buf[12];
    llamaos::memcpy(label_buf, m_bpb.volume_label, 11);
    label_buf[11] = '\0';
    // Trim trailing spaces
    for (int i = 10; i >= 0 && label_buf[i] == ' '; --i) {
        label_buf[i] = '\0';
    }

    klog_info("FAT32: Mounted volume '%s' (%u B/cluster, %u clusters, root=%u)",
              label_buf, bytes_per_cluster(), m_total_clusters, m_bpb.root_cluster);

    return 0;
}

int Fat32Filesystem::unmount() {
    if (m_dev) {
        m_dev->flush();
        m_dev = nullptr;
    }
    return 0;
}

uint64_t Fat32Filesystem::cluster_to_lba(uint32_t cluster) const noexcept {
    if (cluster < 2) return m_cluster_begin_lba;
    return m_cluster_begin_lba + (static_cast<uint64_t>(cluster - 2) * m_bpb.sectors_per_cluster);
}

uint32_t Fat32Filesystem::read_fat_entry(uint32_t cluster) {
    if (!m_dev) return FAT32_BAD_CLUSTER;

    uint64_t fat_offset = static_cast<uint64_t>(cluster) * 4;
    uint64_t fat_sector = m_fat_begin_lba + (fat_offset / 512);
    uint32_t ent_offset = static_cast<uint32_t>(fat_offset % 512);

    alignas(512) uint8_t buf[512];
    storage::BlockStatus st = m_dev->read_sectors(fat_sector, 1, buf);
    if (st != storage::BlockStatus::Success) return FAT32_BAD_CLUSTER;

    uint32_t entry = *reinterpret_cast<const uint32_t*>(buf + ent_offset);
    return entry & 0x0FFFFFFFU;
}

int Fat32Filesystem::write_fat_entry(uint32_t cluster, uint32_t value) {
    if (!m_dev) return -1;

    uint64_t fat_offset = static_cast<uint64_t>(cluster) * 4;
    uint64_t sector_offset = fat_offset / 512;
    uint32_t ent_offset = static_cast<uint32_t>(fat_offset % 512);

    alignas(512) uint8_t buf[512];

    // Update all FAT copies
    for (uint8_t f = 0; f < m_bpb.num_fats; ++f) {
        uint64_t fat_sector = m_fat_begin_lba + (static_cast<uint64_t>(f) * m_bpb.table_size_32) + sector_offset;
        storage::BlockStatus st = m_dev->read_sectors(fat_sector, 1, buf);
        if (st != storage::BlockStatus::Success) return -2;

        uint32_t orig = *reinterpret_cast<uint32_t*>(buf + ent_offset);
        orig = (orig & 0xF0000000U) | (value & 0x0FFFFFFFU);
        *reinterpret_cast<uint32_t*>(buf + ent_offset) = orig;

        st = m_dev->write_sectors(fat_sector, 1, buf);
        if (st != storage::BlockStatus::Success) return -3;
    }

    return 0;
}

uint32_t Fat32Filesystem::alloc_cluster(uint32_t prev_cluster) {
    sync::SpinlockGuard guard(m_fs_lock);

    for (uint32_t c = 2; c < m_total_clusters + 2; ++c) {
        if (read_fat_entry(c) == 0) {
            // Found free cluster!
            write_fat_entry(c, 0x0FFFFFFFU); // Mark EOC

            if (prev_cluster >= 2) {
                write_fat_entry(prev_cluster, c); // Link chain
            }

            // Zero cluster data on disk
            size_t bpc = bytes_per_cluster();
            void* zero_buf = memory::kmalloc(bpc);
            if (zero_buf) {
                llamaos::memset(zero_buf, 0, bpc);
                write_cluster(c, zero_buf);
                memory::kfree(zero_buf);
            }

            return c;
        }
    }

    klog_error("FAT32: Volume full! Could not allocate cluster.");
    return 0;
}

void Fat32Filesystem::free_cluster_chain(uint32_t start_cluster) {
    if (start_cluster < 2) return;

    sync::SpinlockGuard guard(m_fs_lock);
    uint32_t curr = start_cluster;
    size_t hops = 0;
    while (curr >= 2 && curr < FAT32_EOC_MIN) {
        if (++hops > m_total_clusters) {
            klog_warn("FAT32: Cluster chain cycle detected starting at cluster %u!", start_cluster);
            break;
        }
        uint32_t next = read_fat_entry(curr);
        write_fat_entry(curr, 0);
        curr = next;
    }
}

int Fat32Filesystem::read_cluster(uint32_t cluster, void* dst) {
    if (!m_dev || cluster < 2) return -1;
    uint64_t lba = cluster_to_lba(cluster);
    storage::BlockStatus st = m_dev->read_sectors(lba, m_bpb.sectors_per_cluster, dst);
    return (st == storage::BlockStatus::Success) ? 0 : -1;
}

int Fat32Filesystem::write_cluster(uint32_t cluster, const void* src) {
    if (!m_dev || cluster < 2) return -1;
    uint64_t lba = cluster_to_lba(cluster);
    storage::BlockStatus st = m_dev->write_sectors(lba, m_bpb.sectors_per_cluster, src);
    return (st == storage::BlockStatus::Success) ? 0 : -1;
}

Fat32VNode* Fat32Filesystem::alloc_vnode(NodeType type, uint32_t first_cluster, uint32_t file_size, uint32_t dir_cluster, uint32_t dir_offset) {
    sync::SpinlockGuard guard(m_fs_lock);
    for (size_t i = 0; i < MAX_CACHED_VNODES; ++i) {
        if (!m_vnode_in_use[i]) {
            m_vnode_in_use[i] = true;
            m_vnode_pool[i] = Fat32VNode(this, type, first_cluster, file_size, dir_cluster, dir_offset);
            m_vnode_count++;
            return &m_vnode_pool[i];
        }
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// Fat32VNode Implementation
// -----------------------------------------------------------------------------

Fat32VNode::Fat32VNode(Fat32Filesystem* fs, NodeType type, uint32_t first_cluster, uint32_t file_size, uint32_t dir_cluster, uint32_t dir_offset) noexcept
    : m_fs(fs),
      m_type(type),
      m_first_cluster(first_cluster),
      m_file_size(file_size),
      m_dir_cluster(dir_cluster),
      m_dir_offset(dir_offset) {}

void Fat32VNode::update_dir_entry(uint32_t new_size, uint32_t new_first_cluster) {
    if (!m_fs || m_dir_cluster < 2) return;

    m_file_size = new_size;
    m_first_cluster = new_first_cluster;

    size_t bpc = m_fs->bytes_per_cluster();
    void* buf = memory::kmalloc(bpc);
    if (!buf) return;

    if (m_fs->read_cluster(m_dir_cluster, buf) == 0) {
        auto* entry = reinterpret_cast<Fat32DirEntry*>(static_cast<uint8_t*>(buf) + m_dir_offset);
        entry->file_size = new_size;
        entry->first_cluster_low = static_cast<uint16_t>(new_first_cluster & 0xFFFF);
        entry->first_cluster_high = static_cast<uint16_t>((new_first_cluster >> 16) & 0xFFFF);
        m_fs->write_cluster(m_dir_cluster, buf);
        m_fs->device()->flush();
    }

    memory::kfree(buf);
}

int64_t Fat32VNode::read(uint64_t offset, size_t count, void* dst) {
    if (!m_fs || !dst || m_type != NodeType::File) return -1;
    if (offset >= m_file_size || count == 0) return 0;

    size_t bpc = m_fs->bytes_per_cluster();
    void* cluster_buf = memory::kmalloc(bpc);
    if (!cluster_buf) return -1;

    size_t bytes_to_read = count;
    if (offset + bytes_to_read > m_file_size) {
        bytes_to_read = static_cast<size_t>(m_file_size - offset);
    }

    // Traverse cluster chain to starting offset
    uint32_t cluster_index = static_cast<uint32_t>(offset / bpc);
    uint32_t offset_in_cluster = static_cast<uint32_t>(offset % bpc);

    uint32_t curr = m_first_cluster;
    size_t hops = 0;
    for (uint32_t i = 0; i < cluster_index && curr >= 2 && curr < FAT32_EOC_MIN; ++i) {
        if (++hops > m_fs->total_clusters()) {
            curr = FAT32_EOC_MIN;
            break;
        }
        curr = m_fs->read_fat_entry(curr);
    }

    size_t total_read = 0;
    uint8_t* out_p = static_cast<uint8_t*>(dst);

    while (bytes_to_read > 0 && curr >= 2 && curr < FAT32_EOC_MIN) {
        if (m_fs->read_cluster(curr, cluster_buf) != 0) break;

        size_t chunk = bpc - offset_in_cluster;
        if (chunk > bytes_to_read) chunk = bytes_to_read;

        llamaos::memcpy(out_p + total_read, static_cast<uint8_t*>(cluster_buf) + offset_in_cluster, chunk);

        total_read += chunk;
        bytes_to_read -= chunk;
        offset_in_cluster = 0;

        if (++hops > m_fs->total_clusters()) break;
        curr = m_fs->read_fat_entry(curr);
    }

    memory::kfree(cluster_buf);
    return static_cast<int64_t>(total_read);
}

int64_t Fat32VNode::write(uint64_t offset, size_t count, const void* src) {
    if (!m_fs || !src || m_type != NodeType::File) return -1;
    if (count == 0) return 0;

    size_t bpc = m_fs->bytes_per_cluster();
    void* cluster_buf = memory::kmalloc(bpc);
    if (!cluster_buf) return -1;

    if (m_first_cluster < 2) {
        m_first_cluster = m_fs->alloc_cluster(0);
        if (m_first_cluster < 2) {
            memory::kfree(cluster_buf);
            return -1;
        }
    }

    uint32_t target_cluster_idx = static_cast<uint32_t>(offset / bpc);
    uint32_t offset_in_cluster = static_cast<uint32_t>(offset % bpc);

    uint32_t curr = m_first_cluster;
    size_t write_hops = 0;
    for (uint32_t i = 0; i < target_cluster_idx; ++i) {
        if (++write_hops > m_fs->total_clusters()) {
            klog_warn("FAT32: Cluster cycle detected during write seek!");
            memory::kfree(cluster_buf);
            return -1;
        }
        uint32_t next = m_fs->read_fat_entry(curr);
        if (next >= FAT32_EOC_MIN || next < 2) {
            // Allocate new cluster to extend file
            next = m_fs->alloc_cluster(curr);
            if (next < 2) {
                memory::kfree(cluster_buf);
                return -1;
            }
        }
        curr = next;
    }

    size_t bytes_to_write = count;
    size_t total_written = 0;
    const uint8_t* in_p = static_cast<const uint8_t*>(src);

    while (bytes_to_write > 0 && curr >= 2 && curr < FAT32_EOC_MIN) {
        // If not overwriting entire cluster, read existing data first
        if (offset_in_cluster > 0 || bytes_to_write < bpc) {
            m_fs->read_cluster(curr, cluster_buf);
        }

        size_t chunk = bpc - offset_in_cluster;
        if (chunk > bytes_to_write) chunk = bytes_to_write;

        llamaos::memcpy(static_cast<uint8_t*>(cluster_buf) + offset_in_cluster, in_p + total_written, chunk);

        if (m_fs->write_cluster(curr, cluster_buf) != 0) break;

        total_written += chunk;
        bytes_to_write -= chunk;
        offset_in_cluster = 0;

        if (bytes_to_write > 0) {
            uint32_t next = m_fs->read_fat_entry(curr);
            if (next >= FAT32_EOC_MIN || next < 2) {
                next = m_fs->alloc_cluster(curr);
                if (next < 2) break;
            }
            curr = next;
        }
    }

    uint32_t new_size = m_file_size;
    if (offset + total_written > m_file_size) {
        new_size = static_cast<uint32_t>(offset + total_written);
    }
    update_dir_entry(new_size, m_first_cluster);

    memory::kfree(cluster_buf);
    return static_cast<int64_t>(total_written);
}

int Fat32VNode::truncate(uint64_t new_size) {
    if (m_type != NodeType::File) return -1;
    if (new_size == 0 && m_first_cluster >= 2) {
        m_fs->free_cluster_chain(m_first_cluster);
        m_first_cluster = 0;
    }
    update_dir_entry(static_cast<uint32_t>(new_size), m_first_cluster);
    return 0;
}

int Fat32VNode::lookup(const char* name, VNode** out_child) {
    if (!m_fs || !name || !out_child || m_type != NodeType::Directory) return -1;

    char target_83[11];
    Fat32Filesystem::format_to_83(name, target_83);

    size_t bpc = m_fs->bytes_per_cluster();
    void* cluster_buf = memory::kmalloc(bpc);
    if (!cluster_buf) return -1;

    char lfn_buf[256]{0};
    uint32_t curr = m_first_cluster;

    while (curr >= 2 && curr < FAT32_EOC_MIN) {
        if (m_fs->read_cluster(curr, cluster_buf) != 0) break;

        size_t entries_count = bpc / sizeof(Fat32DirEntry);
        const auto* entries = static_cast<const Fat32DirEntry*>(cluster_buf);

        for (size_t i = 0; i < entries_count; ++i) {
            const Fat32DirEntry& e = entries[i];
            if (static_cast<uint8_t>(e.name[0]) == 0x00) {
                // End of directory
                memory::kfree(cluster_buf);
                return -2;
            }
            if (static_cast<uint8_t>(e.name[0]) == 0xE5) {
                lfn_buf[0] = '\0';
                continue;
            }
            if ((e.attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
                const auto* lfn = reinterpret_cast<const Fat32LfnEntry*>(&e);
                extract_lfn_chars(lfn, lfn_buf, sizeof(lfn_buf));
                continue;
            }
            if ((e.attr & FAT_ATTR_VOLUME_ID) != 0) {
                lfn_buf[0] = '\0';
                continue;
            }

            // Compare against LFN if available, or 8.3 target
            bool match = false;
            if (lfn_buf[0] != '\0' && strcasecmp_match(name, lfn_buf)) {
                match = true;
            } else {
                bool match_83 = true;
                for (size_t k = 0; k < 11; ++k) {
                    if (e.name[k] != target_83[k]) {
                        match_83 = false;
                        break;
                    }
                }
                if (match_83) match = true;
            }

            if (match) {
                uint32_t child_cluster = (static_cast<uint32_t>(e.first_cluster_high) << 16) | e.first_cluster_low;
                NodeType ctype = (e.attr & FAT_ATTR_DIRECTORY) ? NodeType::Directory : NodeType::File;
                uint32_t dir_offset = static_cast<uint32_t>(i * sizeof(Fat32DirEntry));

                Fat32VNode* node = m_fs->alloc_vnode(ctype, child_cluster, e.file_size, curr, dir_offset);
                memory::kfree(cluster_buf);
                if (!node) return -3;
                *out_child = node;
                return 0;
            }

            lfn_buf[0] = '\0';
        }

        curr = m_fs->read_fat_entry(curr);
    }

    memory::kfree(cluster_buf);
    return -2; // Not found
}

int Fat32VNode::create(const char* name, NodeType type, VNode** out_created) {
    if (!m_fs || !name || m_type != NodeType::Directory) return -1;

    char target_83[11];
    Fat32Filesystem::format_to_83(name, target_83);

    size_t bpc = m_fs->bytes_per_cluster();
    void* cluster_buf = memory::kmalloc(bpc);
    if (!cluster_buf) return -1;

    uint32_t curr = m_first_cluster;
    uint32_t prev = 0;
    while (curr >= 2 && curr < FAT32_EOC_MIN) {
        if (m_fs->read_cluster(curr, cluster_buf) != 0) break;

        size_t entries_count = bpc / sizeof(Fat32DirEntry);
        auto* entries = static_cast<Fat32DirEntry*>(cluster_buf);

        for (size_t i = 0; i < entries_count; ++i) {
            uint8_t first_byte = static_cast<uint8_t>(entries[i].name[0]);
            if (first_byte == 0x00 || first_byte == 0xE5) {
                // Free slot found!
                llamaos::memcpy(entries[i].name, target_83, 11);
                entries[i].attr = (type == NodeType::Directory) ? FAT_ATTR_DIRECTORY : FAT_ATTR_ARCHIVE;
                entries[i].file_size = 0;

                uint32_t new_cluster = 0;
                if (type == NodeType::Directory) {
                    new_cluster = m_fs->alloc_cluster(0);
                    // Initialize '.' and '..' in directory cluster
                    void* dir_init_buf = memory::kmalloc(bpc);
                    if (dir_init_buf) {
                        llamaos::memset(dir_init_buf, 0, bpc);
                        auto* d_entries = static_cast<Fat32DirEntry*>(dir_init_buf);
                        // Entry 0: "."
                        Fat32Filesystem::format_to_83(".", d_entries[0].name);
                        d_entries[0].attr = FAT_ATTR_DIRECTORY;
                        d_entries[0].first_cluster_low = static_cast<uint16_t>(new_cluster & 0xFFFF);
                        d_entries[0].first_cluster_high = static_cast<uint16_t>((new_cluster >> 16) & 0xFFFF);
                        // Entry 1: ".."
                        Fat32Filesystem::format_to_83("..", d_entries[1].name);
                        d_entries[1].attr = FAT_ATTR_DIRECTORY;
                        d_entries[1].first_cluster_low = static_cast<uint16_t>(m_first_cluster & 0xFFFF);
                        d_entries[1].first_cluster_high = static_cast<uint16_t>((m_first_cluster >> 16) & 0xFFFF);

                        m_fs->write_cluster(new_cluster, dir_init_buf);
                        memory::kfree(dir_init_buf);
                    }
                }

                entries[i].first_cluster_low = static_cast<uint16_t>(new_cluster & 0xFFFF);
                entries[i].first_cluster_high = static_cast<uint16_t>((new_cluster >> 16) & 0xFFFF);

                m_fs->write_cluster(curr, cluster_buf);
                m_fs->device()->flush();

                uint32_t dir_offset = static_cast<uint32_t>(i * sizeof(Fat32DirEntry));
                Fat32VNode* node = m_fs->alloc_vnode(type, new_cluster, 0, curr, dir_offset);
                memory::kfree(cluster_buf);
                if (out_created) *out_created = node;
                return 0;
            }
        }

        prev = curr;
        curr = m_fs->read_fat_entry(curr);
    }

    // If directory has no free slots, extend directory by allocating cluster
    if (prev >= 2) {
        uint32_t new_dir_cluster = m_fs->alloc_cluster(prev);
        if (new_dir_cluster >= 2) {
            llamaos::memset(cluster_buf, 0, bpc);
            auto* entries = static_cast<Fat32DirEntry*>(cluster_buf);
            llamaos::memcpy(entries[0].name, target_83, 11);
            entries[0].attr = (type == NodeType::Directory) ? FAT_ATTR_DIRECTORY : FAT_ATTR_ARCHIVE;
            entries[0].file_size = 0;
            entries[0].first_cluster_low = 0;
            entries[0].first_cluster_high = 0;

            m_fs->write_cluster(new_dir_cluster, cluster_buf);
            m_fs->device()->flush();

            Fat32VNode* node = m_fs->alloc_vnode(type, 0, 0, new_dir_cluster, 0);
            memory::kfree(cluster_buf);
            if (out_created) *out_created = node;
            return 0;
        }
    }

    memory::kfree(cluster_buf);
    return -1;
}

int Fat32VNode::mkdir(const char* name, VNode** out_created) {
    return create(name, NodeType::Directory, out_created);
}

int Fat32VNode::unlink(const char* name) {
    if (!m_fs || !name || m_type != NodeType::Directory) return -1;

    char target_83[11];
    Fat32Filesystem::format_to_83(name, target_83);

    size_t bpc = m_fs->bytes_per_cluster();
    void* cluster_buf = memory::kmalloc(bpc);
    if (!cluster_buf) return -1;

    uint32_t curr = m_first_cluster;
    while (curr >= 2 && curr < FAT32_EOC_MIN) {
        if (m_fs->read_cluster(curr, cluster_buf) != 0) break;

        size_t entries_count = bpc / sizeof(Fat32DirEntry);
        auto* entries = static_cast<Fat32DirEntry*>(cluster_buf);

        for (size_t i = 0; i < entries_count; ++i) {
            Fat32DirEntry& e = entries[i];
            if (static_cast<uint8_t>(e.name[0]) == 0x00) break;
            if (static_cast<uint8_t>(e.name[0]) == 0xE5) continue;
            if ((e.attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) continue;

            bool match = true;
            for (size_t k = 0; k < 11; ++k) {
                if (e.name[k] != target_83[k]) { match = false; break; }
            }

            if (match) {
                uint32_t cluster_to_free = (static_cast<uint32_t>(e.first_cluster_high) << 16) | e.first_cluster_low;
                e.name[0] = static_cast<char>(0xE5); // Mark deleted

                m_fs->write_cluster(curr, cluster_buf);
                m_fs->device()->flush();

                if (cluster_to_free >= 2) {
                    m_fs->free_cluster_chain(cluster_to_free);
                }

                memory::kfree(cluster_buf);
                return 0;
            }
        }
        curr = m_fs->read_fat_entry(curr);
    }

    memory::kfree(cluster_buf);
    return -2; // Not found
}

int Fat32VNode::readdir(uint32_t index, DirEntry* entry) {
    if (!m_fs || !entry || m_type != NodeType::Directory) return -1;

    size_t bpc = m_fs->bytes_per_cluster();
    void* cluster_buf = memory::kmalloc(bpc);
    if (!cluster_buf) return -1;

    char lfn_buf[256]{0};
    uint32_t curr = m_first_cluster;
    uint32_t valid_count = 0;

    while (curr >= 2 && curr < FAT32_EOC_MIN) {
        if (m_fs->read_cluster(curr, cluster_buf) != 0) break;

        size_t entries_count = bpc / sizeof(Fat32DirEntry);
        const auto* entries = static_cast<const Fat32DirEntry*>(cluster_buf);

        for (size_t i = 0; i < entries_count; ++i) {
            const Fat32DirEntry& e = entries[i];
            if (static_cast<uint8_t>(e.name[0]) == 0x00) {
                memory::kfree(cluster_buf);
                return -2; // End of directory
            }
            if (static_cast<uint8_t>(e.name[0]) == 0xE5) {
                lfn_buf[0] = '\0';
                continue;
            }
            if ((e.attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
                const auto* lfn = reinterpret_cast<const Fat32LfnEntry*>(&e);
                extract_lfn_chars(lfn, lfn_buf, sizeof(lfn_buf));
                continue;
            }
            if ((e.attr & FAT_ATTR_VOLUME_ID) != 0) {
                lfn_buf[0] = '\0';
                continue;
            }

            if (valid_count == index) {
                if (lfn_buf[0] != '\0') {
                    llamaos::strncpy(entry->name, lfn_buf, sizeof(entry->name) - 1);
                    entry->name[sizeof(entry->name) - 1] = '\0';
                } else {
                    Fat32Filesystem::format_from_83(e.name, entry->name, sizeof(entry->name));
                }
                entry->type = (e.attr & FAT_ATTR_DIRECTORY) ? NodeType::Directory : NodeType::File;
                entry->size = e.file_size;
                entry->inode = (static_cast<uint32_t>(e.first_cluster_high) << 16) | e.first_cluster_low;
                memory::kfree(cluster_buf);
                return 0;
            }
            valid_count++;
            lfn_buf[0] = '\0';
        }
        curr = m_fs->read_fat_entry(curr);
    }

    memory::kfree(cluster_buf);
    return -2; // Out of range
}

int Fat32VNode::stat(FileStat* st) {
    if (!st) return -1;
    st->type = m_type;
    st->size = m_file_size;
    st->inode = m_first_cluster;
    st->block_size = m_fs ? m_fs->bytes_per_cluster() : 512;
    st->blocks = (m_file_size + st->block_size - 1) / st->block_size;
    return 0;
}

} // namespace llamaos::fs
