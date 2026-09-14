#include "drivers/pci/pci.hpp"
#include "arch/x86_64/cpu/io.hpp"

namespace llamaos::drivers {

PciDevice PciManager::s_devices[MAX_DEVICES]{};
size_t    PciManager::s_device_count{0};
bool      PciManager::s_initialized{false};

uint32_t PciManager::read_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) noexcept {
    uint32_t address = make_config_address(bus, dev, func, offset);
    arch::x86_64::outl(CONFIG_ADDRESS_PORT, address);
    return arch::x86_64::inl(CONFIG_DATA_PORT);
}

uint16_t PciManager::read_config16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) noexcept {
    uint32_t val = read_config32(bus, dev, func, offset);
    return static_cast<uint16_t>((val >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t PciManager::read_config8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) noexcept {
    uint32_t val = read_config32(bus, dev, func, offset);
    return static_cast<uint8_t>((val >> ((offset & 3) * 8)) & 0xFF);
}

void PciManager::write_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) noexcept {
    uint32_t address = make_config_address(bus, dev, func, offset);
    arch::x86_64::outl(CONFIG_ADDRESS_PORT, address);
    arch::x86_64::outl(CONFIG_DATA_PORT, value);
}

void PciManager::write_config16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t value) noexcept {
    uint32_t address = make_config_address(bus, dev, func, offset);
    arch::x86_64::outl(CONFIG_ADDRESS_PORT, address);
    arch::x86_64::outw(static_cast<uint16_t>(CONFIG_DATA_PORT + (offset & 2)), value);
}

void PciManager::write_config8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint8_t value) noexcept {
    uint32_t address = make_config_address(bus, dev, func, offset);
    arch::x86_64::outl(CONFIG_ADDRESS_PORT, address);
    arch::x86_64::outb(static_cast<uint16_t>(CONFIG_DATA_PORT + (offset & 3)), value);
}

void PciManager::enable_bus_mastering(const PciDevice& dev) noexcept {
    uint16_t cmd = read_config16(dev.bus, dev.device, dev.function, 0x04);
    cmd |= (1 << 2); // Bit 2: Bus Master
    write_config16(dev.bus, dev.device, dev.function, 0x04, cmd);
}

void PciManager::enable_io_space(const PciDevice& dev) noexcept {
    uint16_t cmd = read_config16(dev.bus, dev.device, dev.function, 0x04);
    cmd |= (1 << 0); // Bit 0: I/O Space
    write_config16(dev.bus, dev.device, dev.function, 0x04, cmd);
}

void PciManager::enable_memory_space(const PciDevice& dev) noexcept {
    uint16_t cmd = read_config16(dev.bus, dev.device, dev.function, 0x04);
    cmd |= (1 << 1); // Bit 1: Memory Space
    write_config16(dev.bus, dev.device, dev.function, 0x04, cmd);
}

PciBar PciManager::decode_bar(uint32_t bar_low, uint32_t bar_high) noexcept {
    PciBar bar{};
    if (bar_low == 0 || bar_low == 0xFFFFFFFF) {
        return bar; // unpopulated or non-existent
    }

    if ((bar_low & 0x01) != 0) {
        // I/O Space BAR
        bar.is_io = true;
        bar.is_64bit = false;
        bar.is_prefetchable = false;
        bar.base_address = static_cast<uint64_t>(bar_low & ~0x3U);
        bar.valid = (bar.base_address != 0);
    } else {
        // Memory Space BAR
        bar.is_io = false;
        bar.is_prefetchable = ((bar_low & 0x08) != 0);
        uint8_t type = static_cast<uint8_t>((bar_low >> 1) & 0x03);
        if (type == 0x02) {
            // 64-bit Memory BAR
            bar.is_64bit = true;
            bar.base_address = (static_cast<uint64_t>(bar_high) << 32) | static_cast<uint64_t>(bar_low & ~0xFU);
        } else {
            // 32-bit Memory BAR
            bar.is_64bit = false;
            bar.base_address = static_cast<uint64_t>(bar_low & ~0xFU);
        }
        bar.valid = (bar.base_address != 0);
    }
    return bar;
}

const char* PciManager::format_class(uint8_t class_code, uint8_t subclass) noexcept {
    switch (class_code) {
    case 0x00:
        if (subclass == 0x01) return "VGA-Compatible Unclassified";
        return "Unclassified Device";
    case 0x01:
        switch (subclass) {
        case 0x00: return "SCSI Bus Controller";
        case 0x01: return "IDE Controller";
        case 0x02: return "Floppy Disk Controller";
        case 0x05: return "ATA Controller";
        case 0x06: return "SATA Controller";
        case 0x08: return "NVMe Controller";
        default:   return "Mass Storage Controller";
        }
    case 0x02:
        switch (subclass) {
        case 0x00: return "Ethernet Controller";
        case 0x80: return "Other Network Controller";
        default:   return "Network Controller";
        }
    case 0x03:
        switch (subclass) {
        case 0x00: return "VGA Compatible Controller";
        case 0x01: return "XGA Controller";
        case 0x02: return "3D Controller";
        default:   return "Display Controller";
        }
    case 0x04:
        switch (subclass) {
        case 0x00: return "Multimedia Video Device";
        case 0x01: return "Multimedia Audio Device";
        case 0x03: return "Audio Device (HDA)";
        default:   return "Multimedia Controller";
        }
    case 0x05:
        return "Memory Controller";
    case 0x06:
        switch (subclass) {
        case 0x00: return "Host Bridge";
        case 0x01: return "ISA Bridge";
        case 0x02: return "EISA Bridge";
        case 0x04: return "PCI-to-PCI Bridge";
        case 0x05: return "PCMCIA Bridge";
        case 0x80: return "Other Bridge";
        default:   return "Bridge Device";
        }
    case 0x07:
        switch (subclass) {
        case 0x00: return "Serial Controller (16550 UART)";
        case 0x01: return "Parallel Controller";
        case 0x03: return "Modem";
        default:   return "Simple Comm Controller";
        }
    case 0x08:
        switch (subclass) {
        case 0x00: return "PIC (8259A)";
        case 0x01: return "DMA Controller";
        case 0x02: return "Timer (PIT 8254)";
        case 0x03: return "RTC Controller";
        default:   return "Base System Peripheral";
        }
    case 0x09:
        switch (subclass) {
        case 0x00: return "Keyboard Controller";
        case 0x02: return "Mouse Controller";
        default:   return "Input Device Controller";
        }
    case 0x0C:
        switch (subclass) {
        case 0x03: return "USB Controller";
        case 0x05: return "SMBus Controller";
        default:   return "Serial Bus Controller";
        }
    case 0x0D:
        return "Wireless Controller";
    case 0x11:
        return "Signal Processing Controller";
    case 0x12:
        return "Processing Accelerator";
    default:
        return "Unknown Device";
    }
}

