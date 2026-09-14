#include "userland/elf_loader.hpp"
#include "userland/user_memory.hpp"
#include "arch/x86_64/cpu/gdt.hpp"
#include "memory/pmm.hpp"
#include "memory/vmm.hpp"
#include "syscall/syscall_types.hpp"
#include "syscall/syscall_abi.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstddef>

// =============================================================================
// LlamaOS/A - Phase 7 Minimal Userland Host Unit Regression Suite
// =============================================================================
// Deterministically verifies:
// 1. ELF64 header & program header layout, structure sizes, and alignment
// 2. ELF64 validation gates (magic, class, machine, bounds, overflows, security)
// 3. User address space boundaries, canonical checks, and null-page defense
// 4. User memory range validation and buffer parameter safety
// 5. User stack geometry, guard page math, and SysV 16-byte alignment
// 6. GDT User descriptor encodings (DPL 3, Long Mode, Data 0x28, Code 0x30)
// 7. System call ABI consistency and error code contracts
// =============================================================================

static size_t g_assertions_passed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "\n[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
            exit(1); \
        } \
        g_assertions_passed++; \
    } while (0)

using namespace llamaos;
using namespace llamaos::userland;
using namespace llamaos::arch::x86_64;
using namespace llamaos::syscall;

// Host logging stubs for unit tests
namespace llamaos {
void klog_info(const char* /*fmt*/, ...) {}
void klog_warn(const char* /*fmt*/, ...) {}
void klog_error(const char* /*fmt*/, ...) {}
}

namespace llamaos::memory {
PhysicalMemoryManager g_pmm;
VirtualMemoryManager g_vmm;

PhysicalAddress PhysicalMemoryManager::alloc_page() {
    return PhysicalAddress(0x1000000);
}

VmmStatus VirtualMemoryManager::map_page(VirtualAddress /*vaddr*/, PhysicalAddress /*paddr*/, PageFlags /*flags*/) {
    return VmmStatus::Success;
}

VmmStatus VirtualMemoryManager::map_page_in_table(PhysicalAddress /*pml4_pa*/, VirtualAddress /*vaddr*/, PhysicalAddress /*paddr*/, PageFlags /*flags*/) {
    return VmmStatus::Success;
}

PhysicalAddress VirtualMemoryManager::create_user_address_space() {
    return PhysicalAddress(0x2000000);
}

void VirtualMemoryManager::destroy_user_address_space(PhysicalAddress /*pml4_pa*/) {}

bool VirtualMemoryManager::translate(VirtualAddress vaddr, PhysicalAddress* out_paddr, PageFlags* out_flags) const {
    if (vaddr.value() >= 0x1000 && vaddr.value() < 0x7FFFFFFFFFFF) {
        if (out_paddr) *out_paddr = PhysicalAddress(vaddr.value());
        if (out_flags) *out_flags = PageFlags::Present | PageFlags::User | PageFlags::Writable;
        return true;
    }
    return false;
}

void* kmalloc(size_t) { return nullptr; }
void kfree(void*) {}
}

#include "fs/vfs.hpp"
namespace llamaos::fs {
int Vfs::open(const char*, unsigned int) { return -1; }
int Vfs::close(int) { return 0; }
int Vfs::fstat(int, FileStat*) { return -1; }
int64_t Vfs::read(int, void*, size_t) { return 0; }
}

