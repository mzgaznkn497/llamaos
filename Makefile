# ==============================================================================
# LlamaOS/A - Top-Level Makefile
# ==============================================================================
# Provides deterministic reproducible targets for building the kernel ELF,
# generating hybrid UEFI/BIOS ISO images, executing QEMU, and running automated tests.
# ==============================================================================

CXX         ?= g++
NASM        ?= nasm
LD          ?= ld
GRUB_RESCUE ?= grub-mkrescue
PYTHON      ?= python3
QEMU        ?= qemu-system-x86_64

OVMF_PATH   ?= /usr/share/ovmf/OVMF.fd
ifeq ($(wildcard $(OVMF_PATH)),)
    OVMF_PATH := /usr/share/OVMF/OVMF_CODE_4M.fd
endif

BUILD_DIR   := build
ISO_ROOT    := $(BUILD_DIR)/iso_root
KERNEL_ELF  := $(BUILD_DIR)/llamaos.elf
ISO_IMAGE   := $(BUILD_DIR)/llamaos.iso

# Freestanding C++ Compiler Flags
CXXFLAGS := \
    -std=c++20 \
    -ffreestanding \
    -fno-builtin \
    -fno-exceptions \
    -fno-rtti \
    -fno-pie \
    -fno-pic \
    -fno-omit-frame-pointer \
    -fstack-protector-strong \
    -mstack-protector-guard=global \
    -mno-red-zone \
    -mcmodel=kernel \
    -mgeneral-regs-only \
    -Wall -Wextra -Werror \
    -O2 -g \
    -I kernel

# NASM Assembly Flags
NASMFLAGS := -f elf64 -g -F dwarf

# Freestanding Linker Flags
LDFLAGS := \
    -m elf_x86_64 \
    -nostdlib \
    -static \
    -z noexecstack \
    -z max-page-size=0x1000 \
    -T kernel/arch/x86_64/linker.ld

# Source Files
ASM_SRCS := \
    kernel/arch/x86_64/boot/boot.asm \
    kernel/arch/x86_64/cpu/gdt_asm.asm \
    kernel/arch/x86_64/cpu/interrupt_stubs.asm \
    kernel/arch/x86_64/cpu/context_switch.asm \
    kernel/syscall/syscall_entry.asm \
    kernel/userland/user_entry.asm

CXX_SRCS := \
    kernel/core/string.cpp \
    kernel/core/kprint.cpp \
    kernel/core/panic.cpp \
    kernel/core/runtime.cpp \
    kernel/arch/x86_64/cpu/cpu.cpp \
    kernel/arch/x86_64/cpu/gdt.cpp \
    kernel/arch/x86_64/cpu/tss.cpp \
    kernel/arch/x86_64/cpu/idt.cpp \
    kernel/arch/x86_64/cpu/interrupts.cpp \
    kernel/arch/x86_64/cpu/pic.cpp \
    kernel/arch/x86_64/cpu/timer.cpp \
    kernel/arch/x86_64/cpu/per_cpu.cpp \
    kernel/boot/boot_info.cpp \
    kernel/arch/x86_64/boot/multiboot2.cpp \
    kernel/memory/reserved_regions.cpp \
    kernel/memory/pmm.cpp \
    kernel/memory/vmm.cpp \
    kernel/memory/kalloc.cpp \
    kernel/drivers/serial.cpp \
    kernel/drivers/vga.cpp \
    kernel/drivers/console/console.cpp \
    kernel/drivers/ps2/scancode.cpp \
    kernel/drivers/ps2/ps2_controller.cpp \
    kernel/drivers/ps2/keyboard.cpp \
    kernel/drivers/pci/pci.cpp \
    kernel/drivers/framebuffer/framebuffer.cpp \
    kernel/drivers/devices/device_registry.cpp \
    kernel/drivers/devices/hardware_report.cpp \
    kernel/storage/block_request.cpp \
    kernel/storage/block_device.cpp \
    kernel/storage/partition.cpp \
    kernel/storage/storage_manager.cpp \
    kernel/storage/gpt.cpp \
    kernel/core/power.cpp \
    kernel/drivers/virtio/virtio_queue.cpp \
    kernel/drivers/virtio/virtio_block.cpp \
    kernel/drivers/virtio/virtio_net.cpp \
    kernel/net/arp.cpp \
    kernel/net/net_interface.cpp \
    kernel/net/icmp.cpp \
    kernel/net/udp.cpp \
    kernel/net/dhcp.cpp \
    kernel/net/tcp.cpp \
    kernel/net/mgmt_server.cpp \
    kernel/net/net_config.cpp \
    kernel/fs/vfs.cpp \
    kernel/fs/fat32.cpp \
    kernel/threading/thread.cpp \
    kernel/threading/stack_allocator.cpp \
    kernel/threading/scheduler.cpp \
    kernel/syscall/syscall.cpp \
    kernel/userland/user_memory.cpp \
    kernel/userland/elf_loader.cpp \
    kernel/userland/process.cpp \
    kernel/kernel_main.cpp

