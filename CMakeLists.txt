cmake_minimum_required(VERSION 3.20)
project(LlamaOSA LANGUAGES CXX ASM_NASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Freestanding C++ kernel flags
set(KERNEL_CXX_FLAGS
    -ffreestanding
    -fno-builtin
    -fno-exceptions
    -fno-rtti
    -fno-pie
    -fno-pic
    -fno-omit-frame-pointer
    -fstack-protector-strong
    -mstack-protector-guard=global
    -mno-red-zone
    -mcmodel=kernel
    -mgeneral-regs-only
    -Wall
    -Wextra
    -Werror
    -O2
    -g
)

# NASM configuration
set(CMAKE_ASM_NASM_OBJECT_FORMAT "elf64")
set(CMAKE_ASM_NASM_FLAGS "-g -F dwarf")

# Kernel Sources
set(KERNEL_ASM_SOURCES
    kernel/arch/x86_64/boot/boot.asm
    kernel/arch/x86_64/cpu/gdt_asm.asm
    kernel/arch/x86_64/cpu/interrupt_stubs.asm
    kernel/arch/x86_64/cpu/context_switch.asm
    kernel/syscall/syscall_entry.asm
    kernel/userland/user_entry.asm
)

set(KERNEL_CXX_SOURCES
    kernel/core/string.cpp
    kernel/core/kprint.cpp
    kernel/core/panic.cpp
    kernel/core/runtime.cpp
    kernel/arch/x86_64/cpu/cpu.cpp
    kernel/arch/x86_64/cpu/gdt.cpp
    kernel/arch/x86_64/cpu/tss.cpp
    kernel/arch/x86_64/cpu/idt.cpp
    kernel/arch/x86_64/cpu/interrupts.cpp
    kernel/arch/x86_64/cpu/pic.cpp
    kernel/arch/x86_64/cpu/timer.cpp
    kernel/arch/x86_64/cpu/per_cpu.cpp
    kernel/boot/boot_info.cpp
    kernel/arch/x86_64/boot/multiboot2.cpp
    kernel/memory/reserved_regions.cpp
    kernel/memory/pmm.cpp
    kernel/memory/vmm.cpp
    kernel/memory/kalloc.cpp
    kernel/drivers/serial.cpp
    kernel/drivers/vga.cpp
    kernel/drivers/console/console.cpp
    kernel/drivers/ps2/scancode.cpp
    kernel/drivers/ps2/ps2_controller.cpp
    kernel/drivers/ps2/keyboard.cpp
    kernel/drivers/pci/pci.cpp
    kernel/drivers/framebuffer/framebuffer.cpp
    kernel/drivers/devices/device_registry.cpp
    kernel/drivers/devices/hardware_report.cpp
    kernel/threading/thread.cpp
    kernel/threading/stack_allocator.cpp
    kernel/threading/scheduler.cpp
    kernel/syscall/syscall.cpp
    kernel/userland/user_memory.cpp
    kernel/userland/elf_loader.cpp
    kernel/userland/process.cpp
    kernel/kernel_main.cpp
)

set(USER_LD ${CMAKE_CURRENT_SOURCE_DIR}/user/user.ld)
set(USER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/user/init.cpp)
set(USER_ELF ${CMAKE_CURRENT_BINARY_DIR}/init.elf)
set(INIT_ELF_HPP ${CMAKE_CURRENT_SOURCE_DIR}/kernel/userland/init_elf.hpp)

add_custom_command(
    OUTPUT ${INIT_ELF_HPP}
    COMMAND ${CMAKE_CXX_COMPILER} -m64 -march=x86-64 -std=c++20 -static -no-pie -fno-pie -nostdlib -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -Wall -Wextra -Werror -T ${USER_LD} ${USER_SRC} -o ${USER_ELF}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/bin2c.py ${USER_ELF} ${INIT_ELF_HPP} init_elf_data init_elf_size
    DEPENDS ${USER_SRC} ${USER_LD} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/bin2c.py
    COMMENT "Building user init.elf and generating init_elf.hpp"
)

set(KERNEL_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/kernel/arch/x86_64/linker.ld)

add_executable(llamaos.elf ${KERNEL_ASM_SOURCES} ${KERNEL_CXX_SOURCES} ${INIT_ELF_HPP})
target_compile_options(llamaos.elf PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${KERNEL_CXX_FLAGS}>)
target_include_directories(llamaos.elf PRIVATE kernel)

target_link_options(llamaos.elf PRIVATE
    -nostdlib
    -static
    -Wl,-m,elf_x86_64
    -Wl,-z,noexecstack
    -Wl,-z,max-page-size=0x1000
    -T ${KERNEL_LINKER_SCRIPT}
)
set_target_properties(llamaos.elf PROPERTIES LINK_DEPENDS ${KERNEL_LINKER_SCRIPT})

# ISO generation custom target
set(ISO_ROOT ${CMAKE_CURRENT_BINARY_DIR}/iso_root)
set(ISO_PATH ${CMAKE_CURRENT_BINARY_DIR}/llamaos.iso)

add_custom_command(
    OUTPUT ${ISO_PATH}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ISO_ROOT}/boot/grub
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:llamaos.elf> ${ISO_ROOT}/boot/llamaos.elf
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/boot/grub/grub.cfg ${ISO_ROOT}/boot/grub/grub.cfg
    COMMAND grub-mkrescue -o ${ISO_PATH} ${ISO_ROOT} 2>/dev/null
    DEPENDS llamaos.elf ${CMAKE_CURRENT_SOURCE_DIR}/boot/grub/grub.cfg
    COMMENT "Building bootable hybrid ISO image ${ISO_PATH}"
)

