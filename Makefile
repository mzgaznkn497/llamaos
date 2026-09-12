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
    kernel/arch/x86_64/boot/boot.asm

CXX_SRCS := \
    kernel/core/string.cpp \
    kernel/core/kprint.cpp \
    kernel/core/panic.cpp \
    kernel/core/runtime.cpp \
    kernel/arch/x86_64/cpu/cpu.cpp \
    kernel/arch/x86_64/boot/multiboot2.cpp \
    kernel/drivers/serial.cpp \
    kernel/drivers/vga.cpp \
    kernel/kernel_main.cpp

ASM_OBJS := $(patsubst %.asm,$(BUILD_DIR)/%.o,$(ASM_SRCS))
CXX_OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CXX_SRCS))
ALL_OBJS := $(ASM_OBJS) $(CXX_OBJS)

REGRESSION_BIN := $(BUILD_DIR)/test_parser

.PHONY: all kernel iso test test-unit qemu qemu-uefi qemu-gdb clean help

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

iso: $(ISO_IMAGE)

$(ISO_IMAGE): $(KERNEL_ELF) boot/grub/grub.cfg
	@mkdir -p $(ISO_ROOT)/boot/grub
	@cp $(KERNEL_ELF) $(ISO_ROOT)/boot/llamaos.elf
	@cp boot/grub/grub.cfg $(ISO_ROOT)/boot/grub/grub.cfg
	@echo " [GRUB] Generating hybrid bootable ISO image: $(ISO_IMAGE)"
	@$(GRUB_RESCUE) -o $(ISO_IMAGE) $(ISO_ROOT) 2>/dev/null
	@echo " [INFO] Bootable ISO generated successfully at $(ISO_IMAGE)"

test-unit: $(REGRESSION_BIN)
	@echo " [TEST] Executing host-side parser regression test suite..."
	@$(REGRESSION_BIN)

$(REGRESSION_BIN): tests/test_parser.cpp kernel/arch/x86_64/boot/multiboot2.cpp kernel/core/string.cpp
	@mkdir -p $(BUILD_DIR)
	@echo " [HOST_CXX] $@"
	@$(CXX) -std=c++20 -Wall -Wextra -Werror -I kernel $^ -o $@

test: test-unit $(ISO_IMAGE)
	@echo " [TEST] Executing automated QEMU boot test harness..."
	@$(PYTHON) scripts/test_boot.py $(ISO_IMAGE)

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