ASM_OBJS := $(patsubst %.asm,$(BUILD_DIR)/%.o,$(ASM_SRCS))
CXX_OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CXX_SRCS))
ALL_OBJS := $(ASM_OBJS) $(CXX_OBJS)

USER_LD      := user/user.ld
USER_SRC     := user/init.cpp
USER_ELF     := $(BUILD_DIR)/init.elf
INIT_ELF_HPP := kernel/userland/init_elf.hpp

SH_LD        := user/user_sh.ld
SH_SRC       := user/sh.cpp
SH_ELF       := $(BUILD_DIR)/sh.elf
DISK_IMAGE   := $(BUILD_DIR)/llamaos.img

REGRESSION_BIN := $(BUILD_DIR)/test_parser
PMM_TEST_BIN   := $(BUILD_DIR)/test_pmm
VMM_TEST_BIN   := $(BUILD_DIR)/test_vmm
DESCRIPTOR_TEST_BIN := $(BUILD_DIR)/test_descriptors
PHASE4_TEST_BIN     := $(BUILD_DIR)/test_phase4
PHASE5_TEST_BIN     := $(BUILD_DIR)/test_phase5
PHASE6_TEST_BIN     := $(BUILD_DIR)/test_phase6
PHASE7_TEST_BIN     := $(BUILD_DIR)/test_phase7
STORAGE_TEST_BIN    := $(BUILD_DIR)/test_storage

.PHONY: all kernel iso test test-bios test-uefi test-faults test-phase4-live test-interactive test-phase5 test-phase5-live test-scheduler test-preemption test-context test-stack test-thread-exit test-thread-stress test-phase6 test-phase6-live test-syscall-init test-syscall-dispatch test-syscall-yield test-phase7 test-phase7-live test-user-entry test-user-syscall test-user-yield test-user-memory test-user-faults test-user-preemption test-unit test-parser test-memory test-pmm test-vmm test-descriptors test-phase4 test-storage qemu qemu-uefi qemu-gdb clean help


all: kernel iso

kernel: $(KERNEL_ELF)

$(KERNEL_ELF): $(ALL_OBJS) kernel/arch/x86_64/linker.ld
	@mkdir -p $(BUILD_DIR)
	@echo " [LD]   $(KERNEL_ELF)"
	@$(LD) $(LDFLAGS) -o $(KERNEL_ELF) $(ALL_OBJS)
	@echo " [INFO] Kernel ELF successfully linked at $(KERNEL_ELF)"

$(BUILD_DIR)/%.o: %.asm
	@mkdir -p $(dir $@)
	@echo " [NASM] $<"
	@$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo " [CXX]  $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(USER_ELF): $(USER_SRC) $(USER_LD)
	@mkdir -p $(BUILD_DIR)
	@echo " [USER_CXX] $@"
	@$(CXX) -m64 -march=x86-64 -std=c++20 -static -no-pie -fno-pie -nostdlib -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -mno-sse -mno-mmx -Wall -Wextra -Werror -T $(USER_LD) $(USER_SRC) -o $@

$(SH_ELF): $(SH_SRC) $(SH_LD)
	@mkdir -p $(BUILD_DIR)
	@echo " [USER_CXX] $@"
	@$(CXX) -m64 -march=x86-64 -std=c++20 -static -no-pie -fno-pie -nostdlib -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -mno-sse -mno-mmx -Wall -Wextra -Werror -T $(SH_LD) $(SH_SRC) -o $@

$(INIT_ELF_HPP): $(USER_ELF) scripts/bin2c.py
	@echo " [BIN2C] $@"
	@$(PYTHON) scripts/bin2c.py $(USER_ELF) $(INIT_ELF_HPP) init_elf_data init_elf_size

$(BUILD_DIR)/kernel/kernel_main.o: $(INIT_ELF_HPP)

iso: $(ISO_IMAGE)