add_custom_target(iso ALL DEPENDS ${ISO_PATH})

# Host regression test executables
add_executable(test_parser
    tests/test_parser.cpp
    kernel/arch/x86_64/boot/multiboot2.cpp
    kernel/core/string.cpp
)
target_include_directories(test_parser PRIVATE kernel)
target_compile_options(test_parser PRIVATE -Wall -Wextra -Werror)

add_executable(test_pmm
    tests/test_pmm.cpp
    kernel/memory/reserved_regions.cpp
    kernel/core/string.cpp
)
target_include_directories(test_pmm PRIVATE kernel)
target_compile_options(test_pmm PRIVATE -Wall -Wextra -Werror)

add_executable(test_vmm
    tests/test_vmm.cpp
    kernel/memory/reserved_regions.cpp
    kernel/core/string.cpp
)
target_include_directories(test_vmm PRIVATE kernel)
target_compile_options(test_vmm PRIVATE -Wall -Wextra -Werror)

add_executable(test_descriptors
    tests/test_descriptors.cpp
)
target_include_directories(test_descriptors PRIVATE kernel)
target_compile_options(test_descriptors PRIVATE -Wall -Wextra -Werror)

add_executable(test_phase4
    tests/test_phase4.cpp
    kernel/drivers/ps2/scancode.cpp
    kernel/drivers/pci/pci.cpp
    kernel/drivers/framebuffer/framebuffer.cpp
    kernel/drivers/devices/device_registry.cpp
    kernel/core/string.cpp
)
target_include_directories(test_phase4 PRIVATE kernel)
target_compile_options(test_phase4 PRIVATE -Wall -Wextra -Werror)

add_executable(test_phase5
    tests/test_phase5.cpp
    kernel/threading/thread.cpp
)
target_include_directories(test_phase5 PRIVATE kernel)
target_compile_options(test_phase5 PRIVATE -Wall -Wextra -Werror)

add_executable(test_phase6
    tests/test_phase6.cpp
)
target_include_directories(test_phase6 PRIVATE kernel)
target_compile_options(test_phase6 PRIVATE -Wall -Wextra -Werror)

add_executable(test_phase7
    tests/test_phase7.cpp
    kernel/userland/elf_loader.cpp
    kernel/userland/user_memory.cpp
    kernel/core/string.cpp
)
target_include_directories(test_phase7 PRIVATE kernel)
target_compile_options(test_phase7 PRIVATE -Wall -Wextra -Werror)

# Standard CTest integration
enable_testing()
add_test(NAME ParserTest COMMAND test_parser)
add_test(NAME PmmTest COMMAND test_pmm)
add_test(NAME VmmTest COMMAND test_vmm)
add_test(NAME DescriptorTest COMMAND test_descriptors)
add_test(NAME Phase4Test COMMAND test_phase4)
add_test(NAME Phase5Test COMMAND test_phase5)
add_test(NAME Phase6Test COMMAND test_phase6)
add_test(NAME Phase7Test COMMAND test_phase7)

add_custom_target(test-unit
    COMMAND $<TARGET_FILE:test_parser>
    COMMAND $<TARGET_FILE:test_pmm>
    COMMAND $<TARGET_FILE:test_vmm>
    COMMAND $<TARGET_FILE:test_descriptors>
    COMMAND $<TARGET_FILE:test_phase4>
    COMMAND $<TARGET_FILE:test_phase5>
    COMMAND $<TARGET_FILE:test_phase6>
    COMMAND $<TARGET_FILE:test_phase7>
    DEPENDS test_parser test_pmm test_vmm test_descriptors test_phase4 test_phase5 test_phase6 test_phase7
    COMMENT "Executing host unit regression tests"
)

add_custom_target(test-phase7
    COMMAND $<TARGET_FILE:test_phase7>
    DEPENDS test_phase7
    COMMENT "Executing host Phase 7 regression tests"
)

add_custom_target(test-phase7-live
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_phase7.py
    DEPENDS iso
    COMMENT "Executing Phase 7 live QEMU test suite"
)

add_custom_target(test-phase6
    COMMAND $<TARGET_FILE:test_phase6>
    DEPENDS test_phase6
    COMMENT "Executing host Phase 6 regression tests"
)

