#include "drivers/ps2/scancode.hpp"
#include "drivers/ps2/input_queue.hpp"
#include "drivers/ps2/input_event.hpp"
#include "drivers/pci/pci.hpp"
#include "drivers/framebuffer/framebuffer.hpp"
#include "drivers/devices/device_registry.hpp"
#include "drivers/ps2/ps2_controller.hpp"
#include "drivers/ps2/keyboard.hpp"
#include "memory/vmm.hpp"
#include "memory/memory_layout.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>

// Host mock definitions for freestanding kernel symbols
namespace llamaos {
    void klog_info(const char*, ...) {}
    void klog_warn(const char*, ...) {}
    void klog_error(const char*, ...) {}

    namespace memory {
        VirtualMemoryManager g_vmm;
        VmmStatus VirtualMemoryManager::map_page(VirtualAddress, PhysicalAddress, PageFlags) { return VmmStatus::Success; }
        VmmStatus VirtualMemoryManager::unmap_page(VirtualAddress) { return VmmStatus::Success; }
    }

    namespace drivers {
        bool Ps2Controller::s_initialized{false};
        bool Keyboard::s_initialized{false};
    }
}

static size_t g_assertions_passed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "\n[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
            return false; \
        } \
        g_assertions_passed++; \
    } while (0)

using namespace llamaos;
using namespace llamaos::drivers;

// =============================================================================
// 1. Scancode Decoder Tests
// =============================================================================
static bool test_scancode_decoder_make_break() {
    ScancodeDecoder decoder;
    KeyEvent ev{};

    // Press 'A' (Set 1 scancode 0x1E)
    TEST_ASSERT(decoder.process_byte(0x1E, ev) == true, "0x1E should produce event");
    TEST_ASSERT(ev.key == KeyCode::A, "Event code must be KeyCode::A");
    TEST_ASSERT(ev.action == KeyAction::Press, "Event action must be Press");
    TEST_ASSERT(key_event_to_ascii(ev) == 'a', "ASCII must be lowercase 'a'");

    // Release 'A' (Set 1 scancode 0x9E)
    TEST_ASSERT(decoder.process_byte(0x9E, ev) == true, "0x9E should produce event");
    TEST_ASSERT(ev.key == KeyCode::A, "Event code must be KeyCode::A");
    TEST_ASSERT(ev.action == KeyAction::Release, "Event action must be Release");

    // Press '1' (0x02) and 'Space' (0x39)
    TEST_ASSERT(decoder.process_byte(0x02, ev) == true, "0x02 should produce event");
    TEST_ASSERT(ev.key == KeyCode::Num1, "Event code must be KeyCode::Num1");
    TEST_ASSERT(key_event_to_ascii(ev) == '1', "ASCII must be '1'");

    TEST_ASSERT(decoder.process_byte(0x39, ev) == true, "0x39 should produce event");
    TEST_ASSERT(ev.key == KeyCode::Space, "Event code must be KeyCode::Space");
    TEST_ASSERT(key_event_to_ascii(ev) == ' ', "ASCII must be ' '");

    return true;
}

static bool test_scancode_decoder_modifiers() {
    ScancodeDecoder decoder;
    KeyEvent ev{};

    // Press Left Shift (0x2A)
    TEST_ASSERT(decoder.process_byte(0x2A, ev) == true, "0x2A should produce event");
    TEST_ASSERT(ev.key == KeyCode::LeftShift, "Code must be LeftShift");
    TEST_ASSERT(decoder.current_modifiers().shift == true, "Shift modifier must be active");

    // Press 'A' (0x1E) with Shift active -> 'A'
    TEST_ASSERT(decoder.process_byte(0x1E, ev) == true, "0x1E with Shift");
    TEST_ASSERT(ev.key == KeyCode::A, "Code must be A");
    TEST_ASSERT(ev.modifiers.shift == true, "Event must record shift modifier");
    TEST_ASSERT(key_event_to_ascii(ev) == 'A', "ASCII with shift must be uppercase 'A'");

    // Release Left Shift (0xAA)
    TEST_ASSERT(decoder.process_byte(0xAA, ev) == true, "0xAA should produce event");
    TEST_ASSERT(decoder.current_modifiers().shift == false, "Shift modifier must be inactive");

    // Press 'A' again -> 'a'
    TEST_ASSERT(decoder.process_byte(0x1E, ev) == true, "0x1E without Shift");
    TEST_ASSERT(key_event_to_ascii(ev) == 'a', "ASCII must be lowercase 'a'");

    // Test Caps Lock toggle (0x3A)
    TEST_ASSERT(decoder.process_byte(0x3A, ev) == true, "0x3A CapsLock make");
    TEST_ASSERT(decoder.current_modifiers().caps == true, "CapsLock must be on");

    TEST_ASSERT(decoder.process_byte(0x1E, ev) == true, "0x1E with CapsLock");
    TEST_ASSERT(key_event_to_ascii(ev) == 'A', "CapsLock makes 'a' -> 'A'");

    // Press Shift while CapsLock is ON -> inverts to lowercase 'a'
    decoder.process_byte(0x2A, ev); // Shift make
    decoder.process_byte(0x1E, ev); // 'a'
    TEST_ASSERT(key_event_to_ascii(ev) == 'a', "CapsLock + Shift produces lowercase 'a'");
    decoder.process_byte(0xAA, ev); // Shift break

    // Toggle CapsLock OFF
    decoder.process_byte(0xBA, ev); // CapsLock break
    decoder.process_byte(0x3A, ev); // CapsLock make (second toggle)
    TEST_ASSERT(decoder.current_modifiers().caps == false, "CapsLock must toggle off");

    // Test Left Ctrl (0x1D / 0x9D) and Left Alt (0x38 / 0xB8)
    decoder.process_byte(0x1D, ev);
    TEST_ASSERT(decoder.current_modifiers().ctrl == true, "Ctrl must be true");
    decoder.process_byte(0x9D, ev);
    TEST_ASSERT(decoder.current_modifiers().ctrl == false, "Ctrl must be false");

    decoder.process_byte(0x38, ev);
    TEST_ASSERT(decoder.current_modifiers().alt == true, "Alt must be true");
    decoder.process_byte(0xB8, ev);
    TEST_ASSERT(decoder.current_modifiers().alt == false, "Alt must be false");

    return true;
}