// Helper to construct a synthetic valid ELF header
static void setup_valid_elf(Elf64Header& h, Elf64ProgramHeader& ph) {
    memset(&h, 0, sizeof(h));
    memset(&ph, 0, sizeof(ph));

    h.e_ident[EI_MAG0] = ELFMAG0;
    h.e_ident[EI_MAG1] = ELFMAG1;
    h.e_ident[EI_MAG2] = ELFMAG2;
    h.e_ident[EI_MAG3] = ELFMAG3;
    h.e_ident[EI_CLASS] = ELFCLASS64;
    h.e_ident[EI_DATA] = ELFDATA2LSB;
    h.e_ident[EI_VERSION] = EV_CURRENT;
    h.e_type = ET_EXEC;
    h.e_machine = EM_X86_64;
    h.e_version = EV_CURRENT;
    h.e_entry = 0x400000;
    h.e_phoff = sizeof(Elf64Header);
    h.e_ehsize = sizeof(Elf64Header);
    h.e_phentsize = sizeof(Elf64ProgramHeader);
    h.e_phnum = 1;

    ph.p_type = PT_LOAD;
    ph.p_flags = PF_R | PF_X;
    ph.p_offset = 0x1000;
    ph.p_vaddr = 0x400000;
    ph.p_paddr = 0x400000;
    ph.p_filesz = 0x200;
    ph.p_memsz = 0x200;
    ph.p_align = 0x1000;
}

// -----------------------------------------------------------------------------
// Gate 1: ELF64 Layout and Structure Invariants
// -----------------------------------------------------------------------------
static void test_elf_structures() {
    printf(" [RUN]  ELF64 Structures Layout & Alignment          ");

    TEST_ASSERT(sizeof(Elf64Header) == 64, "Elf64Header must be exactly 64 bytes");
    TEST_ASSERT(sizeof(Elf64ProgramHeader) == 56, "Elf64ProgramHeader must be exactly 56 bytes");

    TEST_ASSERT(offsetof(Elf64Header, e_ident) == 0, "e_ident offset mismatch");
    TEST_ASSERT(offsetof(Elf64Header, e_type) == 16, "e_type offset mismatch");
    TEST_ASSERT(offsetof(Elf64Header, e_machine) == 18, "e_machine offset mismatch");
    TEST_ASSERT(offsetof(Elf64Header, e_entry) == 24, "e_entry offset mismatch");
    TEST_ASSERT(offsetof(Elf64Header, e_phoff) == 32, "e_phoff offset mismatch");
    TEST_ASSERT(offsetof(Elf64Header, e_ehsize) == 52, "e_ehsize offset mismatch");
    TEST_ASSERT(offsetof(Elf64Header, e_phentsize) == 54, "e_phentsize offset mismatch");
    TEST_ASSERT(offsetof(Elf64Header, e_phnum) == 56, "e_phnum offset mismatch");

    TEST_ASSERT(offsetof(Elf64ProgramHeader, p_type) == 0, "p_type offset mismatch");
    TEST_ASSERT(offsetof(Elf64ProgramHeader, p_flags) == 4, "p_flags offset mismatch");
    TEST_ASSERT(offsetof(Elf64ProgramHeader, p_offset) == 8, "p_offset offset mismatch");
    TEST_ASSERT(offsetof(Elf64ProgramHeader, p_vaddr) == 16, "p_vaddr offset mismatch");
    TEST_ASSERT(offsetof(Elf64ProgramHeader, p_filesz) == 32, "p_filesz offset mismatch");
    TEST_ASSERT(offsetof(Elf64ProgramHeader, p_memsz) == 40, "p_memsz offset mismatch");
    TEST_ASSERT(offsetof(Elf64ProgramHeader, p_align) == 48, "p_align offset mismatch");

    printf("... PASSED\n");
}

