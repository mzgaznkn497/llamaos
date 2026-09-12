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
)

set(KERNEL_CXX_SOURCES
    kernel/core/string.cpp
    kernel/core/kprint.cpp
    kernel/core/panic.cpp
    kernel/core/runtime.cpp
    kernel/arch/x86_64/cpu/cpu.cpp
    kernel/arch/x86_64/boot/multiboot2.cpp
    kernel/drivers/serial.cpp
    kernel/drivers/vga.cpp
    kernel/kernel_main.cpp
)

set(KERNEL_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/kernel/arch/x86_64/linker.ld)

add_executable(llamaos.elf ${KERNEL_ASM_SOURCES} ${KERNEL_CXX_SOURCES})
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

# Host regression test executable
add_executable(test_parser
    tests/test_parser.cpp
    kernel/arch/x86_64/boot/multiboot2.cpp
    kernel/core/string.cpp
)
target_include_directories(test_parser PRIVATE kernel)
target_compile_options(test_parser PRIVATE -Wall -Wextra -Werror)

# Boot testing target
add_custom_target(test-boot
    COMMAND $<TARGET_FILE:test_parser>
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_boot.py ${ISO_PATH}
    DEPENDS iso test_parser
    COMMENT "Running regression tests and automated QEMU boot test"
)