static bool test_scancode_decoder_extended() {
    ScancodeDecoder decoder;
    KeyEvent ev{};

    // Extended Up Arrow: 0xE0 0x48
    TEST_ASSERT(decoder.process_byte(0xE0, ev) == false, "0xE0 prefix should not emit event immediately");
    TEST_ASSERT(decoder.process_byte(0x48, ev) == true, "0x48 following 0xE0 should emit event");
    TEST_ASSERT(ev.key == KeyCode::ArrowUp, "Code must be ArrowUp");
    TEST_ASSERT(ev.action == KeyAction::Press, "Action must be Press");

    // Extended Up Arrow Release: 0xE0 0xC8
    TEST_ASSERT(decoder.process_byte(0xE0, ev) == false, "0xE0 prefix");
    TEST_ASSERT(decoder.process_byte(0xC8, ev) == true, "0xC8 following 0xE0");
    TEST_ASSERT(ev.key == KeyCode::ArrowUp, "Code must be ArrowUp");
    TEST_ASSERT(ev.action == KeyAction::Release, "Action must be Release");

    // Extended Right Ctrl: 0xE0 0x1D
    decoder.process_byte(0xE0, ev);
    decoder.process_byte(0x1D, ev);
    TEST_ASSERT(ev.key == KeyCode::RightCtrl, "Code must be RightCtrl");
    TEST_ASSERT(decoder.current_modifiers().ctrl == true, "Ctrl modifier active");

    // Extended Right Ctrl Release: 0xE0 0x9D
    decoder.process_byte(0xE0, ev);
    decoder.process_byte(0x9D, ev);
    TEST_ASSERT(ev.key == KeyCode::RightCtrl, "Code must be RightCtrl");
    TEST_ASSERT(decoder.current_modifiers().ctrl == false, "Ctrl modifier inactive");

    return true;
}