// -----------------------------------------------------------------------------
// Gate 2: ELF64 Validation and Adversarial Rejection
// -----------------------------------------------------------------------------
static void test_elf_validation() {
    printf(" [RUN]  ELF64 Header & Segment Adversarial Rejection ");

    Elf64Header h;
    Elf64ProgramHeader ph;
    setup_valid_elf(h, ph);

    size_t file_size = 0x2000;
    uint8_t buffer[0x2000];
    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, &h, sizeof(h));
    memcpy(buffer + h.e_phoff, &ph, sizeof(ph));

    // 1. Valid ELF must succeed
    TEST_ASSERT(ElfLoader::validate_segments(&h, buffer, file_size) == ElfLoadStatus::Success,
                "Valid ELF must pass validation");

    // 2. Null pointer rejection
    TEST_ASSERT(ElfLoader::validate_header(nullptr, file_size) == ElfLoadStatus::NullPointer,
                "Null header must be rejected");

    // 3. Buffer too small
    TEST_ASSERT(ElfLoader::validate_header(&h, sizeof(Elf64Header) - 1) == ElfLoadStatus::BufferTooSmall,
                "Truncated buffer must be rejected");

    // 4. Invalid magic
    h.e_ident[EI_MAG0] = 0x00;
    TEST_ASSERT(ElfLoader::validate_header(&h, file_size) == ElfLoadStatus::InvalidMagic,
                "Invalid magic must be rejected");
    h.e_ident[EI_MAG0] = ELFMAG0;

    // 5. 32-bit ELF rejection
    h.e_ident[EI_CLASS] = 1; // ELFCLASS32
    TEST_ASSERT(ElfLoader::validate_header(&h, file_size) == ElfLoadStatus::InvalidClass,
                "32-bit ELF must be rejected");
    h.e_ident[EI_CLASS] = ELFCLASS64;

    // 6. Big-endian rejection
    h.e_ident[EI_DATA] = 2; // ELFDATA2MSB
    TEST_ASSERT(ElfLoader::validate_header(&h, file_size) == ElfLoadStatus::InvalidDataEncoding,
                "Big-endian ELF must be rejected");
    h.e_ident[EI_DATA] = ELFDATA2LSB;

    // 7. Non-x86_64 machine rejection (e.g. ARM64 0xB7)
    h.e_machine = 0xB7;
    TEST_ASSERT(ElfLoader::validate_header(&h, file_size) == ElfLoadStatus::InvalidMachine,
                "Non-x86_64 machine must be rejected");
    h.e_machine = EM_X86_64;

    // 8. Non-EXEC type rejection (e.g. ET_REL = 1)
    h.e_type = 1;
    TEST_ASSERT(ElfLoader::validate_header(&h, file_size) == ElfLoadStatus::InvalidType,
                "Relocatable ELF must be rejected");
    h.e_type = ET_EXEC;

    // 9. Program header table out of bounds
    h.e_phoff = file_size + 1;
    TEST_ASSERT(ElfLoader::validate_header(&h, file_size) == ElfLoadStatus::PhTableOutOfBounds,
                "OOB program header table must be rejected");
    h.e_phoff = sizeof(Elf64Header);

    // 10. Segment out of file bounds
    ph.p_offset = file_size;
    ph.p_filesz = 100;
    memcpy(buffer + h.e_phoff, &ph, sizeof(ph));
    TEST_ASSERT(ElfLoader::validate_segments(&h, buffer, file_size) == ElfLoadStatus::SegmentOutOfBounds,
                "Segment extending past file EOF must be rejected");
    ph.p_offset = 0x1000;
    ph.p_filesz = 0x200;

    // 11. Integer overflow: memsz < filesz
    ph.p_memsz = 0x100; // less than filesz (0x200)
    memcpy(buffer + h.e_phoff, &ph, sizeof(ph));
    TEST_ASSERT(ElfLoader::validate_segments(&h, buffer, file_size) == ElfLoadStatus::IntegerOverflow,
                "memsz < filesz must be rejected as integer anomaly");
    ph.p_memsz = 0x200;

    // 12. Kernel address overlap rejection (attempts to map into higher-half)
    ph.p_vaddr = 0xFFFFFFFF80100000ULL;
    memcpy(buffer + h.e_phoff, &ph, sizeof(ph));
    TEST_ASSERT(ElfLoader::validate_segments(&h, buffer, file_size) == ElfLoadStatus::KernelAddressOverlap,
                "Segment in kernel space must be rejected");
    ph.p_vaddr = 0x400000;

    // 13. Null-page overlap rejection (< 0x1000)
    ph.p_vaddr = 0x000;
    memcpy(buffer + h.e_phoff, &ph, sizeof(ph));
    TEST_ASSERT(ElfLoader::validate_segments(&h, buffer, file_size) == ElfLoadStatus::KernelAddressOverlap,
                "Segment in null-page guard must be rejected");
    ph.p_vaddr = 0x400000;

    // 14. Entry point outside executable segment
    h.e_entry = 0x500000; // Not inside 0x400000 .. 0x400200
    memcpy(buffer, &h, sizeof(h));
    ph.p_vaddr = 0x400000;
    memcpy(buffer + h.e_phoff, &ph, sizeof(ph));
    TEST_ASSERT(ElfLoader::validate_segments(&h, buffer, file_size) == ElfLoadStatus::EntryNotExecutable,
                "Entry point not inside executable segment must be rejected");
    h.e_entry = 0x400000;
    memcpy(buffer, &h, sizeof(h));

    // 15. Zero program headers (NoLoadableSegments)
    h.e_phnum = 0;
    TEST_ASSERT(ElfLoader::validate_header(&h, file_size) == ElfLoadStatus::NoLoadableSegments,
                "Zero program headers must be rejected");
    h.e_phnum = 1;

    // 16. Invalid ELF Header size
    h.e_ehsize = sizeof(Elf64Header) - 8;
    TEST_ASSERT(ElfLoader::validate_header(&h, file_size) == ElfLoadStatus::InvalidHeaderSize,
                "Invalid ELF header size must be rejected");
    h.e_ehsize = sizeof(Elf64Header);

    // 17. Invalid Program Header entry size
    h.e_phentsize = sizeof(Elf64ProgramHeader) - 4;
    TEST_ASSERT(ElfLoader::validate_header(&h, file_size) == ElfLoadStatus::InvalidPhSize,
                "Invalid program header entry size must be rejected");
    h.e_phentsize = sizeof(Elf64ProgramHeader);

    // 18. Integer overflow: p_vaddr + p_memsz wraparound
    ph.p_vaddr = 0xFFFFFFFFFFFFF000ULL;
    ph.p_memsz = 0x2000ULL;
    ph.p_filesz = 0x100ULL;
    memcpy(buffer + h.e_phoff, &ph, sizeof(ph));
    TEST_ASSERT(ElfLoader::validate_segments(&h, buffer, file_size) == ElfLoadStatus::IntegerOverflow,
                "Virtual address + memsz wraparound must be rejected");
    ph.p_vaddr = 0x400000;
    ph.p_memsz = 0x200;
    ph.p_filesz = 0x200;

    printf("... PASSED\n");
}

