set(
    OS_KERNEL_PUBLIC_HEADERS
    include/os/kernel/boot_info.hpp
    include/os/kernel/block_cache.hpp
    include/os/kernel/console_input.hpp
    include/os/kernel/descriptor_layout.hpp
    include/os/kernel/descriptor_tables.hpp
    include/os/kernel/device_model.hpp
    include/os/kernel/entry.hpp
    include/os/kernel/exception_frame.hpp
    include/os/kernel/exceptions.hpp
    include/os/kernel/file_system.hpp
    include/os/kernel/file_system_format.hpp
    include/os/kernel/freestanding_memory.hpp
    include/os/kernel/kernel_main.hpp
    include/os/kernel/kernel_heap.hpp
    include/os/kernel/kernel_type_cache.hpp
    include/os/kernel/kernel_type_cache.tpp
    include/os/kernel/kernel_virtual_address_allocator.hpp
    include/os/kernel/interrupt_runtime.hpp
    include/os/kernel/io_descriptor.hpp
    include/os/kernel/legacy_pic.hpp
    include/os/kernel/memory_manager.hpp
    include/os/kernel/page_table.hpp
    include/os/kernel/panic.hpp
    include/os/kernel/pipe.hpp
    include/os/kernel/physical_frame_allocator.hpp
    include/os/kernel/physical_memory_map.hpp
    include/os/kernel/port_io.hpp
    include/os/kernel/process_memory_layout.hpp
    include/os/kernel/process_runtime.hpp
    include/os/kernel/process_scheduler.hpp
    include/os/kernel/processor.hpp
    include/os/kernel/programmable_interval_timer.hpp
    include/os/kernel/ps2_keyboard.hpp
    include/os/kernel/serial_port.hpp
    include/os/kernel/spin_lock.hpp
    include/os/kernel/system_calls.hpp
    include/os/kernel/ata_pio.hpp
    include/os/kernel/user_elf.hpp
    include/os/kernel/user_memory.hpp
    include/os/kernel/user_program_images.hpp
)

