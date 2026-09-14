#include "idt.hpp"
#include "gdt.hpp"
#include "interrupts.hpp"
#include "core/kprint.hpp"
#include "memory/vmm.hpp"

// =============================================================================
// LlamaOS/A - Interrupt Descriptor Table (IDT) Implementation
// =============================================================================

extern "C" {
    extern uint8_t _text_start[];
    extern uint8_t _text_end[];
}

namespace llamaos::arch::x86_64 {

alignas(4096) IdtEntry IdtManager::s_idt[256]{};

void IdtManager::install_gate(size_t vector, uint64_t handler, GateType type, uint8_t dpl, uint8_t ist) {
    if (vector >= 256) return;
    s_idt[vector] = encode_idt_gate(handler, Selector::KernelCode, type, dpl, ist);
}

void IdtManager::init() {
    // Zero out all 256 gates
    for (size_t i = 0; i < 256; ++i) {
        s_idt[i] = IdtEntry{};
    }

    // Install CPU Exception Gates (Vectors 0 .. 21)
    install_gate(0,  reinterpret_cast<uint64_t>(isr_stub_0),  GateType::InterruptGate, 0, 0); // #DE
    install_gate(1,  reinterpret_cast<uint64_t>(isr_stub_1),  GateType::InterruptGate, 0, 0); // #DB
    install_gate(2,  reinterpret_cast<uint64_t>(isr_stub_2),  GateType::InterruptGate, 0, 3); // #NMI (IST3)
    install_gate(3,  reinterpret_cast<uint64_t>(isr_stub_3),  GateType::TrapGate,      0, 0); // #BP (Trap Gate)
    install_gate(4,  reinterpret_cast<uint64_t>(isr_stub_4),  GateType::TrapGate,      0, 0); // #OF
    install_gate(5,  reinterpret_cast<uint64_t>(isr_stub_5),  GateType::InterruptGate, 0, 0); // #BR
    install_gate(6,  reinterpret_cast<uint64_t>(isr_stub_6),  GateType::InterruptGate, 0, 0); // #UD
    install_gate(7,  reinterpret_cast<uint64_t>(isr_stub_7),  GateType::InterruptGate, 0, 0); // #NM
    install_gate(8,  reinterpret_cast<uint64_t>(isr_stub_8),  GateType::InterruptGate, 0, 1); // #DF (IST1 Dedicated Stack)
    install_gate(10, reinterpret_cast<uint64_t>(isr_stub_10), GateType::InterruptGate, 0, 0); // #TS
    install_gate(11, reinterpret_cast<uint64_t>(isr_stub_11), GateType::InterruptGate, 0, 0); // #NP
    install_gate(12, reinterpret_cast<uint64_t>(isr_stub_12), GateType::InterruptGate, 0, 0); // #SS
    install_gate(13, reinterpret_cast<uint64_t>(isr_stub_13), GateType::InterruptGate, 0, 0); // #GP
    install_gate(14, reinterpret_cast<uint64_t>(isr_stub_14), GateType::InterruptGate, 0, 2); // #PF (IST2 Dedicated Stack)
    install_gate(16, reinterpret_cast<uint64_t>(isr_stub_16), GateType::InterruptGate, 0, 0); // #MF
    install_gate(17, reinterpret_cast<uint64_t>(isr_stub_17), GateType::InterruptGate, 0, 0); // #AC
    install_gate(18, reinterpret_cast<uint64_t>(isr_stub_18), GateType::InterruptGate, 0, 3); // #MC (IST3 Dedicated Stack)
    install_gate(19, reinterpret_cast<uint64_t>(isr_stub_19), GateType::InterruptGate, 0, 0); // #XM
    install_gate(20, reinterpret_cast<uint64_t>(isr_stub_20), GateType::InterruptGate, 0, 0); // #VE
    install_gate(21, reinterpret_cast<uint64_t>(isr_stub_21), GateType::InterruptGate, 0, 0); // #CP

    // Install Hardware IRQ and Spurious Gates
    install_gate(32,  reinterpret_cast<uint64_t>(isr_stub_32),  GateType::InterruptGate, 0, 0); // IRQ0 Timer
    install_gate(33,  reinterpret_cast<uint64_t>(isr_stub_33),  GateType::InterruptGate, 0, 0); // IRQ1 Keyboard
    install_gate(39,  reinterpret_cast<uint64_t>(isr_stub_39),  GateType::InterruptGate, 0, 0); // IRQ7 Spurious Master
    install_gate(47,  reinterpret_cast<uint64_t>(isr_stub_47),  GateType::InterruptGate, 0, 0); // IRQ15 Spurious Slave
    install_gate(255, reinterpret_cast<uint64_t>(isr_stub_255), GateType::InterruptGate, 0, 0); // Spurious APIC

    // Load IDTR via LIDT
    Idtr idtr{};
    idtr.limit = sizeof(s_idt) - 1;
    idtr.base = reinterpret_cast<uint64_t>(s_idt);
    lidt(idtr);

    klog_info("Interrupt Descriptor Table (IDT) loaded successfully:");
    klog_info("  IDTR Limit    : 0x%04x (size: %u bytes, 256 gates)", idtr.limit, static_cast<uint32_t>(sizeof(s_idt)));
    klog_info("  IDTR Base     : %p", idtr.base);
    klog_info("  Configured Gates: 25 active vectors (#DE..#CP, IRQ0/1, IRQ7/15, APIC spurious)");
}

bool IdtManager::verify() {
    Idtr current_idtr{};
    sidt(current_idtr);

    uint64_t expected_base = reinterpret_cast<uint64_t>(s_idt);
    uint16_t expected_limit = sizeof(s_idt) - 1;

    if (current_idtr.base != expected_base || current_idtr.limit != expected_limit) {
        klog_error("IDT Verify Failed: IDTR mismatch (base: %p != %p, limit: %x != %x)!",
                   current_idtr.base, expected_base, current_idtr.limit, expected_limit);
        return false;
    }

    uintptr_t text_start = reinterpret_cast<uintptr_t>(_text_start);
    uintptr_t text_end   = reinterpret_cast<uintptr_t>(_text_end);

    // Validate active gates
    const size_t active_vectors[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 16, 17, 18, 19, 20, 21,
        32, 33, 39, 47, 255
    };

    for (size_t vec : active_vectors) {
        const auto& gate = s_idt[vec];
        if (gate.present != 1) {
            klog_error("IDT Verify Failed: Active vector %u is marked not present!", static_cast<uint32_t>(vec));
            return false;
        }
        if (gate.selector != Selector::KernelCode) {
            klog_error("IDT Verify Failed: Active vector %u has invalid selector 0x%04x!",
                       static_cast<uint32_t>(vec), gate.selector);
            return false;
        }
        uint64_t handler = gate.handler_address();
        if (handler < text_start || handler >= text_end) {
            klog_error("IDT Verify Failed: Vector %u handler %p outside .text [%p - %p]!",
                       static_cast<uint32_t>(vec), handler, text_start, text_end);
            return false;
        }
    }

    // Validate dedicated IST assignments
    if (s_idt[8].ist != 1) { // #DF
        klog_error("IDT Verify Failed: Double Fault (#DF) gate IST index (%u) != 1!", s_idt[8].ist);
        return false;
    }
    if (s_idt[14].ist != 2) { // #PF
        klog_error("IDT Verify Failed: Page Fault (#PF) gate IST index (%u) != 2!", s_idt[14].ist);
        return false;
    }
    if (s_idt[2].ist != 3) { // #NMI
        klog_error("IDT Verify Failed: NMI gate IST index (%u) != 3!", s_idt[2].ist);
        return false;
    }
    if (s_idt[18].ist != 3) { // #MC
        klog_error("IDT Verify Failed: Machine Check gate IST index (%u) != 3!", s_idt[18].ist);
        return false;
    }

    // Validate that uninstalled vectors are not marked present
    const size_t unused_vectors[] = { 9, 15, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 50, 100 };
    for (size_t vec : unused_vectors) {
        if (s_idt[vec].present != 0) {
            klog_error("IDT Verify Failed: Unused vector %u is unexpectedly marked present!", static_cast<uint32_t>(vec));
            return false;
        }
    }

    // Verify IDT virtual mapping in active VMM
    memory::PhysicalAddress pa;
    memory::PageFlags flags;
    if (!memory::g_vmm.translate(memory::VirtualAddress(expected_base), &pa, &flags)) {
        klog_error("IDT Verify Failed: IDT address %p is not mapped in VMM!", expected_base);
        return false;
    }
    if (!memory::test_flag(flags, memory::PageFlags::Present) ||
        !memory::test_flag(flags, memory::PageFlags::Writable) ||
        !memory::test_flag(flags, memory::PageFlags::NoExecute) ||
        memory::test_flag(flags, memory::PageFlags::User)) {
        klog_error("IDT Verify Failed: IDT page flags invalid (must be RW NX Kernel)!");
        return false;
    }

    return true;
}

} // namespace llamaos::arch::x86_64
