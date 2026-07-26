cmake_minimum_required(VERSION 3.28)

project(
    x86_64_os_lab
    VERSION 1.1.0
    DESCRIPTION "从复位向量开始实现的 x86-64 教学操作系统"
    LANGUAGES CXX
)

include(CTest)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(
    OS_COMMON_WARNING_FLAGS
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Werror
)

add_subdirectory(source/foundation)
add_subdirectory(source/abi)
add_subdirectory(source/user)
add_subdirectory(source/kernel)

set(OS_FIRMWARE_IMAGE_SIZE_BYTES 131072)
set(OS_DISK_IMAGE_SIZE_BYTES 2097152)
set(OS_IMAGE_SIZE_MISMATCH_INCREMENT 1)
math(
    EXPR
    OS_INVALID_FIRMWARE_IMAGE_SIZE_BYTES
    "${OS_FIRMWARE_IMAGE_SIZE_BYTES} + ${OS_IMAGE_SIZE_MISMATCH_INCREMENT}"
)
set(OS_IMAGE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/images")
set(
    OS_EMPTY_FIRMWARE_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/empty_firmware.bin"
)
set(
    OS_EMPTY_DISK_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/empty_disk.img"
)
set(
    OS_FIRMWARE_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/firmware.bin"
)
set(
    OS_FIRMWARE_SERIAL_FAILURE_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/firmware_serial_failure.bin"
)
set(
    OS_FIRMWARE_IDE_BUSY_FAILURE_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/firmware_ide_busy_failure.bin"
)
set(
    OS_FIRMWARE_IDE_ERROR_FAILURE_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/firmware_ide_error_failure.bin"
)
set(OS_BOOT_DISK_IMAGE "${OS_IMAGE_OUTPUT_DIRECTORY}/boot_disk.img")
set(
    OS_STAGE1_INVALID_HEADER_DISK_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/stage1_invalid_header_disk.img"
)
set(
    OS_STAGE1_INVALID_CHECKSUM_DISK_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/stage1_invalid_checksum_disk.img"
)
set(
    OS_KERNEL_INVALID_HEADER_DISK_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/kernel_invalid_header_disk.img"
)
set(
    OS_KERNEL_INVALID_CHECKSUM_DISK_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/kernel_invalid_checksum_disk.img"
)
set(
    OS_KERNEL_INVALID_ELF_DISK_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/kernel_invalid_elf_disk.img"
)
set(
    OS_STAGE1_KERNEL_ATA_TIMEOUT_DISK_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/stage1_kernel_ata_timeout/boot_disk.img"
)
set(
    OS_STAGE1_KERNEL_ATA_ERROR_DISK_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/stage1_kernel_ata_error/boot_disk.img"
)
set(
    OS_STAGE1_MEMORY_MAP_INVALID_DISK_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/stage1_memory_map_invalid/boot_disk.img"
)
set(
    OS_KERNEL_INVALID_OPCODE_DISK_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/kernel_invalid_opcode/boot_disk.img"
)
set(
    OS_KERNEL_PAGE_FAULT_DISK_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/kernel_page_fault/boot_disk.img"
)
set(
    OS_KERNEL_WRITE_PROTECTION_DISK_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/kernel_write_protection/boot_disk.img"
)
set(
    OS_KERNEL_USER_INVALID_OPCODE_DISK_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/kernel_user_invalid_opcode/boot_disk.img"
)
set(
    OS_KERNEL_USER_PAGE_FAULT_DISK_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/kernel_user_page_fault/boot_disk.img"
)
set(
    OS_KERNEL_USER_INVALID_ELF_DISK_IMAGE
    "${OS_IMAGE_OUTPUT_DIRECTORY}/kernel_user_invalid_elf/boot_disk.img"
)

add_subdirectory(source/firmware)
add_subdirectory(source/boot/stage1)

add_custom_command(
    OUTPUT
        "${OS_EMPTY_FIRMWARE_IMAGE}"
        "${OS_EMPTY_DISK_IMAGE}"
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/os.py"
        create-images
        --output-directory
        "${OS_IMAGE_OUTPUT_DIRECTORY}"
        --firmware-size-bytes
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        --disk-size-bytes
        "${OS_DISK_IMAGE_SIZE_BYTES}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/os.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/os_tools/images.py"
    COMMENT "生成 v0.0 QEMU 空镜像"
    VERBATIM
)

add_custom_target(
    os_images
    ALL
    DEPENDS
        os_firmware
        os_firmware_serial_failure
        os_firmware_ide_busy_failure
        os_firmware_ide_error_failure
        os_stage1_images
        "${OS_EMPTY_FIRMWARE_IMAGE}"
        "${OS_EMPTY_DISK_IMAGE}"
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