// -----------------------------------------------------------------------------
// Gate 3: User Space Addressing & Boundary Security
// -----------------------------------------------------------------------------
static void test_user_address_boundaries() {
    printf(" [RUN]  User Space Addressing & Boundary Math        ");

    TEST_ASSERT(UserMemoryValidator::USER_ADDR_MIN == 0x1000ULL, "Null page guard must be 4 KiB (0x1000)");
    TEST_ASSERT(UserMemoryValidator::USER_ADDR_MAX == 0x00007FFFFFFFFFFFULL, "User canonical limit must be 128 TiB");

    // Null page rejection
    TEST_ASSERT(!UserMemoryValidator::is_user_address(0x0000000000000000ULL), "0x0 must not be a user address");
    TEST_ASSERT(!UserMemoryValidator::is_user_address(0x0000000000000FFFULL), "0xFFF must not be a user address");
    TEST_ASSERT(UserMemoryValidator::is_user_address(0x0000000000001000ULL), "0x1000 must be a valid user address");

    // Lower-half user space bounds
    TEST_ASSERT(UserMemoryValidator::is_user_address(0x0000000000400000ULL), "0x400000 must be a valid user address");
    TEST_ASSERT(UserMemoryValidator::is_user_address(0x00007FFFFFFFE000ULL), "User stack top must be a valid user address");
    TEST_ASSERT(UserMemoryValidator::is_user_address(0x00007FFFFFFFFFFFULL), "Max user address must be valid");

    // Non-canonical hole rejection
    TEST_ASSERT(!UserMemoryValidator::is_user_address(0x0000800000000000ULL), "Non-canonical start must not be user address");
    TEST_ASSERT(!UserMemoryValidator::is_user_address(0x0000FFFFFFFFFFFFULL), "Non-canonical hole must not be user address");

    // Higher-half kernel space rejection
    TEST_ASSERT(!UserMemoryValidator::is_user_address(0xFFFF800000000000ULL), "Kernel base must not be user address");
    TEST_ASSERT(!UserMemoryValidator::is_user_address(0xFFFFFFFF80108000ULL), "Kernel text must not be user address");
    TEST_ASSERT(!UserMemoryValidator::is_user_address(0xFFFFFFFFFFFFFFFFULL), "Top of 64-bit space must not be user address");

    // User range validation
    TEST_ASSERT(UserMemoryValidator::is_user_range(0x400000, 1024), "Valid user range must pass");
    TEST_ASSERT(!UserMemoryValidator::is_user_range(0x400000, 0), "Zero-length range must fail");
    TEST_ASSERT(!UserMemoryValidator::is_user_range(0x0, 1024), "Range starting at 0x0 must fail");
    TEST_ASSERT(!UserMemoryValidator::is_user_range(0xFFFFFFFF80108000ULL, 100), "Kernel range must fail");

    // Integer overflow defense in range
    TEST_ASSERT(!UserMemoryValidator::is_user_range(0xFFFFFFFFFFFFFFFFULL, 10), "Overflowing start must fail");
    TEST_ASSERT(!UserMemoryValidator::is_user_range(0x00007FFFFFFFF000ULL, 0x2000), "Range crossing into non-canonical space must fail");

    // User memory buffer validator checks (9 distinct test cases)
    // 1. Valid 1-page buffer
    TEST_ASSERT(UserMemoryValidator::validate_user_buffer(reinterpret_cast<const void*>(0x400000), 4096, false),
                "Valid 1-page read buffer must pass");
    TEST_ASSERT(UserMemoryValidator::validate_user_buffer(reinterpret_cast<const void*>(0x400000), 4096, true),
                "Valid 1-page write buffer must pass");

    // 2. Valid multi-page buffer
    TEST_ASSERT(UserMemoryValidator::validate_user_buffer(reinterpret_cast<const void*>(0x400000), 16384, false),
                "Valid multi-page buffer must pass");

    // 3. Null pointer buffer
    TEST_ASSERT(!UserMemoryValidator::validate_user_buffer(nullptr, 4096, false),
                "Null pointer buffer must fail");

    // 4. Kernel pointer buffer
    TEST_ASSERT(!UserMemoryValidator::validate_user_buffer(reinterpret_cast<const void*>(0xFFFFFFFF80108000ULL), 64, false),
                "Kernel space buffer pointer must fail");

    // 5. Unmapped page buffer
    TEST_ASSERT(!UserMemoryValidator::validate_user_buffer(reinterpret_cast<const void*>(0x0), 4096, false),
                "Unmapped page at 0x0 must fail");
    TEST_ASSERT(!UserMemoryValidator::validate_user_buffer(reinterpret_cast<const void*>(0x500), 64, false),
                "Unmapped null-page sub-range must fail");

    // 6. Buffer crossing valid -> invalid boundary
    TEST_ASSERT(!UserMemoryValidator::validate_user_buffer(reinterpret_cast<const void*>(0x00007FFFFFFFFFF0ULL), 32, false),
                "Buffer crossing into non-canonical space must fail");

    // 7. Wraparound / integer overflow
    TEST_ASSERT(!UserMemoryValidator::validate_user_buffer(reinterpret_cast<const void*>(0xFFFFFFFFFFFFFFFFULL), 32, false),
                "Wraparound buffer must fail");

    // 8. Zero-length buffer
    TEST_ASSERT(!UserMemoryValidator::validate_user_buffer(reinterpret_cast<const void*>(0x400000), 0, false),
                "Zero-length buffer must fail");

    // 9. Maximum-length boundary (> 16 MiB)
    TEST_ASSERT(!UserMemoryValidator::validate_user_buffer(reinterpret_cast<const void*>(0x400000), UserMemoryValidator::MAX_USER_BUFFER_SIZE + 1, false),
                "Buffer exceeding maximum allowed size must fail");

    // User string validation checks
    TEST_ASSERT(!UserMemoryValidator::validate_user_string(nullptr, 100), "Null user string must fail");
    TEST_ASSERT(!UserMemoryValidator::validate_user_string("test", 0), "Zero max_len string must fail");
    TEST_ASSERT(!UserMemoryValidator::validate_user_string(reinterpret_cast<const char*>(0xFFFFFFFF80108000ULL), 100),
                "Kernel space string pointer must fail");

    printf("... PASSED\n");
}