// =============================================================================
// 2. Input Event Queue Tests (SPSC Concurrency & Wraparound Validation)
// =============================================================================
static bool test_input_queue() {
    InputEventQueue<4> queue;
    KeyEvent ev{};

    // Empty state verification
    TEST_ASSERT(queue.is_empty() == true, "New queue must be empty");
    TEST_ASSERT(queue.is_full() == false, "New queue must not be full");
    TEST_ASSERT(queue.count() == 0, "Count must be 0");
    TEST_ASSERT(queue.dropped_count() == 0, "Dropped count must be 0");
    TEST_ASSERT(queue.pop(&ev) == false, "Pop on empty queue must return false");

    // One slot free test (Capacity = 4): push 3 items
    KeyEvent e1{.key = KeyCode::A, .action = KeyAction::Press};
    KeyEvent e2{.key = KeyCode::B, .action = KeyAction::Press};
    KeyEvent e3{.key = KeyCode::C, .action = KeyAction::Press};
    KeyEvent e4{.key = KeyCode::D, .action = KeyAction::Press};

    TEST_ASSERT(queue.push(e1) == true, "Push e1");
    TEST_ASSERT(queue.push(e2) == true, "Push e2");
    TEST_ASSERT(queue.push(e3) == true, "Push e3");
    TEST_ASSERT(queue.count() == 3, "Queue count must be 3");
    TEST_ASSERT(queue.is_full() == false, "Queue with 3/4 items must not be full (one slot free)");
    TEST_ASSERT(queue.is_empty() == false, "Queue with 3 items must not be empty");

    // Push 4th item -> reaches exact capacity
    TEST_ASSERT(queue.push(e4) == true, "Push e4");
    TEST_ASSERT(queue.count() == 4, "Queue count must be 4");
    TEST_ASSERT(queue.is_full() == true, "Queue must be full at capacity 4");

    // Overflow & dropped event accounting: push 5th, 6th, 7th items
    KeyEvent e5{.key = KeyCode::E, .action = KeyAction::Press};
    KeyEvent e6{.key = KeyCode::F, .action = KeyAction::Press};
    KeyEvent e7{.key = KeyCode::G, .action = KeyAction::Press};

    TEST_ASSERT(queue.push(e5) == false, "Push e5 on full queue must fail");
    TEST_ASSERT(queue.dropped_count() == 1, "Dropped count must increment to 1");
    TEST_ASSERT(queue.push(e6) == false, "Push e6 on full queue must fail");
    TEST_ASSERT(queue.dropped_count() == 2, "Dropped count must increment to 2");
    TEST_ASSERT(queue.push(e7) == false, "Push e7 on full queue must fail");
    TEST_ASSERT(queue.dropped_count() == 3, "Dropped count must increment to 3");

    // Pop in FIFO order: verify original items preserved without corruption
    TEST_ASSERT(queue.pop(&ev) == true, "Pop e1");
    TEST_ASSERT(ev.key == KeyCode::A, "First popped must be A");

    TEST_ASSERT(queue.pop(&ev) == true, "Pop e2");
    TEST_ASSERT(ev.key == KeyCode::B, "Second popped must be B");

    TEST_ASSERT(queue.pop(&ev) == true, "Pop e3");
    TEST_ASSERT(ev.key == KeyCode::C, "Third popped must be C");

    TEST_ASSERT(queue.pop(&ev) == true, "Pop e4");
    TEST_ASSERT(ev.key == KeyCode::D, "Fourth popped must be D");

    TEST_ASSERT(queue.is_empty() == true, "Queue must be empty now");
    TEST_ASSERT(queue.pop(&ev) == false, "Pop on emptied queue must fail");
    TEST_ASSERT(queue.dropped_count() == 3, "Dropped count preserved after pop");

    // Reset / clear verification
    queue.clear();
    TEST_ASSERT(queue.count() == 0, "Count after clear must be 0");
    TEST_ASSERT(queue.dropped_count() == 0, "Dropped count after clear must be 0");
    TEST_ASSERT(queue.is_empty() == true, "is_empty after clear must be true");

    // Alternating Producer / Consumer: 1000 consecutive push/pop operations
    for (size_t i = 0; i < 1000; ++i) {
        KeyEvent item{.key = static_cast<KeyCode>(static_cast<uint8_t>(KeyCode::A) + (i % 26)), .action = KeyAction::Press};
        TEST_ASSERT(queue.push(item) == true, "Alternating push must succeed");
        TEST_ASSERT(queue.count() == 1, "Alternating count must be 1");
        TEST_ASSERT(queue.is_empty() == false, "Alternating queue not empty");

        KeyEvent popped{};
        TEST_ASSERT(queue.pop(&popped) == true, "Alternating pop must succeed");
        TEST_ASSERT(popped.key == item.key, "Popped item key must match pushed item");
        TEST_ASSERT(queue.count() == 0, "Alternating count after pop must be 0");
        TEST_ASSERT(queue.is_empty() == true, "Alternating queue must be empty after pop");
    }

    // Monotonic wrap-around across 100 full fill/drain cycles
    for (size_t cycle = 0; cycle < 100; ++cycle) {
        TEST_ASSERT(queue.is_empty() == true, "Cycle start must be empty");
        for (uint8_t i = 0; i < 4; ++i) {
            KeyEvent e{.key = static_cast<KeyCode>(static_cast<uint8_t>(KeyCode::A) + i), .action = KeyAction::Press};
            TEST_ASSERT(queue.push(e) == true, "Push in cycle");
        }
        TEST_ASSERT(queue.is_full() == true, "Cycle must be full");
        TEST_ASSERT(queue.count() == 4, "Cycle count must be 4");

        for (uint8_t i = 0; i < 4; ++i) {
            TEST_ASSERT(queue.pop(&ev) == true, "Pop in cycle");
            TEST_ASSERT(ev.key == static_cast<KeyCode>(static_cast<uint8_t>(KeyCode::A) + i), "Popped key must match FIFO order");
        }
        TEST_ASSERT(queue.is_empty() == true, "Cycle end must be empty");
    }

    return true;
}

