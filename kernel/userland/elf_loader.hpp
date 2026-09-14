#pragma once

#include "core/types.hpp"
#include "memory/memory_types.hpp"
#include "memory/memory_layout.hpp"

// =============================================================================
// LlamaOS/A - Freestanding Minimal ELF64 Executable Loader
// =============================================================================
// Parses, validates, and loads 64-bit ELF executables into user address space:
// - Validates ELF64 magic, machine (x86-64), type (ET_EXEC), and program headers
// - Enforces safe bounds, checked alignment, and prevents integer overflows
// - Allocates physical pages via PMM and maps into VMM with User permissions
// - Enforces W^X: PT_LOAD segments mapped with exact RX, R, or RW permissions
// - Allocates 16 KiB User Stack bounded by a 4 KiB unmapped guard page
// =============================================================================

namespace llamaos::userland {

// Standard ELF Ident indices
inline constexpr size_t EI_MAG0       = 0;
inline constexpr size_t EI_MAG1       = 1;
inline constexpr size_t EI_MAG2       = 2;
inline constexpr size_t EI_MAG3       = 3;
inline constexpr size_t EI_CLASS      = 4;
inline constexpr size_t EI_DATA       = 5;
inline constexpr size_t EI_VERSION    = 6;
inline constexpr size_t EI_OSABI      = 7;
inline constexpr size_t EI_ABIVERSION = 8;
inline constexpr size_t EI_NIDENT     = 16;

inline constexpr uint8_t ELFMAG0 = 0x7F;
inline constexpr uint8_t ELFMAG1 = 'E';
inline constexpr uint8_t ELFMAG2 = 'L';
inline constexpr uint8_t ELFMAG3 = 'F';

inline constexpr uint8_t ELFCLASS64 = 2;
inline constexpr uint8_t ELFDATA2LSB = 1; // 2's complement little endian
inline constexpr uint8_t EV_CURRENT = 1;

// ELF types
inline constexpr uint16_t ET_EXEC = 2;
inline constexpr uint16_t EM_X86_64 = 0x3E; // 62

// Program header types
inline constexpr uint32_t PT_NULL = 0;
inline constexpr uint32_t PT_LOAD = 1;

// Program header flags
inline constexpr uint32_t PF_X = 1;
inline constexpr uint32_t PF_W = 2;
inline constexpr uint32_t PF_R = 4;

// User stack configuration
inline constexpr uint64_t USER_STACK_TOP_VA    = 0x00007FFFFFFFE000ULL;
inline constexpr size_t   USER_STACK_PAGES     = 4; // 16 KiB usable stack
inline constexpr uint64_t USER_STACK_SIZE_BYTES= USER_STACK_PAGES * 4096ULL;
inline constexpr uint64_t USER_STACK_BOTTOM_VA = USER_STACK_TOP_VA - USER_STACK_SIZE_BYTES;
inline constexpr uint64_t USER_STACK_GUARD_VA  = USER_STACK_BOTTOM_VA - 4096ULL;

// Standard 64-byte ELF64 Header
struct [[gnu::packed]] Elf64Header {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};
static_assert(sizeof(Elf64Header) == 64, "Elf64Header must be 64 bytes");

// Standard 56-byte ELF64 Program Header
struct [[gnu::packed]] Elf64ProgramHeader {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};
static_assert(sizeof(Elf64ProgramHeader) == 56, "Elf64ProgramHeader must be 56 bytes");

enum class ElfLoadStatus {
    Success,
    NullPointer,
    BufferTooSmall,
    InvalidMagic,
    InvalidClass,
    InvalidDataEncoding,
    InvalidMachine,
    InvalidType,
    InvalidVersion,
    InvalidHeaderSize,
    InvalidPhSize,
    PhTableOutOfBounds,
    SegmentOutOfBounds,
    IntegerOverflow,
    KernelAddressOverlap,
    NonCanonicalAddress,
    NoLoadableSegments,
    EntryNotExecutable,
    AllocationFailed,
    MappingFailed
};

const char* to_string(ElfLoadStatus status) noexcept;

struct ElfLoadResult {
    ElfLoadStatus          status{ElfLoadStatus::NullPointer};
    memory::VirtualAddress entry_point{0};
    memory::VirtualAddress stack_top{0};
    size_t                 pages_mapped{0};
};

class ElfLoader {
public:
    // Standalone validation of ELF header
    [[nodiscard]] static ElfLoadStatus validate_header(const Elf64Header* header, size_t file_size) noexcept;

    // Standalone validation of program header table and segments
    [[nodiscard]] static ElfLoadStatus validate_segments(const Elf64Header* header, const uint8_t* file_data, size_t file_size) noexcept;

    // End-to-end ELF loader
    [[nodiscard]] static ElfLoadResult load(const uint8_t* elf_data, size_t file_size, memory::PhysicalAddress pml4_pa = memory::PhysicalAddress(0)) noexcept;

    // Loads an ELF64 binary directly from the Virtual Filesystem (VFS)
    [[nodiscard]] static ElfLoadResult load_from_vfs(const char* path, memory::PhysicalAddress pml4_pa = memory::PhysicalAddress(0)) noexcept;
};

} // namespace llamaos::userland
