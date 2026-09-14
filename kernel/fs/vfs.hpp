#pragma once

#include "core/types.hpp"
#include "storage/block_device.hpp"

// =============================================================================
// LlamaOS/A - Virtual Filesystem (VFS) Layer
// =============================================================================
// Provides uniform POSIX-compatible filesystem abstractions:
// VNode, Filesystem driver interface, Mount table, File Descriptors,
// Path Resolution (with '.' and '..' handling), Directory Enumeration,
// and File Operations (open, close, read, write, seek, stat, mkdir, unlink).
// =============================================================================

namespace llamaos::fs {

enum class NodeType : uint8_t {
    Unknown,
    File,
    Directory,
    BlockDevice,
    CharacterDevice
};

// Open flags
inline constexpr uint32_t O_RDONLY    = 0x0001;
inline constexpr uint32_t O_WRONLY    = 0x0002;
inline constexpr uint32_t O_RDWR      = 0x0003;
inline constexpr uint32_t O_CREAT     = 0x0040;
inline constexpr uint32_t O_TRUNC     = 0x0200;
inline constexpr uint32_t O_APPEND    = 0x0400;

enum class SeekOrigin : uint8_t {
    Set = 0,
    Current = 1,
    End = 2
};

struct FileStat {
    NodeType type{NodeType::Unknown};
    uint64_t size{0};
    uint64_t inode{0};
    uint32_t block_size{512};
    uint64_t blocks{0};
};

struct DirEntry {
    char     name[64]{0};
    NodeType type{NodeType::Unknown};
    uint64_t size{0};
    uint64_t inode{0};
};

class VNode;

class Filesystem {
public:
    virtual ~Filesystem() = default;
    [[nodiscard]] virtual const char* name() const noexcept = 0;
    virtual int mount(storage::BlockDevice* dev, VNode** out_root) = 0;
    virtual int unmount() = 0;
};

class VNode {
public:
    virtual ~VNode() = default;

    [[nodiscard]] virtual NodeType type() const noexcept = 0;
    [[nodiscard]] virtual uint64_t size() const noexcept = 0;

    // File operations
    virtual int64_t read(uint64_t offset, size_t count, void* dst) = 0;
    virtual int64_t write(uint64_t offset, size_t count, const void* src) = 0;
    virtual int truncate(uint64_t new_size) = 0;

    // Directory operations
    virtual int lookup(const char* name, VNode** out_child) = 0;
    virtual int create(const char* name, NodeType type, VNode** out_created) = 0;
    virtual int mkdir(const char* name, VNode** out_created) = 0;
    virtual int unlink(const char* name) = 0;
    virtual int readdir(uint32_t index, DirEntry* entry) = 0;

    virtual int stat(FileStat* st) = 0;
};

struct FileDescriptor {
    VNode*   vnode{nullptr};
    uint32_t flags{0};
    uint64_t offset{0};
    bool     in_use{false};
};

class Vfs {
public:
    static constexpr size_t MAX_MOUNTS = 8;
    static constexpr size_t MAX_FILE_DESCRIPTORS = 64;
    static constexpr size_t PATH_MAX = 256;

    static void init();

    // Mount management
    static int mount(const char* mountpoint, storage::BlockDevice* dev, Filesystem* fs);
    static int unmount(const char* mountpoint);

    [[nodiscard]] static VNode* root_vnode() noexcept;

    // Path normalization & resolution
    static int normalize_path(const char* in_path, char* out_path, size_t max_len);
    static int resolve_path(const char* path, VNode** out_vnode, char* out_basename = nullptr, VNode** out_parent = nullptr);

    // Standard POSIX-style System Calls Backend
    static int open(const char* path, uint32_t flags);
    static int close(int fd);
    static int64_t read(int fd, void* dst, size_t count);
    static int64_t write(int fd, const void* src, size_t count);
    static int64_t seek(int fd, int64_t offset, SeekOrigin origin);
    static int stat(const char* path, FileStat* st);
    static int fstat(int fd, FileStat* st);
    static int mkdir(const char* path);
    static int readdir(int fd, uint32_t index, DirEntry* entry);
    static int unlink(const char* path);

private:
    struct MountPoint {
        char                  path[32]{0};
        storage::BlockDevice* device{nullptr};
        Filesystem*           fs{nullptr};
        VNode*                root{nullptr};
        bool                  active{false};
    };

    static MountPoint     s_mounts[MAX_MOUNTS];
    static FileDescriptor s_fds[MAX_FILE_DESCRIPTORS];
    static bool           s_initialized;
};

} // namespace llamaos::fs