// =============================================================================
// 3. PCI Configuration Space & BAR Decoder Tests
// =============================================================================
static bool test_pci_primitives() {
    // Test make_config_address
    uint32_t addr0 = PciManager::make_config_address(0, 0, 0, 0);
    TEST_ASSERT(addr0 == 0x80000000, "Bus 0, Dev 0, Func 0, Offset 0 must be 0x80000000");

    uint32_t addr1 = PciManager::make_config_address(1, 2, 3, 0x14);
    uint32_t expected1 = (1U << 31) | (1U << 16) | (2U << 11) | (3U << 8) | 0x14;
    TEST_ASSERT(addr1 == expected1, "Config address construction must match PCI spec");

    // Address construction across bus boundary (bus 255)
    uint32_t addr255 = PciManager::make_config_address(255, 31, 7, 0xFC);
    uint32_t expected255 = (1U << 31) | (255U << 16) | (31U << 11) | (7U << 8) | 0xFC;
    TEST_ASSERT(addr255 == expected255, "Bus 255 address construction must match");

    // Offset alignment mask
    uint32_t addr_unaligned = PciManager::make_config_address(0, 0, 0, 0x17);
    TEST_ASSERT((addr_unaligned & 0x03) == 0, "Config address offset must be 4-byte aligned (bits 0..1 == 0)");

    // Test decode_bar - I/O Space BAR
    PciBar io_bar = PciManager::decode_bar(0x0000C001);
    TEST_ASSERT(io_bar.valid == true, "I/O BAR must be valid");
    TEST_ASSERT(io_bar.is_io == true, "I/O BAR is_io must be true");
    TEST_ASSERT(io_bar.is_64bit == false, "I/O BAR cannot be 64-bit");
    TEST_ASSERT(io_bar.base_address == 0xC000, "I/O base address must mask out lower 2 bits");

    // Test decode_bar - I/O BAR with high DWORD supplied (must still be 32-bit I/O)
    PciBar io_bar_high = PciManager::decode_bar(0x0000C001, 0x12345678);
    TEST_ASSERT(io_bar_high.valid == true, "I/O BAR with high DWORD must be valid");
    TEST_ASSERT(io_bar_high.is_io == true, "I/O BAR must remain I/O");
    TEST_ASSERT(io_bar_high.is_64bit == false, "I/O BAR cannot become 64-bit");
    TEST_ASSERT(io_bar_high.base_address == 0xC000, "I/O base address ignores high DWORD");

    // Test decode_bar - 32-bit Memory BAR
    PciBar mem32_bar = PciManager::decode_bar(0xFD000000);
    TEST_ASSERT(mem32_bar.valid == true, "Mem32 BAR must be valid");
    TEST_ASSERT(mem32_bar.is_io == false, "Mem32 BAR is_io must be false");
    TEST_ASSERT(mem32_bar.is_64bit == false, "Mem32 BAR is_64bit must be false");
    TEST_ASSERT(mem32_bar.is_prefetchable == false, "Mem32 BAR prefetchable must be false");
    TEST_ASSERT(mem32_bar.base_address == 0xFD000000, "Mem32 address must match");

    // Test decode_bar - 64-bit Prefetchable Memory BAR
    PciBar mem64_bar = PciManager::decode_bar(0xE000000C, 0x00000002);
    TEST_ASSERT(mem64_bar.valid == true, "Mem64 BAR must be valid");
    TEST_ASSERT(mem64_bar.is_io == false, "Mem64 BAR is_io must be false");
    TEST_ASSERT(mem64_bar.is_64bit == true, "Mem64 BAR is_64bit must be true");
    TEST_ASSERT(mem64_bar.is_prefetchable == true, "Mem64 BAR prefetchable must be true (bit 3 == 1)");
    TEST_ASSERT(mem64_bar.base_address == 0x00000002E0000000ULL, "Mem64 address must combine high and low");

    // Test decode_bar - 64-bit Non-Prefetchable Memory BAR
    PciBar mem64_nonpref = PciManager::decode_bar(0x80000004, 0x00000001);
    TEST_ASSERT(mem64_nonpref.valid == true, "Mem64 non-prefetchable must be valid");
    TEST_ASSERT(mem64_nonpref.is_io == false, "Mem64 non-prefetchable is_io must be false");
    TEST_ASSERT(mem64_nonpref.is_64bit == true, "Mem64 non-prefetchable must be 64-bit");
    TEST_ASSERT(mem64_nonpref.is_prefetchable == false, "Mem64 bit 3 is 0 (non-prefetchable)");
    TEST_ASSERT(mem64_nonpref.base_address == 0x0000000180000000ULL, "Mem64 high+low match");

    // Test decode_bar - 64-bit Memory BAR above 4 GiB with low address bits zero
    PciBar mem64_highonly = PciManager::decode_bar(0x00000004, 0x00000004);
    TEST_ASSERT(mem64_highonly.valid == true, "Mem64 above 4 GiB must be valid");
    TEST_ASSERT(mem64_highonly.is_64bit == true, "Mem64 flag active");
    TEST_ASSERT(mem64_highonly.base_address == 0x0000000400000000ULL, "Mem64 base address 16 GiB");

    // Test decode_bar - 64-bit Memory BAR with all zeros
    PciBar mem64_zero = PciManager::decode_bar(0x00000004, 0x00000000);
    TEST_ASSERT(mem64_zero.valid == false, "Mem64 with 0 address is invalid/unmapped");

    // Test decode_bar - Unpopulated BAR
    PciBar empty_bar0 = PciManager::decode_bar(0x00000000);
    TEST_ASSERT(empty_bar0.valid == false, "0x00000000 BAR must be invalid");
    PciBar empty_barF = PciManager::decode_bar(0xFFFFFFFF);
    TEST_ASSERT(empty_barF.valid == false, "0xFFFFFFFF BAR must be invalid");

    // Test format_class
    const char* vga_cls = PciManager::format_class(0x03, 0x00);
    TEST_ASSERT(strstr(vga_cls, "VGA") != nullptr, "Class 0x03:0x00 must mention VGA");

    const char* bridge_cls = PciManager::format_class(0x06, 0x00);
    TEST_ASSERT(strcmp(bridge_cls, "Host Bridge") == 0, "Class 0x06:0x00 must be Host Bridge");

    return true;
}