// -----------------------------------------------------------------------------
// Gate 4: User Stack Geometry & Guard Page Math
// -----------------------------------------------------------------------------
static void test_user_stack_geometry() {
    printf(" [RUN]  User Stack Geometry & Guard Page Math        ");

    TEST_ASSERT(USER_STACK_TOP_VA == 0x00007FFFFFFFE000ULL, "User stack top must be 0x00007FFFFFFFE000");
    TEST_ASSERT((USER_STACK_TOP_VA % 16) == 0, "User stack top must be 16-byte aligned");
    TEST_ASSERT((USER_STACK_TOP_VA % 4096) == 0, "User stack top must be page aligned");

    TEST_ASSERT(USER_STACK_PAGES == 4, "User stack must have 4 usable pages (16 KiB)");
    TEST_ASSERT(USER_STACK_SIZE_BYTES == 16384, "User stack must be 16384 bytes");

    TEST_ASSERT(USER_STACK_BOTTOM_VA == USER_STACK_TOP_VA - 16384, "Stack bottom math mismatch");
    TEST_ASSERT(USER_STACK_GUARD_VA == USER_STACK_BOTTOM_VA - 4096, "Guard page must directly precede stack bottom");

    TEST_ASSERT(USER_STACK_GUARD_VA < USER_STACK_BOTTOM_VA, "Guard page must reside below stack");
    TEST_ASSERT(UserMemoryValidator::is_user_address(USER_STACK_BOTTOM_VA), "Stack bottom must be in user space");
    TEST_ASSERT(UserMemoryValidator::is_user_address(USER_STACK_GUARD_VA), "Guard page address must be in user space");

    printf("... PASSED\n");
}