$(ISO_IMAGE): $(KERNEL_ELF) boot/grub/grub.cfg
	@mkdir -p $(ISO_ROOT)/boot/grub
	@cp $(KERNEL_ELF) $(ISO_ROOT)/boot/llamaos.elf
	@cp boot/grub/grub.cfg $(ISO_ROOT)/boot/grub/grub.cfg
	@echo " [GRUB] Generating hybrid bootable ISO image: $(ISO_IMAGE)"
	@$(GRUB_RESCUE) -o $(ISO_IMAGE) $(ISO_ROOT) 2>/dev/null
	@echo " [INFO] Bootable ISO generated successfully at $(ISO_IMAGE)"

disk: $(KERNEL_ELF) $(USER_ELF) $(SH_ELF) scripts/create_boot_disk.sh
	@echo " [DISK] Generating bootable persistent disk image: $(DISK_IMAGE)"
	@bash scripts/create_boot_disk.sh $(DISK_IMAGE)
	@echo " [INFO] Bootable persistent disk image generated at $(DISK_IMAGE)"

test-unit: $(REGRESSION_BIN) $(PMM_TEST_BIN) $(VMM_TEST_BIN) $(DESCRIPTOR_TEST_BIN) $(PHASE4_TEST_BIN) $(PHASE5_TEST_BIN) $(PHASE6_TEST_BIN) $(PHASE7_TEST_BIN) $(STORAGE_TEST_BIN)
	@echo " [TEST] Executing host-side parser regression test suite..."
	@$(REGRESSION_BIN)
	@echo " [TEST] Executing host-side PMM regression test suite..."
	@$(PMM_TEST_BIN)
	@echo " [TEST] Executing host-side VMM regression test suite..."
	@$(VMM_TEST_BIN)
	@echo " [TEST] Executing host-side Descriptor & IDT regression test suite..."
	@$(DESCRIPTOR_TEST_BIN)
	@echo " [TEST] Executing host-side Phase 4 Device & Hardware regression test suite..."
	@$(PHASE4_TEST_BIN)
	@echo " [TEST] Executing host-side Phase 5 Process & Threading regression test suite..."
	@$(PHASE5_TEST_BIN)
	@echo " [TEST] Executing host-side Phase 6 System Call regression test suite..."
	@$(PHASE6_TEST_BIN)
	@echo " [TEST] Executing host-side Phase 7 Minimal Userland regression test suite..."
	@$(PHASE7_TEST_BIN)
	@echo " [TEST] Executing host-side Storage, GPT, VFS & Filesystem regression test suite..."
	@$(STORAGE_TEST_BIN)

test-parser: $(REGRESSION_BIN)
	@$(REGRESSION_BIN)

test-pmm: $(PMM_TEST_BIN)
	@$(PMM_TEST_BIN)

test-vmm: $(VMM_TEST_BIN)
	@$(VMM_TEST_BIN)

test-descriptors: $(DESCRIPTOR_TEST_BIN)
	@$(DESCRIPTOR_TEST_BIN)

test-phase4: $(PHASE4_TEST_BIN)
	@$(PHASE4_TEST_BIN)

test-phase5: $(PHASE5_TEST_BIN)
	@$(PHASE5_TEST_BIN)

test-phase6: $(PHASE6_TEST_BIN)
	@$(PHASE6_TEST_BIN)

test-memory: $(PMM_TEST_BIN) $(VMM_TEST_BIN)
	@echo " [TEST] Running PMM test suite..."
	@$(PMM_TEST_BIN)
	@echo " [TEST] Running VMM test suite..."
	@$(VMM_TEST_BIN)

$(REGRESSION_BIN): tests/test_parser.cpp kernel/boot/boot_info.cpp kernel/arch/x86_64/boot/multiboot2.cpp kernel/core/string.cpp
	@mkdir -p $(BUILD_DIR)
	@echo " [HOST_CXX] $@"
	@$(CXX) -std=c++20 -Wall -Wextra -Werror -I kernel $^ -o $@

$(PMM_TEST_BIN): tests/test_pmm.cpp kernel/memory/reserved_regions.cpp kernel/core/string.cpp kernel/memory/memory_types.hpp kernel/memory/memory_layout.hpp
	@mkdir -p $(BUILD_DIR)
	@echo " [HOST_CXX] $@"
	@$(CXX) -std=c++20 -Wall -Wextra -Werror -I kernel tests/test_pmm.cpp kernel/memory/reserved_regions.cpp kernel/core/string.cpp -o $@