// =============================================================================
// 4. Framebuffer Metadata Validation Tests
// =============================================================================
static bool test_framebuffer_validation() {
    uint64_t total_size = 0;

    // Valid standard 1024x768x32
    TEST_ASSERT(Framebuffer::validate_metadata(0xFD000000, 4096, 1024, 768, 32, 1, &total_size) == true,
                "Standard 1024x768x32 must be valid");
    TEST_ASSERT(total_size == 4096ULL * 768, "Total size must equal pitch * height");

    // Valid standard 800x600x24
    TEST_ASSERT(Framebuffer::validate_metadata(0xE0000000, 2400, 800, 600, 24, 1, &total_size) == true,
                "Standard 800x600x24 must be valid");

    // Valid standard 640x480x16
    TEST_ASSERT(Framebuffer::validate_metadata(0xE0000000, 1280, 640, 480, 16, 1, &total_size) == true,
                "Standard 640x480x16 must be valid");

    // Valid large dimensions: 4K (3840x2160x32)
    TEST_ASSERT(Framebuffer::validate_metadata(0x80000000, 15360, 3840, 2160, 32, 1, &total_size) == true,
                "4K UHD 3840x2160x32 must be valid");
    TEST_ASSERT(total_size == 15360ULL * 2160, "4K total size correct");

    // Valid large dimensions: 8K (7680x4320x32)
    TEST_ASSERT(Framebuffer::validate_metadata(0x80000000, 30720, 7680, 4320, 32, 1, &total_size) == true,
                "8K UHD 7680x4320x32 must be valid");
    TEST_ASSERT(total_size == 30720ULL * 4320, "8K total size correct");

    // Valid: Pitch greater than width * bytes_per_pixel (padding)
    TEST_ASSERT(Framebuffer::validate_metadata(0xFD000000, 4096, 1024, 768, 24, 1, &total_size) == true,
                "Padded pitch (4096 > 3072) must be valid");

    // Valid: Unaligned physical base address
    TEST_ASSERT(Framebuffer::validate_metadata(0xFD000800, 4096, 1024, 768, 32, 1, &total_size) == true,
                "Unaligned physical base address (offset 0x800) must validate");
    TEST_ASSERT(total_size == 4096ULL * 768, "Total size correct for unaligned base");

    // Invalid: Zero address
    TEST_ASSERT(Framebuffer::validate_metadata(0, 4096, 1024, 768, 32, 1) == false, "Zero addr must fail");

    // Invalid: Dimensions out of range
    TEST_ASSERT(Framebuffer::validate_metadata(0xFD000000, 4096, 0, 768, 32, 1) == false, "Zero width must fail");
    TEST_ASSERT(Framebuffer::validate_metadata(0xFD000000, 4096, 1024, 0, 32, 1) == false, "Zero height must fail");
    TEST_ASSERT(Framebuffer::validate_metadata(0xFD000000, 40960, 7681, 768, 32, 1) == false, "Excessive width > 7680 must fail");
    TEST_ASSERT(Framebuffer::validate_metadata(0xFD000000, 40960, 1024, 4321, 32, 1) == false, "Excessive height > 4320 must fail");

    // Invalid: Zero pitch
    TEST_ASSERT(Framebuffer::validate_metadata(0xFD000000, 0, 1024, 768, 32, 1) == false, "Zero pitch must fail");

    // Invalid: Unsupported BPP
    TEST_ASSERT(Framebuffer::validate_metadata(0xFD000000, 128, 1024, 768, 1, 1) == false, "1bpp must fail");
    TEST_ASSERT(Framebuffer::validate_metadata(0xFD000000, 512, 1024, 768, 4, 1) == false, "4bpp must fail");
    TEST_ASSERT(Framebuffer::validate_metadata(0xFD000000, 1024, 1024, 768, 8, 1) == false, "8bpp must fail");
    TEST_ASSERT(Framebuffer::validate_metadata(0xFD000000, 2048, 1024, 768, 15, 1) == false, "15bpp must fail");

    // Invalid: Unsupported Type (!= 1 Direct RGB)
    TEST_ASSERT(Framebuffer::validate_metadata(0xFD000000, 4096, 1024, 768, 32, 0) == false, "Indexed mode must fail");
    TEST_ASSERT(Framebuffer::validate_metadata(0xFD000000, 4096, 1024, 768, 32, 2) == false, "EGA text must fail");

    // Invalid: Pitch smaller than width * bytes_per_pixel
    TEST_ASSERT(Framebuffer::validate_metadata(0xFD000000, 1000, 1024, 768, 32, 1) == false, "Insufficient pitch must fail");

    // Invalid: Overflow protection (Addr + Size wrapping 64-bit boundary)
    TEST_ASSERT(Framebuffer::validate_metadata(0xFFFFFFFFFFFFFF00ULL, 4096, 1024, 768, 32, 1) == false,
                "Addr + Size wrapping 64-bit boundary must fail");

    // Invalid: pitch * height overflow
    TEST_ASSERT(Framebuffer::validate_metadata(0x1000, 0xFFFFFFFF, 1024, 0xFFFFFFFF, 32, 1) == false,
                "Pitch * height overflow must fail");

    return true;
}