const char* PciDevice::class_string() const noexcept {
    return PciManager::format_class(class_code, subclass);
}

void PciManager::probe_device(uint8_t bus, uint8_t dev, uint8_t func) noexcept {
    if (s_device_count >= MAX_DEVICES) {
        return;
    }

    uint16_t vendor_id = read_config16(bus, dev, func, 0x00);
    if (vendor_id == 0xFFFF || vendor_id == 0x0000) {
        return;
    }

    uint16_t device_id   = read_config16(bus, dev, func, 0x02);
    uint8_t  revision    = read_config8(bus, dev, func, 0x08);
    uint8_t  prog_if     = read_config8(bus, dev, func, 0x09);
    uint8_t  subclass    = read_config8(bus, dev, func, 0x0A);
    uint8_t  class_code  = read_config8(bus, dev, func, 0x0B);
    uint8_t  header_type = read_config8(bus, dev, func, 0x0E);

    PciDevice& d = s_devices[s_device_count];
    d.bus = bus;
    d.device = dev;
    d.function = func;
    d.vendor_id = vendor_id;
    d.device_id = device_id;
    d.revision = revision;
    d.prog_if = prog_if;
    d.subclass = subclass;
    d.class_code = class_code;
    d.header_type = header_type;
    d.bar_count = 0;

    for (size_t b = 0; b < 6; ++b) {
        d.bars[b] = PciBar{};
    }

    // Decode BARs for Header Type 0 (standard device)
    if ((header_type & 0x7F) == 0x00) {
        for (size_t bar_idx = 0; bar_idx < 6; ) {
            uint8_t offset = static_cast<uint8_t>(0x10 + bar_idx * 4);
            uint32_t bar_low = read_config32(bus, dev, func, offset);

            bool is_64bit = ((bar_low & 0x01) == 0) && (((bar_low >> 1) & 0x03) == 0x02);
            uint32_t bar_high = 0;
            if (is_64bit && (bar_idx + 1 < 6)) {
                bar_high = read_config32(bus, dev, func, static_cast<uint8_t>(offset + 4));
            }

            PciBar bar = decode_bar(bar_low, bar_high);
            d.bars[bar_idx] = bar;
            if (bar.valid) {
                d.bar_count++;
            }

            if (is_64bit && (bar_idx + 1 < 6)) {
                bar_idx += 2;
            } else {
                bar_idx += 1;
            }
        }
    }

    s_device_count++;
}

bool PciManager::init() {
    s_device_count = 0;
    s_initialized = false;

    // Scan all 256 standard PCI buses (buses 0..255)
    for (uint16_t bus = 0; bus < 256; ++bus) {
        if (s_device_count >= MAX_DEVICES) {
            break;
        }

        for (uint8_t dev = 0; dev < 32; ++dev) {
            if (s_device_count >= MAX_DEVICES) {
                break;
            }

            uint16_t vendor = read_config16(static_cast<uint8_t>(bus), dev, 0, 0x00);
            if (vendor == 0xFFFF || vendor == 0x0000) {
                continue;
            }

            probe_device(static_cast<uint8_t>(bus), dev, 0);

            uint8_t header_type = read_config8(static_cast<uint8_t>(bus), dev, 0, 0x0E);
            if ((header_type & 0x80) != 0) {
                // Multi-function device: scan functions 1..7
                for (uint8_t func = 1; func < 8; ++func) {
                    if (s_device_count >= MAX_DEVICES) {
                        break;
                    }
                    uint16_t fn_vendor = read_config16(static_cast<uint8_t>(bus), dev, func, 0x00);
                    if (fn_vendor != 0xFFFF && fn_vendor != 0x0000) {
                        probe_device(static_cast<uint8_t>(bus), dev, func);
                    }
                }
            }
        }
    }

    s_initialized = true;
    return true;
}

const PciDevice* PciManager::find_device(uint16_t vendor_id, uint16_t device_id) noexcept {
    for (size_t i = 0; i < s_device_count; ++i) {
        if (s_devices[i].vendor_id == vendor_id && s_devices[i].device_id == device_id) {
            return &s_devices[i];
        }
    }
    return nullptr;
}

const PciDevice* PciManager::find_by_class(uint8_t class_code, uint8_t subclass) noexcept {
    for (size_t i = 0; i < s_device_count; ++i) {
        if (s_devices[i].class_code == class_code && s_devices[i].subclass == subclass) {
            return &s_devices[i];
        }
    }
    return nullptr;
}

} // namespace llamaos::drivers
