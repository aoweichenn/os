set(
    OS_KERNEL_PUBLIC_HEADERS
    include/os/kernel/boot_info.hpp
    include/os/kernel/descriptor_layout.hpp
    include/os/kernel/descriptor_tables.hpp
    include/os/kernel/entry.hpp
    include/os/kernel/exception_frame.hpp
    include/os/kernel/exceptions.hpp
    include/os/kernel/kernel_main.hpp
    include/os/kernel/panic.hpp
    include/os/kernel/processor.hpp
    include/os/kernel/serial_port.hpp
)

set(
    OS_KERNEL_SHARED_SOURCES
    src/boot_info.cpp
    src/descriptor_layout.cpp
    src/descriptor_tables.cpp
    src/exception_frame.cpp
    src/exceptions.cpp
    src/kernel_main.cpp
    src/panic.cpp
    src/processor.cpp
    src/serial_port.cpp
)

add_library(
    os_kernel_boot_protocol_host
    STATIC
        src/boot_info.cpp
        include/os/kernel/boot_info.hpp
)
target_include_directories(
    os_kernel_boot_protocol_host
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(
    os_kernel_boot_protocol_host
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
)

add_library(
    os_kernel_architecture_model_host
    STATIC
        src/descriptor_layout.cpp
        src/exception_frame.cpp
        include/os/kernel/descriptor_layout.hpp
        include/os/kernel/exception_frame.hpp
)
target_include_directories(
    os_kernel_architecture_model_host
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(
    os_kernel_architecture_model_host
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
)

set(
    OS_KERNEL_LINKER_SCRIPT_TEMPLATE
    "${CMAKE_CURRENT_SOURCE_DIR}/linker/kernel.ld.in"
)

find_program(OS_KERNEL_LLD_EXECUTABLE NAMES ld.lld REQUIRED)
find_program(OS_KERNEL_NASM_EXECUTABLE NAMES nasm REQUIRED)

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

set(
    OS_KERNEL_ARCHITECTURE_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/architecture.asm"
)
set(
    OS_KERNEL_ARCHITECTURE_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/generated/architecture.o"
)
add_custom_command(
    OUTPUT "${OS_KERNEL_ARCHITECTURE_OBJECT}"
    COMMAND
        "${CMAKE_COMMAND}"
        -E
        make_directory
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND
        "${OS_KERNEL_NASM_EXECUTABLE}"
        -f
        elf64
        -o
        "${OS_KERNEL_ARCHITECTURE_OBJECT}"
        "${OS_KERNEL_ARCHITECTURE_SOURCE}"
    DEPENDS "${OS_KERNEL_ARCHITECTURE_SOURCE}"
    COMMENT "汇编 x86-64 描述符装载与异常入口"
    VERBATIM
)
add_custom_target(
    os_kernel_architecture_object
    DEPENDS "${OS_KERNEL_ARCHITECTURE_OBJECT}"
)

set(
    OS_KERNEL_FREESTANDING_COMPILE_OPTIONS
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
    -masm=intel
    -mcmodel=kernel
    -mno-mmx
    -mno-red-zone
    -mno-sse
    -mno-sse2
    -nostdinc++
)

function(
    os_add_kernel_object_target
    OS_KERNEL_OBJECT_TARGET
    OS_KERNEL_ENTRY_SOURCE
)
    add_library(${OS_KERNEL_OBJECT_TARGET} OBJECT)
    target_sources(
        ${OS_KERNEL_OBJECT_TARGET}
        PRIVATE
            ${OS_KERNEL_SHARED_SOURCES}
            "${OS_KERNEL_ENTRY_SOURCE}"
    )
    target_include_directories(
        ${OS_KERNEL_OBJECT_TARGET}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_options(
        ${OS_KERNEL_OBJECT_TARGET}
        PRIVATE
            ${OS_KERNEL_FREESTANDING_COMPILE_OPTIONS}
    )
endfunction()

os_add_kernel_object_target(
    os_kernel_objects
    "${CMAKE_CURRENT_SOURCE_DIR}/src/entry.cpp"
)
target_sources(
    os_kernel_objects
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
        FILES
            ${OS_KERNEL_PUBLIC_HEADERS}
)

os_add_kernel_object_target(
    os_kernel_invalid_opcode_objects
    "${PROJECT_SOURCE_DIR}/tests/system/kernel/invalid_opcode_entry.cpp"
)
os_add_kernel_object_target(
    os_kernel_page_fault_objects
    "${PROJECT_SOURCE_DIR}/tests/system/kernel/page_fault_entry.cpp"
)

set(
    OS_KERNEL_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/kernel.elf"
    CACHE INTERNAL
    "自研 ELF64 内核路径"
)
set(
    OS_KERNEL_INVALID_OPCODE_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/kernel_invalid_opcode.elf"
    CACHE INTERNAL
    "非法指令异常注入 ELF64 内核路径"
)
set(
    OS_KERNEL_PAGE_FAULT_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/kernel_page_fault.elf"
    CACHE INTERNAL
    "页故障注入 ELF64 内核路径"
)

function(
    os_add_kernel_elf_target
    OS_KERNEL_ELF_TARGET
    OS_KERNEL_OBJECT_TARGET
    OS_KERNEL_OUTPUT_PATH
    OS_KERNEL_LINK_COMMENT
)
    add_custom_command(
        OUTPUT "${OS_KERNEL_OUTPUT_PATH}"
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
            "${OS_KERNEL_OUTPUT_PATH}"
            $<TARGET_OBJECTS:${OS_KERNEL_OBJECT_TARGET}>
            "${OS_KERNEL_ARCHITECTURE_OBJECT}"
        DEPENDS
            ${OS_KERNEL_OBJECT_TARGET}
            os_kernel_architecture_object
            "${OS_KERNEL_LINKER_SCRIPT}"
        COMMENT "${OS_KERNEL_LINK_COMMENT}"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
    add_custom_target(
        ${OS_KERNEL_ELF_TARGET}
        ALL
        DEPENDS "${OS_KERNEL_OUTPUT_PATH}"
    )
endfunction()

os_add_kernel_elf_target(
    os_kernel_elf
    os_kernel_objects
    "${OS_KERNEL_ELF_PATH}"
    "链接自研 freestanding ELF64 内核"
)
os_add_kernel_elf_target(
    os_kernel_invalid_opcode_elf
    os_kernel_invalid_opcode_objects
    "${OS_KERNEL_INVALID_OPCODE_ELF_PATH}"
    "链接非法指令异常注入 ELF64 内核"
)
os_add_kernel_elf_target(
    os_kernel_page_fault_elf
    os_kernel_page_fault_objects
    "${OS_KERNEL_PAGE_FAULT_ELF_PATH}"
    "链接页故障异常注入 ELF64 内核"
)
