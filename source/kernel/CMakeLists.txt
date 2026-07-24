set(
    OS_KERNEL_PUBLIC_HEADERS
    include/os/kernel/entry.hpp
)

set(
    OS_KERNEL_PRIVATE_SOURCES
    src/entry.cpp
)

set(
    OS_KERNEL_LINKER_SCRIPT_TEMPLATE
    "${CMAKE_CURRENT_SOURCE_DIR}/linker/kernel.ld.in"
)

find_program(OS_KERNEL_LLD_EXECUTABLE NAMES ld.lld REQUIRED)

set(OS_KERNEL_LINK_ADDRESS 0x00100000)
set(OS_KERNEL_PAGE_SIZE_BYTES 0x00001000)
set(OS_KERNEL_MAXIMUM_PAGE_SIZE_BYTES 0x1000)
set(OS_KERNEL_ELF_PROGRAM_FLAG_EXECUTE 1)
set(OS_KERNEL_ELF_PROGRAM_FLAG_WRITE 2)
set(OS_KERNEL_ELF_PROGRAM_FLAG_READ 4)
math(
    EXPR
    OS_KERNEL_SEGMENT_FLAGS_READ_EXECUTE
    "${OS_KERNEL_ELF_PROGRAM_FLAG_READ} | ${OS_KERNEL_ELF_PROGRAM_FLAG_EXECUTE}"
)
math(
    EXPR
    OS_KERNEL_SEGMENT_FLAGS_READ_WRITE
    "${OS_KERNEL_ELF_PROGRAM_FLAG_READ} | ${OS_KERNEL_ELF_PROGRAM_FLAG_WRITE}"
)
set(
    OS_KERNEL_SEGMENT_FLAGS_READ_ONLY
    "${OS_KERNEL_ELF_PROGRAM_FLAG_READ}"
)
set(
    OS_KERNEL_LINKER_SCRIPT
    "${CMAKE_CURRENT_BINARY_DIR}/kernel.ld"
)
configure_file(
    "${OS_KERNEL_LINKER_SCRIPT_TEMPLATE}"
    "${OS_KERNEL_LINKER_SCRIPT}"
    @ONLY
)

add_library(os_kernel_objects OBJECT)
target_sources(
    os_kernel_objects
    PRIVATE
        ${OS_KERNEL_PRIVATE_SOURCES}
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
        FILES
            ${OS_KERNEL_PUBLIC_HEADERS}
)
target_include_directories(
    os_kernel_objects
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(
    os_kernel_objects
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

set(
    OS_KERNEL_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/kernel.elf"
    CACHE INTERNAL
    "自研 ELF64 内核路径"
)

add_custom_command(
    OUTPUT "${OS_KERNEL_ELF_PATH}"
    COMMAND
        "${OS_KERNEL_LLD_EXECUTABLE}"
        -m
        elf_x86_64
        -T
        "${OS_KERNEL_LINKER_SCRIPT}"
        --build-id=none
        -z
        max-page-size=${OS_KERNEL_MAXIMUM_PAGE_SIZE_BYTES}
        -o
        "${OS_KERNEL_ELF_PATH}"
        $<TARGET_OBJECTS:os_kernel_objects>
    DEPENDS
        os_kernel_objects
        "${OS_KERNEL_LINKER_SCRIPT}"
    COMMENT "链接自研 freestanding ELF64 内核"
    COMMAND_EXPAND_LISTS
    VERBATIM
)

add_custom_target(
    os_kernel_elf
    ALL
    DEPENDS "${OS_KERNEL_ELF_PATH}"
)