// -----------------------------------------------------------------------------
// Gate 5: GDT User Selectors & Descriptors
// -----------------------------------------------------------------------------
static void test_gdt_user_descriptors() {
    printf(" [RUN]  GDT User Selectors & Descriptor Encoding     ");

    // Selector constant checks
    TEST_ASSERT(Selector::UserData == 0x28, "UserData selector base must be 0x28 (Slot 5)");
    TEST_ASSERT(Selector::UserCode == 0x30, "UserCode selector base must be 0x30 (Slot 6)");

    // RPL=3 selector values
    uint16_t rpl3_data = Selector::UserData | 0x03;
    uint16_t rpl3_code = Selector::UserCode | 0x03;
    TEST_ASSERT(rpl3_data == 0x002B, "User Data selector with RPL 3 must be 0x002B");
    TEST_ASSERT(rpl3_code == 0x0033, "User Code selector with RPL 3 must be 0x0033");

    // User Data descriptor encoding: DPL=3
    GdtEntry udata = encode_data_descriptor(3);
    uint8_t d_access = (udata.raw >> 40) & 0xFF;
    uint8_t d_flags  = (udata.raw >> 52) & 0x0F;
    TEST_ASSERT((d_access & 0x80) != 0, "User data P bit must be 1");
    TEST_ASSERT(((d_access >> 5) & 0x03) == 3, "User data DPL must be 3");
    TEST_ASSERT((d_access & 0x10) != 0, "User data S bit must be 1 (non-system)");
    TEST_ASSERT((d_access & 0x08) == 0, "User data executable bit must be 0");
    TEST_ASSERT((d_access & 0x02) != 0, "User data writable bit must be 1");
    TEST_ASSERT((d_flags & 0x08) != 0, "User data granularity bit must be 1");

    // User Code descriptor encoding: DPL=3, L=1 (64-bit)
    GdtEntry ucode = encode_code_descriptor(3);
    uint8_t c_access = (ucode.raw >> 40) & 0xFF;
    uint8_t c_flags  = (ucode.raw >> 52) & 0x0F;
    TEST_ASSERT((c_access & 0x80) != 0, "User code P bit must be 1");
    TEST_ASSERT(((c_access >> 5) & 0x03) == 3, "User code DPL must be 3");
    TEST_ASSERT((c_access & 0x10) != 0, "User code S bit must be 1 (non-system)");
    TEST_ASSERT((c_access & 0x08) != 0, "User code executable bit must be 1");
    TEST_ASSERT((c_access & 0x02) != 0, "User code readable bit must be 1");
    TEST_ASSERT((c_flags & 0x02) != 0, "User code L bit must be 1 (64-bit Long Mode)");
    TEST_ASSERT((c_flags & 0x04) == 0, "User code D/B bit must be 0 in 64-bit");

    // SYSRETQ compatibility: STAR[63:48] + 8 == 0x28, STAR[63:48] + 16 == 0x30
    uint16_t star_user_base = static_cast<uint16_t>((SYSCALL_STAR_VALUE >> 48) & 0xFFFFULL);
    TEST_ASSERT(star_user_base == 0x0020, "STAR user base must be 0x0020");
    TEST_ASSERT((star_user_base + 8) == Selector::UserData, "SYSRETQ SS target must match UserData selector");
    TEST_ASSERT((star_user_base + 16) == Selector::UserCode, "SYSRETQ CS target must match UserCode selector");

    printf("... PASSED\n");
}

