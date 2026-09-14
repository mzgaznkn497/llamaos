#include "fs/vfs.hpp"
#include "core/string.hpp"
#include "core/kprint.hpp"
#include "sync/spinlock.hpp"

namespace llamaos::fs {

Vfs::MountPoint     Vfs::s_mounts[MAX_MOUNTS]{};
FileDescriptor      Vfs::s_fds[MAX_FILE_DESCRIPTORS]{};
bool                Vfs::s_initialized{false};

static sync::Spinlock s_vfs_lock;

void Vfs::init() {
    sync::SpinlockGuard guard(s_vfs_lock);
    for (size_t i = 0; i < MAX_MOUNTS; ++i) {
        s_mounts[i] = MountPoint{};
    }
    for (size_t i = 0; i < MAX_FILE_DESCRIPTORS; ++i) {
        s_fds[i] = FileDescriptor{};
    }
    s_initialized = true;
    klog_info("VFS initialized (Max Mounts: %u, Max FDs: %u).",
              static_cast<uint32_t>(MAX_MOUNTS), static_cast<uint32_t>(MAX_FILE_DESCRIPTORS));
}

int Vfs::mount(const char* mountpoint, storage::BlockDevice* dev, Filesystem* fs) {
    if (!mountpoint || !dev || !fs) return -1;

    sync::SpinlockGuard guard(s_vfs_lock);
    if (!s_initialized) {
        for (size_t i = 0; i < MAX_MOUNTS; ++i) s_mounts[i] = MountPoint{};
        for (size_t i = 0; i < MAX_FILE_DESCRIPTORS; ++i) s_fds[i] = FileDescriptor{};
        s_initialized = true;
    }

    // Find free mount slot
    MountPoint* slot = nullptr;
    for (size_t i = 0; i < MAX_MOUNTS; ++i) {
        if (!s_mounts[i].active) {
            slot = &s_mounts[i];
            break;
        }
    }
    if (!slot) return -2;

    VNode* root = nullptr;
    int res = fs->mount(dev, &root);
    if (res != 0 || !root) {
        klog_error("VFS: Failed to mount filesystem '%s' on device '%s' (err %d)",
                   fs->name(), dev->name(), res);
        return res;
    }

    llamaos::strncpy(slot->path, mountpoint, sizeof(slot->path) - 1);
    slot->path[sizeof(slot->path) - 1] = '\0';
    slot->device = dev;
    slot->fs = fs;
    slot->root = root;
    slot->active = true;

    klog_info("VFS: Mounted '%s' on '%s' (Device: '%s')",
              fs->name(), slot->path, dev->name());

    return 0;
}

int Vfs::unmount(const char* mountpoint) {
    if (!mountpoint) return -1;

    sync::SpinlockGuard guard(s_vfs_lock);
    for (size_t i = 0; i < MAX_MOUNTS; ++i) {
        if (s_mounts[i].active && llamaos::strcmp(s_mounts[i].path, mountpoint) == 0) {
            int res = s_mounts[i].fs->unmount();
            s_mounts[i] = MountPoint{};
            klog_info("VFS: Unmounted '%s'", mountpoint);
            return res;
        }
    }
    return -1;
}

VNode* Vfs::root_vnode() noexcept {
    for (size_t i = 0; i < MAX_MOUNTS; ++i) {
        if (s_mounts[i].active && llamaos::strcmp(s_mounts[i].path, "/") == 0) {
            return s_mounts[i].root;
        }
    }
    return nullptr;
}

int Vfs::normalize_path(const char* in_path, char* out_path, size_t max_len) {
    if (!in_path || !out_path || max_len < 2) return -1;

    size_t in_len = llamaos::strlen(in_path);
    if (in_len >= PATH_MAX || in_len == 0) return -1;

    // Segment stack
    char segments[16][64];
    size_t seg_count = 0;

    size_t i = 0;
    while (i < in_len) {
        // Skip consecutive '/'
        while (i < in_len && in_path[i] == '/') i++;
        if (i >= in_len) break;

        size_t start = i;
        while (i < in_len && in_path[i] != '/') i++;
        size_t len = i - start;

        if (len == 1 && in_path[start] == '.') {
            // Ignore '.'
            continue;
        } else if (len == 2 && in_path[start] == '.' && in_path[start + 1] == '.') {
            // Pop '..'
            if (seg_count > 0) seg_count--;
        } else {
            if (seg_count >= 16 || len >= sizeof(segments[0])) return -1;
            llamaos::memcpy(segments[seg_count], &in_path[start], len);
            segments[seg_count][len] = '\0';
            seg_count++;
        }
    }

    if (seg_count == 0) {
        out_path[0] = '/';
        out_path[1] = '\0';
        return 0;
    }

    size_t out_idx = 0;
    for (size_t s = 0; s < seg_count; ++s) {
        if (out_idx + 1 >= max_len) return -1;
        out_path[out_idx++] = '/';
        size_t slen = llamaos::strlen(segments[s]);
        if (out_idx + slen >= max_len) return -1;
        llamaos::memcpy(&out_path[out_idx], segments[s], slen);
        out_idx += slen;
    }
    out_path[out_idx] = '\0';
    return 0;
}

int Vfs::resolve_path(const char* path, VNode** out_vnode, char* out_basename, VNode** out_parent) {
    if (!path) return -1;

    char norm[PATH_MAX];
    if (normalize_path(path, norm, sizeof(norm)) != 0) return -1;

    VNode* curr = root_vnode();
    if (!curr) return -2;

    if (norm[0] == '/' && norm[1] == '\0') {
        if (out_vnode) *out_vnode = curr;
        if (out_parent) *out_parent = nullptr;
        if (out_basename) {
            out_basename[0] = '/';
            out_basename[1] = '\0';
        }
        return 0;
    }

    // Split segments and walk
    size_t len = llamaos::strlen(norm);
    size_t i = 1; // skip leading '/'

    VNode* parent = curr;
    char last_seg[64]{0};

    while (i < len) {
        size_t start = i;
        while (i < len && norm[i] != '/') i++;
        size_t seg_len = i - start;

        llamaos::memcpy(last_seg, &norm[start], seg_len);
        last_seg[seg_len] = '\0';

        if (i < len) {
            // Internal directory segment
            VNode* child = nullptr;
            int res = curr->lookup(last_seg, &child);
            if (res != 0 || !child) return -3;
            parent = curr;
            curr = child;
            i++; // skip '/'
        } else {
            // Final leaf segment
            parent = curr;
            VNode* leaf = nullptr;
            int res = curr->lookup(last_seg, &leaf);
            if (out_basename) {
                llamaos::strncpy(out_basename, last_seg, 63);
                out_basename[63] = '\0';
            }
            if (out_parent) *out_parent = parent;
            if (out_vnode) *out_vnode = leaf;
            return (res == 0 && leaf) ? 0 : 1; // 1 means parent found, leaf doesn't exist
        }
    }

    return -1;
}

int Vfs::open(const char* path, uint32_t flags) {
    if (!path) return -1;

    VNode* node = nullptr;
    VNode* parent = nullptr;
    char basename[64]{0};

    int lookup_res = resolve_path(path, &node, basename, &parent);

    if (lookup_res == 1 && (flags & O_CREAT)) {
        // Create new file
        if (!parent) return -2;
        int cres = parent->create(basename, NodeType::File, &node);
        if (cres != 0 || !node) return -3;
    } else if (lookup_res != 0 || !node) {
        return -4; // Not found
    }

    if (flags & O_TRUNC) {
        node->truncate(0);
    }

    sync::SpinlockGuard guard(s_vfs_lock);
    int fd = -1;
    for (size_t i = 3; i < MAX_FILE_DESCRIPTORS; ++i) { // reserve 0, 1, 2 for stdio
        if (!s_fds[i].in_use) {
            fd = static_cast<int>(i);
            break;
        }
    }
    if (fd < 0) return -5; // Out of file descriptors

    s_fds[fd].vnode = node;
    s_fds[fd].flags = flags;
    s_fds[fd].offset = (flags & O_APPEND) ? node->size() : 0;
    s_fds[fd].in_use = true;

    return fd;
}

int Vfs::close(int fd) {
    if (fd < 0 || static_cast<size_t>(fd) >= MAX_FILE_DESCRIPTORS) return -1;

    sync::SpinlockGuard guard(s_vfs_lock);
    if (!s_fds[fd].in_use) return -1;

    s_fds[fd].vnode = nullptr;
    s_fds[fd].flags = 0;
    s_fds[fd].offset = 0;
    s_fds[fd].in_use = false;

    return 0;
}

int64_t Vfs::read(int fd, void* dst, size_t count) {
    if (fd < 0 || static_cast<size_t>(fd) >= MAX_FILE_DESCRIPTORS || !dst) return -1;

    sync::SpinlockGuard guard(s_vfs_lock);
    if (!s_fds[fd].in_use || !s_fds[fd].vnode) return -1;

    int64_t bytes = s_fds[fd].vnode->read(s_fds[fd].offset, count, dst);
    if (bytes > 0) {
        s_fds[fd].offset += static_cast<uint64_t>(bytes);
    }
    return bytes;
}

int64_t Vfs::write(int fd, const void* src, size_t count) {
    if (fd < 0 || static_cast<size_t>(fd) >= MAX_FILE_DESCRIPTORS || !src) return -1;

    sync::SpinlockGuard guard(s_vfs_lock);
    if (!s_fds[fd].in_use || !s_fds[fd].vnode) return -1;

    int64_t bytes = s_fds[fd].vnode->write(s_fds[fd].offset, count, src);
    if (bytes > 0) {
        s_fds[fd].offset += static_cast<uint64_t>(bytes);
    }
    return bytes;
}

int64_t Vfs::seek(int fd, int64_t offset, SeekOrigin origin) {
    if (fd < 0 || static_cast<size_t>(fd) >= MAX_FILE_DESCRIPTORS) return -1;

    sync::SpinlockGuard guard(s_vfs_lock);
    if (!s_fds[fd].in_use || !s_fds[fd].vnode) return -1;

    int64_t new_offset = 0;
    switch (origin) {
    case SeekOrigin::Set:
        new_offset = offset;
        break;
    case SeekOrigin::Current:
        new_offset = static_cast<int64_t>(s_fds[fd].offset) + offset;
        break;
    case SeekOrigin::End:
        new_offset = static_cast<int64_t>(s_fds[fd].vnode->size()) + offset;
        break;
    default:
        return -1;
    }

    if (new_offset < 0) return -2;
    s_fds[fd].offset = static_cast<uint64_t>(new_offset);
    return new_offset;
}

int Vfs::stat(const char* path, FileStat* st) {
    if (!path || !st) return -1;
    VNode* node = nullptr;
    if (resolve_path(path, &node) != 0 || !node) return -2;
    return node->stat(st);
}

int Vfs::fstat(int fd, FileStat* st) {
    if (fd < 0 || static_cast<size_t>(fd) >= MAX_FILE_DESCRIPTORS || !st) return -1;
    sync::SpinlockGuard guard(s_vfs_lock);
    if (!s_fds[fd].in_use || !s_fds[fd].vnode) return -1;
    return s_fds[fd].vnode->stat(st);
}

int Vfs::mkdir(const char* path) {
    if (!path) return -1;
    VNode* parent = nullptr;
    VNode* existing = nullptr;
    char basename[64]{0};

    int lookup = resolve_path(path, &existing, basename, &parent);
    if (lookup == 0 && existing) return -2; // already exists
    if (!parent) return -3;

    VNode* created = nullptr;
    return parent->mkdir(basename, &created);
}

int Vfs::readdir(int fd, uint32_t index, DirEntry* entry) {
    if (fd < 0 || static_cast<size_t>(fd) >= MAX_FILE_DESCRIPTORS || !entry) return -1;
    sync::SpinlockGuard guard(s_vfs_lock);
    if (!s_fds[fd].in_use || !s_fds[fd].vnode) return -1;
    return s_fds[fd].vnode->readdir(index, entry);
}

int Vfs::unlink(const char* path) {
    if (!path) return -1;
    VNode* parent = nullptr;
    VNode* target = nullptr;
    char basename[64]{0};

    int lookup = resolve_path(path, &target, basename, &parent);
    if (lookup != 0 || !parent || !target) return -2; // Not found

    return parent->unlink(basename);
}

} // namespace llamaos::fs