// =============================================================================
// 5. Framebuffer Mapping Plan Tests (Silent Truncation Elimination)
// =============================================================================
static bool test_framebuffer_mapping_plan() {
    // 1. Aligned base in direct map (0 .. 2 GiB)
    {
        uint64_t paddr = 0x01000000; // 16 MiB
        uint64_t size = 64 * 1024;   // 64 KiB = 16 pages
        auto plan = Framebuffer::calculate_mapping_plan(paddr, size);
        TEST_ASSERT(plan.valid == true, "Direct map aligned must be valid");
        TEST_ASSERT(plan.uses_direct_map == true, "Should use direct map");
        TEST_ASSERT(plan.aligned_paddr == 0x01000000, "aligned_paddr matches");
        TEST_ASSERT(plan.page_offset == 0, "page_offset is 0");
        TEST_ASSERT(plan.total_mapped_bytes == size, "total_mapped_bytes matches");
        TEST_ASSERT(plan.num_pages == 16, "num_pages matches 16");
        TEST_ASSERT(plan.base_vaddr == llamaos::phys_to_virt(static_cast<uintptr_t>(0x01000000)), "base_vaddr is direct map");
        TEST_ASSERT(plan.framebuffer_vaddr == plan.base_vaddr, "framebuffer_vaddr matches base_vaddr");
    }

    // 2. Unaligned base in direct map
    {
        uint64_t paddr = 0x01000800; // 16 MiB + 2048
        uint64_t size = 4096;        // spans 2 pages
        auto plan = Framebuffer::calculate_mapping_plan(paddr, size);
        TEST_ASSERT(plan.valid == true, "Direct map unaligned must be valid");
        TEST_ASSERT(plan.uses_direct_map == true, "Should use direct map");
        TEST_ASSERT(plan.aligned_paddr == 0x01000000, "aligned_paddr page rounded");
        TEST_ASSERT(plan.page_offset == 0x800, "page_offset is 0x800");
        TEST_ASSERT(plan.total_mapped_bytes == 0x800 + 4096, "total_mapped_bytes includes page offset");
        TEST_ASSERT(plan.num_pages == 2, "num_pages is 2");
        TEST_ASSERT(plan.framebuffer_vaddr == plan.base_vaddr + 0x800, "framebuffer_vaddr preserves offset");
    }

    // 3. Exact page size in MMIO window (paddr >= 2 GiB)
    {
        uint64_t paddr = 0xE0000000; // 3.5 GiB (PCI BAR)
        uint64_t size = 4096;
        auto plan = Framebuffer::calculate_mapping_plan(paddr, size);
        TEST_ASSERT(plan.valid == true, "MMIO exact page must be valid");
        TEST_ASSERT(plan.uses_direct_map == false, "Must use MMIO window");
        TEST_ASSERT(plan.aligned_paddr == 0xE0000000, "aligned_paddr matches");
        TEST_ASSERT(plan.page_offset == 0, "page_offset is 0");
        TEST_ASSERT(plan.num_pages == 1, "num_pages is 1");
        TEST_ASSERT(plan.base_vaddr == memory::layout::MMIO_WINDOW_START.value(), "base_vaddr is MMIO start");
        TEST_ASSERT(plan.framebuffer_vaddr == plan.base_vaddr, "framebuffer_vaddr matches base");
    }

    // 4. One byte over page size in MMIO window
    {
        uint64_t paddr = 0xE0000000;
        uint64_t size = 4097;
        auto plan = Framebuffer::calculate_mapping_plan(paddr, size);
        TEST_ASSERT(plan.valid == true, "MMIO 4097 bytes must be valid");
        TEST_ASSERT(plan.num_pages == 2, "4097 bytes must map to 2 pages");
    }

    // 5. Standard multi-page framebuffer (1024x768x32 = 3,145,728 bytes = 768 pages)
    {
        uint64_t paddr = 0xFD000000;
        uint64_t size = 1024ULL * 768 * 4;
        auto plan = Framebuffer::calculate_mapping_plan(paddr, size);
        TEST_ASSERT(plan.valid == true, "Standard 1024x768x32 MMIO must be valid");
        TEST_ASSERT(plan.num_pages == 768, "1024x768x32 must be exactly 768 pages");
        TEST_ASSERT(plan.base_vaddr == memory::layout::MMIO_WINDOW_START.value(), "base_vaddr correct");
        TEST_ASSERT(plan.framebuffer_vaddr == plan.base_vaddr, "framebuffer_vaddr correct");
    }

    // 6. Unaligned multi-page framebuffer in MMIO window
    {
        uint64_t paddr = 0xFD000800;
        uint64_t size = 1024ULL * 768 * 4; // 3,145,728 bytes
        auto plan = Framebuffer::calculate_mapping_plan(paddr, size);
        TEST_ASSERT(plan.valid == true, "Unaligned multi-page MMIO must be valid");
        TEST_ASSERT(plan.aligned_paddr == 0xFD000000, "aligned_paddr page rounded down");
        TEST_ASSERT(plan.page_offset == 0x800, "page_offset is 0x800");
        TEST_ASSERT(plan.total_mapped_bytes == 0x800 + size, "total_mapped_bytes includes offset");
        TEST_ASSERT(plan.num_pages == 769, "num_pages is 769 due to page-offset crossing");
        TEST_ASSERT(plan.framebuffer_vaddr == plan.base_vaddr + 0x800, "framebuffer_vaddr preserves page_offset");
    }

    // 7. Framebuffer exactly fitting MMIO window limit (512 MiB = 536,870,912 bytes = 131,072 pages)
    {
        uint64_t paddr = 0x80000000;
        uint64_t size = memory::layout::MMIO_WINDOW_SIZE; // 512 MiB
        auto plan = Framebuffer::calculate_mapping_plan(paddr, size);
        TEST_ASSERT(plan.valid == true, "512 MiB exactly fitting MMIO window must be valid");
        TEST_ASSERT(plan.num_pages == memory::layout::MMIO_WINDOW_MAX_PAGES, "num_pages matches max pages (131072)");
        TEST_ASSERT(plan.total_mapped_bytes == memory::layout::MMIO_WINDOW_SIZE, "total_mapped_bytes matches limit");
    }

    // 8. Silent truncation prevention: Framebuffer exceeding MMIO window limit by 1 byte MUST FAIL
    {
        uint64_t paddr = 0x80000000;
        uint64_t size = memory::layout::MMIO_WINDOW_SIZE + 1; // 512 MiB + 1
        auto plan = Framebuffer::calculate_mapping_plan(paddr, size);
        TEST_ASSERT(plan.valid == false, "Framebuffer exceeding MMIO window size by 1 byte must be rejected");
    }

    // 9. Silent truncation prevention: Framebuffer at limit with unaligned base MUST FAIL
    {
        uint64_t paddr = 0x80000800; // offset 0x800
        uint64_t size = memory::layout::MMIO_WINDOW_SIZE; // 512 MiB
        auto plan = Framebuffer::calculate_mapping_plan(paddr, size);
        TEST_ASSERT(plan.valid == false, "Unaligned base + 512 MiB exceeds window capacity and must be rejected");
    }

    // 10. Framebuffer huge size (1 GiB) MUST FAIL
    {
        uint64_t paddr = 0x80000000;
        uint64_t size = 1024ULL * 1024 * 1024; // 1 GiB
        auto plan = Framebuffer::calculate_mapping_plan(paddr, size);
        TEST_ASSERT(plan.valid == false, "1 GiB framebuffer exceeds 512 MiB window and must be rejected");
    }

    // 11. 64-bit integer wraparound (paddr + size > UINT64_MAX)
    {
        uint64_t paddr = 0xFFFFFFFFFFFFF000ULL;
        uint64_t size = 0x2000;
        auto plan = Framebuffer::calculate_mapping_plan(paddr, size);
        TEST_ASSERT(plan.valid == false, "paddr + size wrapping 64-bit space must be rejected");
    }

    // 12. Zero address or zero size
    {
        auto plan_zero_addr = Framebuffer::calculate_mapping_plan(0, 4096);
        TEST_ASSERT(plan_zero_addr.valid == false, "Zero paddr must be rejected");

        auto plan_zero_size = Framebuffer::calculate_mapping_plan(0xFD000000, 0);
        TEST_ASSERT(plan_zero_size.valid == false, "Zero size must be rejected");
    }

    return true;
}

