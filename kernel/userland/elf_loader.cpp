#include "elf_loader.hpp"
#include "memory/pmm.hpp"
#include "memory/vmm.hpp"
#include "memory/kalloc.hpp"
#include "fs/vfs.hpp"
#include "core/kprint.hpp"
#include "core/string.hpp"

// =============================================================================
// LlamaOS/A - Minimal ELF64 Loader Implementation
// =============================================================================

namespace llamaos::userland {

const char* to_string(ElfLoadStatus status) noexcept {
    switch (status) {
        case ElfLoadStatus::Success:             return "Success";
        case ElfLoadStatus::NullPointer:         return "NullPointer";
        case ElfLoadStatus::BufferTooSmall:      return "BufferTooSmall";
        case ElfLoadStatus::InvalidMagic:        return "InvalidMagic";
        case ElfLoadStatus::InvalidClass:        return "InvalidClass";
        case ElfLoadStatus::InvalidDataEncoding: return "InvalidDataEncoding";
        case ElfLoadStatus::InvalidMachine:      return "InvalidMachine";
        case ElfLoadStatus::InvalidType:         return "InvalidType";
        case ElfLoadStatus::InvalidVersion:      return "InvalidVersion";
        case ElfLoadStatus::InvalidHeaderSize:   return "InvalidHeaderSize";
        case ElfLoadStatus::InvalidPhSize:       return "InvalidPhSize";
        case ElfLoadStatus::PhTableOutOfBounds:  return "PhTableOutOfBounds";
        case ElfLoadStatus::SegmentOutOfBounds:  return "SegmentOutOfBounds";
        case ElfLoadStatus::IntegerOverflow:     return "IntegerOverflow";
        case ElfLoadStatus::KernelAddressOverlap:return "KernelAddressOverlap";
        case ElfLoadStatus::NonCanonicalAddress: return "NonCanonicalAddress";
        case ElfLoadStatus::NoLoadableSegments:  return "NoLoadableSegments";
        case ElfLoadStatus::EntryNotExecutable:  return "EntryNotExecutable";
        case ElfLoadStatus::AllocationFailed:    return "AllocationFailed";
        case ElfLoadStatus::MappingFailed:       return "MappingFailed";
        default:                                 return "UnknownStatus";
    }
}

ElfLoadStatus ElfLoader::validate_header(const Elf64Header* header, size_t file_size) noexcept {
    if (!header) return ElfLoadStatus::NullPointer;
    if (file_size < sizeof(Elf64Header)) return ElfLoadStatus::BufferTooSmall;

    // 1. Validate ELF Magic: \x7f E L F
    if (header->e_ident[EI_MAG0] != ELFMAG0 ||
        header->e_ident[EI_MAG1] != ELFMAG1 ||
        header->e_ident[EI_MAG2] != ELFMAG2 ||
        header->e_ident[EI_MAG3] != ELFMAG3) {
        return ElfLoadStatus::InvalidMagic;
    }

    // 2. Validate 64-bit ELF class
    if (header->e_ident[EI_CLASS] != ELFCLASS64) {
        return ElfLoadStatus::InvalidClass;
    }

    // 3. Validate Little-Endian LSB data encoding
    if (header->e_ident[EI_DATA] != ELFDATA2LSB) {
        return ElfLoadStatus::InvalidDataEncoding;
    }

    // 4. Validate Version
    if (header->e_ident[EI_VERSION] != EV_CURRENT || header->e_version != EV_CURRENT) {
        return ElfLoadStatus::InvalidVersion;
    }

    // 5. Validate Machine: AMD64 / x86-64 (0x3E)
    if (header->e_machine != EM_X86_64) {
        return ElfLoadStatus::InvalidMachine;
    }

    // 6. Validate Executable Type (ET_EXEC = 2)
    if (header->e_type != ET_EXEC) {
        return ElfLoadStatus::InvalidType;
    }

    // 7. Validate Header and Program Header entry sizes
    if (header->e_ehsize != sizeof(Elf64Header)) {
        return ElfLoadStatus::InvalidHeaderSize;
    }
    if (header->e_phentsize != sizeof(Elf64ProgramHeader)) {
        return ElfLoadStatus::InvalidPhSize;
    }

    // 8. Validate Program Header table file bounds
    if (header->e_phnum == 0) {
        return ElfLoadStatus::NoLoadableSegments;
    }

    uint64_t ph_table_size = static_cast<uint64_t>(header->e_phnum) * sizeof(Elf64ProgramHeader);
    uint64_t ph_table_end = header->e_phoff + ph_table_size;
    if (ph_table_end < header->e_phoff || ph_table_end > file_size) {
        return ElfLoadStatus::PhTableOutOfBounds;
    }

    return ElfLoadStatus::Success;
}

ElfLoadStatus ElfLoader::validate_segments(const Elf64Header* header, const uint8_t* file_data, size_t file_size) noexcept {
    ElfLoadStatus status = validate_header(header, file_size);
    if (status != ElfLoadStatus::Success) return status;

    const auto* ph_table = reinterpret_cast<const Elf64ProgramHeader*>(file_data + header->e_phoff);
    size_t load_count = 0;
    bool entry_found_in_executable = false;

    for (size_t i = 0; i < header->e_phnum; ++i) {
        const auto& ph = ph_table[i];
        if (ph.p_type != PT_LOAD) continue;

        load_count++;

        // Check for file bounds and overflow
        uint64_t file_end = ph.p_offset + ph.p_filesz;
        if (file_end < ph.p_offset || file_end > file_size) {
            return ElfLoadStatus::SegmentOutOfBounds;
        }

        // Memory size must be >= file size (difference is BSS)
        if (ph.p_memsz < ph.p_filesz) {
            return ElfLoadStatus::IntegerOverflow;
        }

        // Check memory virtual address limits
        uint64_t mem_end = ph.p_vaddr + ph.p_memsz;
        if (mem_end < ph.p_vaddr) {
            return ElfLoadStatus::IntegerOverflow;
        }

        // Must reside in canonical lower-half user space (above null page, below 128 TiB)
        if (ph.p_vaddr < 0x1000ULL || mem_end > 0x0000800000000000ULL) {
            return ElfLoadStatus::KernelAddressOverlap;
        }

        // Check if entry point falls into this executable segment
        if ((ph.p_flags & PF_X) != 0) {
            if (header->e_entry >= ph.p_vaddr && header->e_entry < mem_end) {
                entry_found_in_executable = true;
            }
        }
    }

    if (load_count == 0) {
        return ElfLoadStatus::NoLoadableSegments;
    }

    if (!entry_found_in_executable) {
        return ElfLoadStatus::EntryNotExecutable;
    }

    return ElfLoadStatus::Success;
}

ElfLoadResult ElfLoader::load(const uint8_t* elf_data, size_t file_size, memory::PhysicalAddress pml4_pa) noexcept {
    ElfLoadResult res{};
    if (!elf_data) {
        res.status = ElfLoadStatus::NullPointer;
        return res;
    }

    memory::PhysicalAddress target_pml4 = pml4_pa.is_null() ? memory::g_vmm.root_pml4_address() : pml4_pa;

    const auto* header = reinterpret_cast<const Elf64Header*>(elf_data);
    res.status = validate_segments(header, elf_data, file_size);
    if (res.status != ElfLoadStatus::Success) {
        return res;
    }

    const auto* ph_table = reinterpret_cast<const Elf64ProgramHeader*>(elf_data + header->e_phoff);
    size_t total_pages_mapped = 0;

    // Load each PT_LOAD segment into user address space
    for (size_t i = 0; i < header->e_phnum; ++i) {
        const auto& ph = ph_table[i];
        if (ph.p_type != PT_LOAD) continue;

        uint64_t seg_start = ph.p_vaddr;
        uint64_t seg_end   = ph.p_vaddr + ph.p_memsz;
        uint64_t page_start = seg_start & ~(PAGE_SIZE - 1);
        uint64_t page_end   = (seg_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        // Derive PageFlags from ELF segment flags (W^X discipline)
        memory::PageFlags flags = memory::PageFlags::Present | memory::PageFlags::User;
        if ((ph.p_flags & PF_W) != 0) {
            flags |= memory::PageFlags::Writable;
        }
        if ((ph.p_flags & PF_X) == 0) {
            flags |= memory::PageFlags::NoExecute;
        }

        for (uint64_t va_val = page_start; va_val < page_end; va_val += PAGE_SIZE) {
            memory::VirtualAddress va(va_val);

            // Allocate a physical page frame
            memory::PhysicalAddress pa = memory::g_pmm.alloc_page();
            if (pa.is_null()) {
                res.status = ElfLoadStatus::AllocationFailed;
                return res;
            }

            // Zero-initialize physical page frame
            uint8_t* frame_ptr = reinterpret_cast<uint8_t*>(memory::phys_to_virt(pa).as_ptr());
            llamaos::memset(frame_ptr, 0, PAGE_SIZE);

            // Map into target page table with User permissions
            memory::VmmStatus vmm_stat = memory::g_vmm.map_page_in_table(target_pml4, va, pa, flags);
            if (vmm_stat != memory::VmmStatus::Success) {
                res.status = ElfLoadStatus::MappingFailed;
                return res;
            }
            total_pages_mapped++;

            // Copy file-backed payload into this page if overlapping [p_vaddr, p_vaddr + p_filesz)
            uint64_t page_min = va_val;
            uint64_t page_max = va_val + PAGE_SIZE;

            uint64_t file_start = ph.p_vaddr;
            uint64_t file_end   = ph.p_vaddr + ph.p_filesz;

            if (page_max > file_start && page_min < file_end) {
                uint64_t copy_start = (page_min < file_start) ? file_start : page_min;
                uint64_t copy_end   = (page_max > file_end) ? file_end : page_max;
                size_t copy_len     = static_cast<size_t>(copy_end - copy_start);

                size_t page_offset  = static_cast<size_t>(copy_start - page_min);
                size_t file_offset  = static_cast<size_t>(ph.p_offset + (copy_start - file_start));

                llamaos::memcpy(frame_ptr + page_offset, elf_data + file_offset, copy_len);
            }
        }
    }

    // Allocate and map 16 KiB User Stack bounded by unmapped 4 KiB Guard Page
    for (size_t p = 0; p < USER_STACK_PAGES; ++p) {
        uint64_t stack_page_va = USER_STACK_BOTTOM_VA + (p * PAGE_SIZE);
        memory::VirtualAddress va(stack_page_va);

        memory::PhysicalAddress pa = memory::g_pmm.alloc_page();
        if (pa.is_null()) {
            res.status = ElfLoadStatus::AllocationFailed;
            return res;
        }

        uint8_t* frame_ptr = reinterpret_cast<uint8_t*>(memory::phys_to_virt(pa).as_ptr());
        llamaos::memset(frame_ptr, 0, PAGE_SIZE);

        memory::PageFlags stack_flags = memory::PageFlags::Present
                                      | memory::PageFlags::User
                                      | memory::PageFlags::Writable
                                      | memory::PageFlags::NoExecute;

        memory::VmmStatus vmm_stat = memory::g_vmm.map_page_in_table(target_pml4, va, pa, stack_flags);
        if (vmm_stat != memory::VmmStatus::Success) {
            res.status = ElfLoadStatus::MappingFailed;
            return res;
        }
        total_pages_mapped++;
    }

    // Guard page (USER_STACK_GUARD_VA) remains non-present to catch stack overflows

    res.status = ElfLoadStatus::Success;
    res.entry_point = memory::VirtualAddress(header->e_entry);
    res.stack_top = memory::VirtualAddress(USER_STACK_TOP_VA);
    res.pages_mapped = total_pages_mapped;

    klog_info("ELF64 Loader: Executable successfully loaded into Ring 3 user space:");
    klog_info("  Entry Point RIP : %p", res.entry_point.as_ptr());
    klog_info("  User Stack Top  : %p (16 KiB usable stack, 4 KiB guard page at %p)",
              res.stack_top.as_ptr(), reinterpret_cast<void*>(USER_STACK_GUARD_VA));
    klog_info("  Total User Pages: %u pages (%llu KiB mapped)",
              static_cast<uint32_t>(res.pages_mapped), (res.pages_mapped * 4096ULL) / 1024ULL);

    return res;
}

ElfLoadResult ElfLoader::load_from_vfs(const char* path, memory::PhysicalAddress pml4_pa) noexcept {
    ElfLoadResult res{};
    res.status = ElfLoadStatus::NullPointer;
    if (!path) return res;

    int fd = fs::Vfs::open(path, fs::O_RDONLY);
    if (fd < 0) {
        klog_error("ElfLoader: Cannot open file '%s' from VFS (err %d)", path, fd);
        res.status = ElfLoadStatus::BufferTooSmall;
        return res;
    }

    fs::FileStat st{};
    if (fs::Vfs::fstat(fd, &st) != 0 || st.size == 0) {
        fs::Vfs::close(fd);
        klog_error("ElfLoader: Failed to stat file '%s'", path);
        res.status = ElfLoadStatus::BufferTooSmall;
        return res;
    }

    void* buffer = memory::kmalloc(st.size);
    if (!buffer) {
        fs::Vfs::close(fd);
        klog_error("ElfLoader: Out of heap memory allocating %llu bytes for '%s'", st.size, path);
        res.status = ElfLoadStatus::AllocationFailed;
        return res;
    }

    int64_t bytes_read = fs::Vfs::read(fd, buffer, st.size);
    fs::Vfs::close(fd);

    if (bytes_read != static_cast<int64_t>(st.size)) {
        memory::kfree(buffer);
        klog_error("ElfLoader: Read error on '%s' (expected %llu, got %lld)", path, st.size, bytes_read);
        res.status = ElfLoadStatus::BufferTooSmall;
        return res;
    }

    klog_info("ElfLoader: Read %llu bytes from '%s' into kernel buffer, loading ELF...", st.size, path);
    res = load(reinterpret_cast<const uint8_t*>(buffer), static_cast<size_t>(st.size), pml4_pa);
    memory::kfree(buffer);
    return res;
}

} // namespace llamaos::userland