$(VMM_TEST_BIN): tests/test_vmm.cpp kernel/memory/memory_types.hpp kernel/memory/memory_layout.hpp
	@mkdir -p $(BUILD_DIR)
	@echo " [HOST_CXX] $@"
	@$(CXX) -std=c++20 -Wall -Wextra -Werror -I kernel tests/test_vmm.cpp -o $@

$(DESCRIPTOR_TEST_BIN): tests/test_descriptors.cpp
	@mkdir -p $(BUILD_DIR)
	@echo " [HOST_CXX] $@"
	@$(CXX) -std=c++20 -Wall -Wextra -Werror -I kernel tests/test_descriptors.cpp -o $@

$(PHASE4_TEST_BIN): tests/test_phase4.cpp kernel/drivers/ps2/scancode.cpp kernel/drivers/pci/pci.cpp kernel/drivers/framebuffer/framebuffer.cpp kernel/drivers/devices/device_registry.cpp kernel/core/string.cpp
	@mkdir -p $(BUILD_DIR)
	@echo " [HOST_CXX] $@"
	@$(CXX) -std=c++20 -Wall -Wextra -Werror -I kernel $^ -o $@

$(PHASE5_TEST_BIN): tests/test_phase5.cpp kernel/threading/thread.cpp
	@mkdir -p $(BUILD_DIR)
	@echo " [HOST_CXX] $@"
	@$(CXX) -std=c++20 -Wall -Wextra -Werror -I kernel $^ -o $@

$(PHASE6_TEST_BIN): tests/test_phase6.cpp kernel/syscall/syscall_types.hpp kernel/syscall/syscall_abi.hpp
	@mkdir -p $(BUILD_DIR)
	@echo " [HOST_CXX] $@"
	@$(CXX) -std=c++20 -Wall -Wextra -Werror -I kernel tests/test_phase6.cpp -o $@

test-phase7: $(PHASE7_TEST_BIN)
	@$(PHASE7_TEST_BIN)

$(PHASE7_TEST_BIN): tests/test_phase7.cpp kernel/userland/elf_loader.cpp kernel/userland/user_memory.cpp kernel/core/string.cpp
	@mkdir -p $(BUILD_DIR)
	@echo " [HOST_CXX] $@"
	@$(CXX) -std=c++20 -Wall -Wextra -Werror -I kernel $^ -o $@

test-storage: $(STORAGE_TEST_BIN)
	@$(STORAGE_TEST_BIN)

$(STORAGE_TEST_BIN): tests/test_storage.cpp kernel/storage/block_request.cpp kernel/storage/block_device.cpp kernel/storage/partition.cpp kernel/storage/storage_manager.cpp kernel/storage/gpt.cpp kernel/fs/vfs.cpp kernel/fs/fat32.cpp kernel/core/string.cpp
	@mkdir -p $(BUILD_DIR)
	@echo " [HOST_CXX] $@"
	@$(CXX) -std=c++20 -Wall -Wextra -Werror -I kernel $^ -o $@

test: test-unit $(ISO_IMAGE)
	@echo " [TEST] Executing automated QEMU boot test harness (BIOS + UEFI)..."
	@$(PYTHON) scripts/test_boot.py $(ISO_IMAGE)

test-bios: test-unit $(ISO_IMAGE)
	@echo " [TEST] Executing automated QEMU BIOS boot test..."
	@$(PYTHON) scripts/test_boot.py $(ISO_IMAGE) --bios-only

test-uefi: test-unit $(ISO_IMAGE)
	@echo " [TEST] Executing automated QEMU UEFI boot test..."
	@$(PYTHON) scripts/test_boot.py $(ISO_IMAGE) --uefi-only

test-faults: kernel
	@echo " [TEST] Executing isolated hardware CPU fault test suite (#PF, #GP, #DF)..."
	@$(PYTHON) scripts/test_faults.py

test-phase4-live: kernel
	@echo " [TEST] Executing Phase 4 live QEMU test suite (PCI, Keyboard, Framebuffer)..."
	@$(PYTHON) scripts/test_phase4.py

test-interactive: kernel
	@echo " [TEST] Executing interactive keyboard input test..."
	@$(PYTHON) scripts/test_interactive.py

test-phase5-live: kernel
	@echo " [TEST] Executing Phase 5 live QEMU test suite (All Phase 5 live tests)..."
	@$(PYTHON) scripts/test_phase5.py

test-scheduler: kernel
	@echo " [TEST] Executing Phase 5 live cooperative context switching test..."
	@$(PYTHON) scripts/test_phase5.py test-scheduler

