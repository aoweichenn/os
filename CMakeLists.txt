cmake_minimum_required(VERSION 3.28)

project(
    x86_64_os_lab
    VERSION 0.0.0
    DESCRIPTION "从复位向量开始实现的 x86-64 教学操作系统"
    LANGUAGES CXX
)

include(CTest)

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

set(
    OS_FOUNDATION_SOURCES
    source/foundation/address_range.cpp
)

set(
    OS_FOUNDATION_PUBLIC_INCLUDE_DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/source/foundation/include"
)

add_library(os_foundation_host STATIC ${OS_FOUNDATION_SOURCES})
target_include_directories(
    os_foundation_host
    PUBLIC
        "${OS_FOUNDATION_PUBLIC_INCLUDE_DIRECTORY}"
)
target_compile_options(
    os_foundation_host
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
)

add_library(os_foundation_x86_64 STATIC ${OS_FOUNDATION_SOURCES})
target_include_directories(
    os_foundation_x86_64
    PUBLIC
        "${OS_FOUNDATION_PUBLIC_INCLUDE_DIRECTORY}"
)
target_compile_options(
    os_foundation_x86_64
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
        --target=x86_64-unknown-none-elf
        -ffreestanding
        -fno-builtin
        -fno-exceptions
        -fno-pic
        -fno-pie
        -fno-rtti
        -fno-stack-protector
        -fno-threadsafe-statics
        -fno-use-cxa-atexit
        -m64
        -mcmodel=kernel
        -mno-mmx
        -mno-red-zone
        -mno-sse
        -mno-sse2
        -nostdinc++
)

set(OS_FIRMWARE_IMAGE_SIZE_BYTES 131072)
set(OS_DISK_IMAGE_SIZE_BYTES 1048576)
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

add_custom_command(
    OUTPUT
        "${OS_EMPTY_FIRMWARE_IMAGE}"
        "${OS_EMPTY_DISK_IMAGE}"
    COMMAND
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/create_empty_images.sh"
        "${OS_IMAGE_OUTPUT_DIRECTORY}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/create_empty_images.sh"
    COMMENT "生成 v0.0 QEMU 空镜像"
    VERBATIM
)

add_custom_target(
    os_images
    ALL
    DEPENDS
        "${OS_EMPTY_FIRMWARE_IMAGE}"
        "${OS_EMPTY_DISK_IMAGE}"
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