// -----------------------------------------------------------------------------
// Gate 6: Syscall ABI Expansion & Error Codes
// -----------------------------------------------------------------------------
static void test_syscall_abi_phase7() {
    printf(" [RUN]  Syscall ABI Expansion & Error Codes          ");

    TEST_ASSERT(SysExit == 5, "SysExit syscall number must be 5");
    TEST_ASSERT(SysMaxPhase7 == 5, "Phase 7 max syscall must be 5");

    TEST_ASSERT(SYS_SUCCESS == 0, "SYS_SUCCESS must be 0");
    TEST_ASSERT(SYS_ERR_NOSYS == -1, "SYS_ERR_NOSYS must be -1");
    TEST_ASSERT(SYS_ERR_INVAL == -2, "SYS_ERR_INVAL must be -2");
    TEST_ASSERT(SYS_ERR_FAULT == -3, "SYS_ERR_FAULT must be -3");
    TEST_ASSERT(SYS_ERR_PERM == -4, "SYS_ERR_PERM must be -4");

    printf("... PASSED\n");
}

int main() {
    printf("================================================================================\n");
    printf(" LlamaOS/A - Phase 7 Minimal Userland Host Unit Regression Suite\n");
    printf("================================================================================\n");

    test_elf_structures();
    test_elf_validation();
    test_user_address_boundaries();
    test_user_stack_geometry();
    test_gdt_user_descriptors();
    test_syscall_abi_phase7();

    printf("================================================================================\n");
    printf(" Phase 7 Regression Suite Complete: ALL tests PASSED (%zu assertions verified)\n", g_assertions_passed);
    printf("================================================================================\n");
    return 0;
}