set(
    OS_KERNEL_SHARED_SOURCES
    src/ata_pio.cpp
    src/block_cache.cpp
    src/boot_info.cpp
    src/console_input.cpp
    src/device_model.cpp
    src/descriptor_layout.cpp
    src/descriptor_tables.cpp
    src/exception_frame.cpp
    src/exceptions.cpp
    src/file_system.cpp
    src/file_system_format.cpp
    src/freestanding_memory.cpp
    src/kernel_main.cpp
    src/kernel_heap.cpp
    src/kernel_type_cache.cpp
    src/kernel_virtual_address_allocator.cpp
    src/interrupt_runtime.cpp
    src/io_descriptor.cpp
    src/legacy_pic.cpp
    src/memory_manager.cpp
    src/page_table.cpp
    src/page_table_layout.cpp
    src/panic.cpp
    src/pipe.cpp
    src/physical_frame_allocator.cpp
    src/physical_memory_map.cpp
    src/port_io.cpp
    src/process_memory_layout.cpp
    src/process_runtime.cpp
    src/process_scheduler.cpp
    src/processor.cpp
    src/programmable_interval_timer.cpp
    src/ps2_keyboard.cpp
    src/serial_port.cpp
    src/spin_lock.cpp
    src/system_calls.cpp
    src/user_elf.cpp
    src/user_memory.cpp
    src/user_program_images.cpp
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

add_library(
    os_kernel_memory_model_host
    STATIC
        src/kernel_heap.cpp
        src/kernel_type_cache.cpp
        src/kernel_virtual_address_allocator.cpp
        src/page_table.cpp
        src/page_table_layout.cpp
        src/physical_frame_allocator.cpp
        src/physical_memory_map.cpp
        include/os/kernel/kernel_heap.hpp
        include/os/kernel/kernel_type_cache.hpp
        include/os/kernel/kernel_type_cache.tpp
        include/os/kernel/kernel_virtual_address_allocator.hpp
        include/os/kernel/page_table.hpp
        include/os/kernel/physical_frame_allocator.hpp
        include/os/kernel/physical_memory_map.hpp
)
target_include_directories(
    os_kernel_memory_model_host
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(
    os_kernel_memory_model_host
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
)

add_library(
    os_kernel_device_model_host
    STATIC
        src/device_model.cpp
        include/os/kernel/device_model.hpp
)
target_include_directories(
    os_kernel_device_model_host
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(
    os_kernel_device_model_host
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
)

add_library(
    os_kernel_user_model_host
    STATIC
        src/user_elf.cpp
        include/os/kernel/user_elf.hpp
)

add_library(
    os_kernel_process_model_host
    STATIC
        src/process_memory_layout.cpp
        src/process_scheduler.cpp
        include/os/kernel/process_memory_layout.hpp
        include/os/kernel/process_scheduler.hpp
)

add_library(
    os_kernel_ipc_model_host
    STATIC
        src/pipe.cpp
        src/spin_lock.cpp
        include/os/kernel/pipe.hpp
        include/os/kernel/spin_lock.hpp
)

add_library(
    os_kernel_user_environment_model_host
    STATIC
        src/console_input.cpp
        src/io_descriptor.cpp
        include/os/kernel/console_input.hpp
        include/os/kernel/io_descriptor.hpp
)
target_include_directories(
    os_kernel_user_environment_model_host
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(
    os_kernel_user_environment_model_host
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
)

add_library(
    os_kernel_file_system_model_host
    STATIC
        src/block_cache.cpp
        src/file_system.cpp
        src/file_system_format.cpp
        src/spin_lock.cpp
        include/os/kernel/block_cache.hpp
        include/os/kernel/file_system.hpp
        include/os/kernel/file_system_format.hpp
        include/os/kernel/spin_lock.hpp
)
target_include_directories(
    os_kernel_file_system_model_host
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(
    os_kernel_file_system_model_host
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
)
target_include_directories(
    os_kernel_ipc_model_host
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(
    os_kernel_ipc_model_host
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
)
target_include_directories(
    os_kernel_process_model_host
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(
    os_kernel_process_model_host
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
)
target_include_directories(
    os_kernel_user_model_host
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(
    os_kernel_user_model_host
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
    COMMENT "汇编 x86-64 描述符、异常与硬件中断入口"
    VERBATIM
)
add_custom_target(
    os_kernel_architecture_object
    DEPENDS "${OS_KERNEL_ARCHITECTURE_OBJECT}"
)

set(
    OS_KERNEL_USER_IMAGES_ASSEMBLY_SOURCE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/user_images.asm"
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/user_images.asm.in"
    "${OS_KERNEL_USER_IMAGES_ASSEMBLY_SOURCE}"
    @ONLY
)
set(
    OS_KERNEL_USER_IMAGES_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/generated/user_images.o"
)
add_custom_command(
    OUTPUT "${OS_KERNEL_USER_IMAGES_OBJECT}"
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
        "${OS_KERNEL_USER_IMAGES_OBJECT}"
        "${OS_KERNEL_USER_IMAGES_ASSEMBLY_SOURCE}"
    DEPENDS
        "${OS_KERNEL_USER_IMAGES_ASSEMBLY_SOURCE}"
        "${OS_USER_SMOKE_ELF_PATH}"
        "${OS_USER_INVALID_OPCODE_ELF_PATH}"
        "${OS_USER_PAGE_FAULT_ELF_PATH}"
        "${OS_USER_SCHEDULER_WORKER_ELF_PATH}"
        "${OS_USER_IPC_PRODUCER_ELF_PATH}"
        "${OS_USER_IPC_CONSUMER_ELF_PATH}"
        "${OS_USER_SHELL_ELF_PATH}"
    COMMENT "嵌入七个自研 Ring 3 ELF64 程序"
    VERBATIM
)
add_custom_target(
    os_kernel_user_images_object
    DEPENDS "${OS_KERNEL_USER_IMAGES_OBJECT}"
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
    target_link_libraries(${OS_KERNEL_OBJECT_TARGET} PRIVATE os_system_abi)
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
os_add_kernel_object_target(
    os_kernel_write_protection_objects
    "${PROJECT_SOURCE_DIR}/tests/system/kernel/write_protection_entry.cpp"
)
os_add_kernel_object_target(
    os_kernel_user_invalid_opcode_objects
    "${PROJECT_SOURCE_DIR}/tests/system/kernel/user_invalid_opcode_entry.cpp"
)
os_add_kernel_object_target(
    os_kernel_user_page_fault_objects
    "${PROJECT_SOURCE_DIR}/tests/system/kernel/user_page_fault_entry.cpp"
)
os_add_kernel_object_target(
    os_kernel_user_invalid_elf_objects
    "${PROJECT_SOURCE_DIR}/tests/system/kernel/user_invalid_elf_entry.cpp"
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
set(
    OS_KERNEL_WRITE_PROTECTION_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/kernel_write_protection.elf"
    CACHE INTERNAL
    "内核写保护故障注入 ELF64 内核路径"
)
set(
    OS_KERNEL_USER_INVALID_OPCODE_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/kernel_user_invalid_opcode.elf"
    CACHE INTERNAL
    "Ring 3 非法指令隔离测试 ELF64 内核路径"
)
set(
    OS_KERNEL_USER_PAGE_FAULT_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/kernel_user_page_fault.elf"
    CACHE INTERNAL
    "Ring 3 页故障隔离测试 ELF64 内核路径"
)
set(
    OS_KERNEL_USER_INVALID_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/kernel_user_invalid_elf.elf"
    CACHE INTERNAL
    "Ring 3 非法 ELF 拒绝测试 ELF64 内核路径"
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
            "${OS_KERNEL_USER_IMAGES_OBJECT}"
        DEPENDS
            ${OS_KERNEL_OBJECT_TARGET}
            $<TARGET_OBJECTS:${OS_KERNEL_OBJECT_TARGET}>
            os_kernel_architecture_object
            os_kernel_user_images_object
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
os_add_kernel_elf_target(
    os_kernel_write_protection_elf
    os_kernel_write_protection_objects
    "${OS_KERNEL_WRITE_PROTECTION_ELF_PATH}"
    "链接内核写保护故障注入 ELF64 内核"
)
os_add_kernel_elf_target(
    os_kernel_user_invalid_opcode_elf
    os_kernel_user_invalid_opcode_objects
    "${OS_KERNEL_USER_INVALID_OPCODE_ELF_PATH}"
    "链接 Ring 3 非法指令隔离测试 ELF64 内核"
)
os_add_kernel_elf_target(
    os_kernel_user_page_fault_elf
    os_kernel_user_page_fault_objects
    "${OS_KERNEL_USER_PAGE_FAULT_ELF_PATH}"
    "链接 Ring 3 页故障隔离测试 ELF64 内核"
)
os_add_kernel_elf_target(
    os_kernel_user_invalid_elf
    os_kernel_user_invalid_elf_objects
    "${OS_KERNEL_USER_INVALID_ELF_PATH}"
    "链接 Ring 3 非法 ELF 拒绝测试 ELF64 内核"
)