add_custom_target(test-phase5
    COMMAND $<TARGET_FILE:test_phase5>
    DEPENDS test_phase5
    COMMENT "Executing host Phase 5 regression tests"
)

add_custom_target(test-phase4
    COMMAND $<TARGET_FILE:test_phase4>
    DEPENDS test_phase4
    COMMENT "Executing host Phase 4 regression tests"
)

add_custom_target(test-descriptors
    COMMAND $<TARGET_FILE:test_descriptors>
    DEPENDS test_descriptors
    COMMENT "Executing host descriptor regression tests"
)

add_custom_target(test-pmm
    COMMAND $<TARGET_FILE:test_pmm>
    DEPENDS test_pmm
    COMMENT "Executing host PMM regression tests"
)

add_custom_target(test-vmm
    COMMAND $<TARGET_FILE:test_vmm>
    DEPENDS test_vmm
    COMMENT "Executing host VMM regression tests"
)

add_custom_target(test-memory
    COMMAND $<TARGET_FILE:test_pmm>
    COMMAND $<TARGET_FILE:test_vmm>
    DEPENDS test_pmm test_vmm
    COMMENT "Executing host PMM and VMM regression tests"
)

# Boot testing target (BIOS + UEFI)
add_custom_target(test-boot
    COMMAND $<TARGET_FILE:test_parser>
    COMMAND $<TARGET_FILE:test_pmm>
    COMMAND $<TARGET_FILE:test_vmm>
    COMMAND $<TARGET_FILE:test_descriptors>
    COMMAND $<TARGET_FILE:test_phase4>
    COMMAND $<TARGET_FILE:test_phase5>
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_boot.py ${ISO_PATH}
    DEPENDS iso test_parser test_pmm test_vmm test_descriptors test_phase4 test_phase5
    COMMENT "Running regression tests and automated QEMU boot test"
)

add_custom_target(test-bios
    COMMAND $<TARGET_FILE:test_parser>
    COMMAND $<TARGET_FILE:test_pmm>
    COMMAND $<TARGET_FILE:test_vmm>
    COMMAND $<TARGET_FILE:test_descriptors>
    COMMAND $<TARGET_FILE:test_phase4>
    COMMAND $<TARGET_FILE:test_phase5>
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_boot.py ${ISO_PATH} --bios-only
    DEPENDS iso test_parser test_pmm test_vmm test_descriptors test_phase4 test_phase5
    COMMENT "Running regression tests and BIOS boot test"
)

add_custom_target(test-uefi
    COMMAND $<TARGET_FILE:test_parser>
    COMMAND $<TARGET_FILE:test_pmm>
    COMMAND $<TARGET_FILE:test_vmm>
    COMMAND $<TARGET_FILE:test_descriptors>
    COMMAND $<TARGET_FILE:test_phase4>
    COMMAND $<TARGET_FILE:test_phase5>
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_boot.py ${ISO_PATH} --uefi-only
    DEPENDS iso test_parser test_pmm test_vmm test_descriptors test_phase4 test_phase5
    COMMENT "Running regression tests and UEFI boot test"
)

add_custom_target(test-faults
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_faults.py
    DEPENDS llamaos.elf
    COMMENT "Running isolated hardware CPU fault tests (#PF, #GP, #DF)"
)

add_custom_target(test-phase4-live
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_phase4.py
    DEPENDS llamaos.elf
    COMMENT "Running Phase 4 live QEMU test suite (PCI, Keyboard, Framebuffer)"
)

add_custom_target(test-interactive
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_interactive.py
    DEPENDS llamaos.elf
    COMMENT "Running interactive keyboard input test"
)

add_custom_target(test-phase5-live
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_phase5.py
    DEPENDS llamaos.elf
    COMMENT "Running Phase 5 live QEMU test suite (All Phase 5 live tests)"
)

add_custom_target(test-scheduler
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_phase5.py test-scheduler
    DEPENDS llamaos.elf
    COMMENT "Running Phase 5 cooperative context switching test"
)

add_custom_target(test-preemption
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_phase5.py test-preemption
    DEPENDS llamaos.elf
    COMMENT "Running Phase 5 PIT preemption test"
)

add_custom_target(test-context
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_phase5.py test-context
    DEPENDS llamaos.elf
    COMMENT "Running Phase 5 register preservation test"
)

add_custom_target(test-stack
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_phase5.py test-stack
    DEPENDS llamaos.elf
    COMMENT "Running Phase 5 stack isolation and guard page test"
)

add_custom_target(test-thread-exit
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_phase5.py test-thread-exit
    DEPENDS llamaos.elf
    COMMENT "Running Phase 5 thread exit and reclamation test"
)

add_custom_target(test-thread-stress
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_phase5.py test-thread-stress
    DEPENDS llamaos.elf
    COMMENT "Running Phase 5 8-thread concurrency stress test"
)


