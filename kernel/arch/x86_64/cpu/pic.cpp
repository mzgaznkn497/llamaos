#include "pic.hpp"
#include "io.hpp"
#include "cpu.hpp"
#include "core/kprint.hpp"

// =============================================================================
// LlamaOS/A - Dual 8259 PIC & APIC Foundation Implementation
// =============================================================================

namespace llamaos::arch::x86_64 {

bool PicManager::s_initialized{false};
bool PicManager::s_apic_supported{false};
bool PicManager::s_apic_enabled{false};
uint64_t PicManager::s_apic_base_paddr{0};

volatile uint64_t PicManager::s_spurious_irq7{0};
volatile uint64_t PicManager::s_spurious_irq15{0};

void PicManager::init() {
    // Step 1: Detect CPUID APIC capability and MSR status
    s_apic_supported = g_cpu_info.features.apic;
    if (s_apic_supported) {
        // Read IA32_APIC_BASE MSR (0x1B)
        uint64_t apic_base_msr = read_msr(0x1B);
        s_apic_enabled = (apic_base_msr & (1ULL << 11)) != 0;
        s_apic_base_paddr = apic_base_msr & 0x000FFFFFFFFFF000ULL;
        klog_info("APIC Hardware Reconnaissance: Present=%b, GlobalEnable=%b, Base=%p",
                  s_apic_supported, s_apic_enabled, s_apic_base_paddr);
    } else {
        klog_info("APIC Hardware Reconnaissance: CPUID APIC feature not present.");
    }

    // Step 2: Establish controlled 8259 PIC initialization path
    klog_info("Remapping Dual 8259 PIC (Master: IRQ0..7 -> 0x20..0x27, Slave: IRQ8..15 -> 0x28..0x2F)...");

    // ICW1: Start initialization in cascade mode
    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();

    // ICW2: Master PIC vector offset (0x20 / 32)
    outb(PIC1_DATA, MASTER_OFFSET);
    io_wait();
    // ICW2: Slave PIC vector offset (0x28 / 40)
    outb(PIC2_DATA, SLAVE_OFFSET);
    io_wait();

    // ICW3: Tell Master PIC that Slave PIC is connected to IRQ2 (pin 2 = bit 2 = 0x04)
    outb(PIC1_DATA, 0x04);
    io_wait();
    // ICW3: Tell Slave PIC its cascade identity (2)
    outb(PIC2_DATA, 0x02);
    io_wait();

    // ICW4: Set 8086/88 mode
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    // Step 3: Initially mask all 16 IRQ lines to prevent unexpected interrupts
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    s_initialized = true;
    klog_info("Dual 8259 PIC successfully remapped and initialized (all IRQ lines masked).");
}

bool PicManager::verify() {
    if (!s_initialized) {
        klog_error("PIC Verify Failed: PIC subsystem not initialized!");
        return false;
    }

    // Verify PIC data register reads are responsive
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    // Initial mask must have high bits set
    if (mask1 == 0x00 && mask2 == 0x00) {
        klog_error("PIC Verify Failed: PIC mask registers read all zeros unexpectedly!");
        return false;
    }

    klog_info("PIC Hardware Verified: MasterMask=0x%02x, SlaveMask=0x%02x, SpuriousIRQ7=%llu, SpuriousIRQ15=%llu",
              mask1, mask2, s_spurious_irq7, s_spurious_irq15);
    return true;
}

void PicManager::send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

void PicManager::mask_irq(uint8_t irq) {
    uint16_t port;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    uint8_t val = inb(port) | static_cast<uint8_t>(1 << irq);
    outb(port, val);
}

void PicManager::unmask_irq(uint8_t irq) {
    uint16_t port;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    uint8_t val = inb(port) & static_cast<uint8_t>(~(1 << irq));
    outb(port, val);
}

uint16_t PicManager::read_isr() {
    outb(PIC1_COMMAND, 0x0B);
    outb(PIC2_COMMAND, 0x0B);
    return static_cast<uint16_t>((inb(PIC2_COMMAND) << 8) | inb(PIC1_COMMAND));
}

uint16_t PicManager::read_irr() {
    outb(PIC1_COMMAND, 0x0A);
    outb(PIC2_COMMAND, 0x0A);
    return static_cast<uint16_t>((inb(PIC2_COMMAND) << 8) | inb(PIC1_COMMAND));
}

void handle_pic_spurious(uint8_t irq) {
    if (irq == 7) {
        outb(PicManager::PIC1_COMMAND, 0x0B); // Read ISR
        uint8_t isr = inb(PicManager::PIC1_COMMAND);
        if ((isr & 0x80) == 0) {
            // Spurious IRQ7: Do NOT send EOI to PIC
            PicManager::record_spurious_irq7();
            klog_warn("Spurious IRQ7 detected from Master PIC (Ignored without EOI).");
            return;
        }
        // Genuine IRQ7: Send EOI
        PicManager::send_eoi(7);
    } else if (irq == 15) {
        outb(PicManager::PIC2_COMMAND, 0x0B); // Read Slave ISR
        uint8_t isr = inb(PicManager::PIC2_COMMAND);
        if ((isr & 0x80) == 0) {
            // Spurious IRQ15: Send EOI only to Master PIC
            PicManager::record_spurious_irq15();
            klog_warn("Spurious IRQ15 detected from Slave PIC (Master EOI acknowledged).");
            outb(PicManager::PIC1_COMMAND, PicManager::PIC_EOI);
            return;
        }
        // Genuine IRQ15: Send EOI to both
        PicManager::send_eoi(15);
    }
}

} // namespace llamaos::arch::x86_64