// =============================================================================
// 6. Framebuffer Pixel Bounds & Checked Arithmetic Tests
// =============================================================================
static bool test_framebuffer_pixel_access() {
    const uint32_t width = 1024;
    const uint32_t height = 768;
    const uint8_t bpp = 32;
    const uint32_t pitch = 4096;
    const uint64_t total_size = 4096ULL * 768;

    uint64_t offset = 0;

    // 1. Pixel (0, 0)
    TEST_ASSERT(Framebuffer::is_pixel_in_bounds(0, 0, bpp, pitch, width, height, total_size, &offset) == true,
                "Origin pixel (0, 0) must be in bounds");
    TEST_ASSERT(offset == 0, "Origin offset must be 0");

    // 2. Last valid pixel (1023, 767)
    TEST_ASSERT(Framebuffer::is_pixel_in_bounds(1023, 767, bpp, pitch, width, height, total_size, &offset) == true,
                "Bottom-right pixel (1023, 767) must be in bounds");
    // Expected offset: 767 * 4096 + 1023 * 4 = 3141632 + 4092 = 3145724.
    // 3145724 + 4 = 3145728 == total_size
    TEST_ASSERT(offset == 3145724ULL, "Bottom-right pixel offset matches 3145724");

    // 3. Out of bounds X coordinate (x == width)
    TEST_ASSERT(Framebuffer::is_pixel_in_bounds(1024, 767, bpp, pitch, width, height, total_size, &offset) == false,
                "x == width must be out of bounds");

    // 4. Out of bounds Y coordinate (y == height)
    TEST_ASSERT(Framebuffer::is_pixel_in_bounds(1023, 768, bpp, pitch, width, height, total_size, &offset) == false,
                "y == height must be out of bounds");

    // 5. Huge coordinates (UINT32_MAX)
    TEST_ASSERT(Framebuffer::is_pixel_in_bounds(UINT32_MAX, 0, bpp, pitch, width, height, total_size, &offset) == false,
                "x = UINT32_MAX must be out of bounds");
    TEST_ASSERT(Framebuffer::is_pixel_in_bounds(0, UINT32_MAX, bpp, pitch, width, height, total_size, &offset) == false,
                "y = UINT32_MAX must be out of bounds");

    // 6. Truncated total_size (e.g. 1 byte less than required for last pixel)
    TEST_ASSERT(Framebuffer::is_pixel_in_bounds(1023, 767, bpp, pitch, width, height, total_size - 1, &offset) == false,
                "Truncated total_size must reject access to last pixel");

    // 7. 24bpp format: 800x600, pitch = 2400, total_size = 2400 * 600 = 1440000
    {
        uint64_t off24 = 0;
        TEST_ASSERT(Framebuffer::is_pixel_in_bounds(799, 599, 24, 2400, 800, 600, 1440000, &off24) == true,
                    "24bpp last pixel must be in bounds");
        TEST_ASSERT(off24 == 1439997ULL, "24bpp offset matches 1439997");
    }

    // 8. 16bpp format: 640x480, pitch = 1280, total_size = 1280 * 480 = 614400
    {
        uint64_t off16 = 0;
        TEST_ASSERT(Framebuffer::is_pixel_in_bounds(639, 479, 16, 1280, 640, 480, 614400, &off16) == true,
                    "16bpp last pixel must be in bounds");
        TEST_ASSERT(off16 == 614398ULL, "16bpp offset matches 614398");
    }

    // 9. Unsupported bpp (e.g. 8bpp or 0bpp)
    TEST_ASSERT(Framebuffer::is_pixel_in_bounds(0, 0, 8, 1024, 1024, 768, 1024 * 768, &offset) == false,
                "8bpp must be rejected");
    TEST_ASSERT(Framebuffer::is_pixel_in_bounds(0, 0, 0, 1024, 1024, 768, 1024 * 768, &offset) == false,
                "0bpp must be rejected");

    return true;
}