test-preemption: kernel
	@echo " [TEST] Executing Phase 5 live PIT preemption test..."
	@$(PYTHON) scripts/test_phase5.py test-preemption

test-context: kernel
	@echo " [TEST] Executing Phase 5 live callee-saved register preservation test..."
	@$(PYTHON) scripts/test_phase5.py test-context

test-stack: kernel
	@echo " [TEST] Executing Phase 5 live stack isolation and guard page test..."
	@$(PYTHON) scripts/test_phase5.py test-stack

test-thread-exit: kernel
	@echo " [TEST] Executing Phase 5 live thread exit and reclamation test..."
	@$(PYTHON) scripts/test_phase5.py test-thread-exit

test-thread-stress: kernel
	@echo " [TEST] Executing Phase 5 live 8-thread concurrency stress test..."
	@$(PYTHON) scripts/test_phase5.py test-thread-stress

test-phase6-live: kernel
	@echo " [TEST] Executing Phase 6 live QEMU test suite (All Phase 6 live tests)..."
	@$(PYTHON) scripts/test_phase6.py

test-syscall-init: kernel
	@echo " [TEST] Executing Phase 6 live MSR & initialization test..."
	@$(PYTHON) scripts/test_phase6.py test-syscall-init

test-syscall-dispatch: kernel
	@echo " [TEST] Executing Phase 6 live syscall dispatch test..."
	@$(PYTHON) scripts/test_phase6.py test-syscall-dispatch

test-syscall-yield: kernel
	@echo " [TEST] Executing Phase 6 live syscall yield test..."
	@$(PYTHON) scripts/test_phase6.py test-syscall-yield

test-phase7-live: kernel
	@echo " [TEST] Executing Phase 7 live QEMU test suite (All Phase 7 live tests)..."
	@$(PYTHON) scripts/test_phase7.py

test-user-entry: kernel
	@echo " [TEST] Executing Phase 7 Ring 3 entry test..."
	@$(PYTHON) scripts/test_phase7.py test-user-entry

test-user-syscall: kernel
	@echo " [TEST] Executing Phase 7 user syscall test..."
	@$(PYTHON) scripts/test_phase7.py test-user-syscall

test-user-yield: kernel
	@echo " [TEST] Executing Phase 7 user yield test..."
	@$(PYTHON) scripts/test_phase7.py test-user-yield

test-user-memory: kernel
	@echo " [TEST] Executing Phase 7 user memory test..."
	@$(PYTHON) scripts/test_phase7.py test-user-memory

test-user-faults: kernel
	@echo " [TEST] Executing Phase 7 user fault containment test..."
	@$(PYTHON) scripts/test_phase7.py test-user-faults

test-user-preemption: kernel
	@echo " [TEST] Executing Phase 7 user preemption test..."
	@$(PYTHON) scripts/test_phase7.py test-user-preemption


qemu: $(ISO_IMAGE)
	@echo " [QEMU] Booting LlamaOS/A in QEMU (BIOS)..."
	$(QEMU) -m 512M -cdrom $(ISO_IMAGE) -serial stdio -vga std

qemu-uefi: $(ISO_IMAGE)
	@echo " [QEMU] Booting LlamaOS/A in QEMU with UEFI firmware ($(OVMF_PATH))..."
	$(QEMU) -m 512M -bios $(OVMF_PATH) -cdrom $(ISO_IMAGE) -serial stdio -vga std

qemu-gdb: $(ISO_IMAGE)
	@echo " [QEMU] Booting with GDB server listening on :1234 (-s -S)..."
	$(QEMU) -m 512M -cdrom $(ISO_IMAGE) -serial stdio -s -S

clean:
	@rm -rf $(BUILD_DIR)
	@echo " [CLEAN] Removed build directory."

help:
	@echo "LlamaOS/A Build System"
	@echo "Targets:"
	@echo "  make            - Build kernel ELF and hybrid bootable ISO"
	@echo "  make kernel     - Build kernel ELF binary"
	@echo "  make iso        - Package kernel into bootable ISO image"
	@echo "  make test       - Run automated QEMU boot test runner"
	@echo "  make qemu       - Launch QEMU with serial connected to console"
	@echo "  make qemu-uefi  - Launch QEMU with UEFI (OVMF) firmware"
	@echo "  make qemu-gdb   - Launch QEMU paused waiting for GDB connection"
	@echo "  make clean      - Remove build artifacts"