// =============================================================================
// 7. Device Registry Tests
// =============================================================================
static bool test_device_registry() {
    DeviceRegistry::init();
    TEST_ASSERT(DeviceRegistry::device_count() == 0, "Init registry must have 0 devices");

    DeviceInfo d1{
        .name = "Test Keyboard",
        .type = DeviceType::Keyboard,
        .state = DeviceState::Active,
        .driver_name = "Ps2Keyboard",
        .resource_addr = 0x60,
        .irq = 1
    };

    TEST_ASSERT(DeviceRegistry::register_device(d1) == true, "Register d1");
    TEST_ASSERT(DeviceRegistry::device_count() == 1, "Count must be 1");

    const DeviceInfo* retrieved = DeviceRegistry::get_device(0);
    TEST_ASSERT(retrieved != nullptr, "Retrieved device must not be null");
    TEST_ASSERT(strcmp(retrieved->name, "Test Keyboard") == 0, "Name must match");
    TEST_ASSERT(retrieved->type == DeviceType::Keyboard, "Type must match");
    TEST_ASSERT(retrieved->irq == 1, "IRQ must match");

    const DeviceInfo* found = DeviceRegistry::find_by_name("Test Keyboard");
    TEST_ASSERT(found != nullptr, "find_by_name must locate device");
    TEST_ASSERT(DeviceRegistry::find_by_name("NonExistent") == nullptr, "find_by_name must return null for non-existent");

    // String helpers
    TEST_ASSERT(strcmp(DeviceRegistry::type_to_string(DeviceType::Keyboard), "Keyboard") == 0, "type_to_string Keyboard");
    TEST_ASSERT(strcmp(DeviceRegistry::state_to_string(DeviceState::Active), "Active") == 0, "state_to_string Active");

    return true;
}

// =============================================================================
// Main Runner
// =============================================================================
int main() {
    printf("============================================================\n");
    printf(" LlamaOS/A - Phase 4 Device & Hardware Host Test Suite\n");
    printf("============================================================\n");

    bool all_passed = true;

    all_passed &= test_scancode_decoder_make_break();
    all_passed &= test_scancode_decoder_modifiers();
    all_passed &= test_scancode_decoder_extended();
    all_passed &= test_input_queue();
    all_passed &= test_pci_primitives();
    all_passed &= test_framebuffer_validation();
    all_passed &= test_framebuffer_mapping_plan();
    all_passed &= test_framebuffer_pixel_access();
    all_passed &= test_device_registry();

    printf("\nTotal Assertions Passed: %zu\n", g_assertions_passed);

    if (all_passed) {
        printf("[SUCCESS] All Phase 4 Device & Hardware tests passed perfectly!\n");
        return 0;
    } else {
        printf("[FAILURE] Some Phase 4 tests failed!\n");
        return 1;
    }
}
