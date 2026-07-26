find_program(OS_USER_CLANG_EXECUTABLE NAMES clang++ REQUIRED)
find_program(OS_USER_LLD_EXECUTABLE NAMES ld.lld REQUIRED)
find_program(OS_USER_NASM_EXECUTABLE NAMES nasm REQUIRED)

add_library(
    os_user_shell_model_host
    STATIC
        src/shell_parser.cpp
        include/os/user/shell_parser.hpp
)
target_include_directories(
    os_user_shell_model_host
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(
    os_user_shell_model_host
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
)

set(OS_USER_VIRTUAL_BASE 0x40000000)
set(OS_USER_PAGE_SIZE_BYTES 0x1000)
set(OS_USER_MAXIMUM_PAGE_SIZE_BYTES 0x1000)
set(OS_USER_ELF_PROGRAM_FLAG_EXECUTE 1)
set(OS_USER_ELF_PROGRAM_FLAG_WRITE 2)
set(OS_USER_ELF_PROGRAM_FLAG_READ 4)
math(
    EXPR
    OS_USER_SEGMENT_FLAGS_READ_EXECUTE
    "${OS_USER_ELF_PROGRAM_FLAG_READ} | ${OS_USER_ELF_PROGRAM_FLAG_EXECUTE}"
)
math(
    EXPR
    OS_USER_SEGMENT_FLAGS_READ_WRITE
    "${OS_USER_ELF_PROGRAM_FLAG_READ} | ${OS_USER_ELF_PROGRAM_FLAG_WRITE}"
)
set(OS_USER_SEGMENT_FLAGS_READ_ONLY "${OS_USER_ELF_PROGRAM_FLAG_READ}")

set(
    OS_USER_LINKER_SCRIPT
    "${CMAKE_CURRENT_BINARY_DIR}/user.ld"
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/linker/user.ld.in"
    "${OS_USER_LINKER_SCRIPT}"
    @ONLY
)

set(
    OS_USER_SYSTEM_CALL_ASSEMBLY_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/generated/system_call.o"
)

set(
    OS_USER_EXTENDED_STATE_ASSEMBLY_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/generated/extended_state.o"
)
add_custom_command(
    OUTPUT "${OS_USER_EXTENDED_STATE_ASSEMBLY_OBJECT}"
    COMMAND
        "${CMAKE_COMMAND}"
        -E
        make_directory
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND
        "${OS_USER_NASM_EXECUTABLE}"
        -f
        elf64
        -o
        "${OS_USER_EXTENDED_STATE_ASSEMBLY_OBJECT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/extended_state.asm"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/extended_state.asm"
    COMMENT "汇编 Ring 3 x87/SSE2 线程现场验收辅助代码"
    VERBATIM
)
add_custom_command(
    OUTPUT "${OS_USER_SYSTEM_CALL_ASSEMBLY_OBJECT}"
    COMMAND
        "${CMAKE_COMMAND}"
        -E
        make_directory
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND
        "${OS_USER_NASM_EXECUTABLE}"
        -f
        elf64
        -o
        "${OS_USER_SYSTEM_CALL_ASSEMBLY_OBJECT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/system_call.asm"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/system_call.asm"
    COMMENT "汇编 Ring 3 INT 0x80 系统调用桩"
    VERBATIM
)

set(
    OS_USER_FREESTANDING_COMPILE_OPTIONS
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
    -mcmodel=small
    -mno-mmx
    -mno-red-zone
    -mno-sse
    -mno-sse2
    -nostdinc++
)

function(
    os_add_user_program
    OS_USER_TARGET
    OS_USER_PROGRAM_SOURCE
    OS_USER_OUTPUT_PATH
    OS_USER_LINK_COMMENT
)
    set(OS_USER_OBJECT_TARGET "${OS_USER_TARGET}_objects")
    add_library(
        ${OS_USER_OBJECT_TARGET}
        OBJECT
            src/extended_state.cpp
            src/freestanding_memory.cpp
            src/system_call.cpp
            "${OS_USER_PROGRAM_SOURCE}"
            ${ARGN}
    )
    target_link_libraries(${OS_USER_OBJECT_TARGET} PRIVATE os_system_abi)
    target_include_directories(
        ${OS_USER_OBJECT_TARGET}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_options(
        ${OS_USER_OBJECT_TARGET}
        PRIVATE
            ${OS_USER_FREESTANDING_COMPILE_OPTIONS}
    )

    add_custom_command(
        OUTPUT "${OS_USER_OUTPUT_PATH}"
        COMMAND
            "${OS_USER_LLD_EXECUTABLE}"
            -m
            elf_x86_64
            -T
            "${OS_USER_LINKER_SCRIPT}"
            --build-id=none
            -z
            max-page-size=${OS_USER_MAXIMUM_PAGE_SIZE_BYTES}
            -o
            "${OS_USER_OUTPUT_PATH}"
            $<TARGET_OBJECTS:${OS_USER_OBJECT_TARGET}>
            "${OS_USER_SYSTEM_CALL_ASSEMBLY_OBJECT}"
            "${OS_USER_EXTENDED_STATE_ASSEMBLY_OBJECT}"
        DEPENDS
            ${OS_USER_OBJECT_TARGET}
            $<TARGET_OBJECTS:${OS_USER_OBJECT_TARGET}>
            "${OS_USER_SYSTEM_CALL_ASSEMBLY_OBJECT}"
            "${OS_USER_EXTENDED_STATE_ASSEMBLY_OBJECT}"
            "${OS_USER_LINKER_SCRIPT}"
        COMMENT "${OS_USER_LINK_COMMENT}"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
    add_custom_target(${OS_USER_TARGET} ALL DEPENDS "${OS_USER_OUTPUT_PATH}")
endfunction()

set(
    OS_USER_SMOKE_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_smoke.elf"
    CACHE INTERNAL
    "Ring 3 系统调用验收程序"
)
set(
    OS_USER_INVALID_OPCODE_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_invalid_opcode.elf"
    CACHE INTERNAL
    "Ring 3 非法指令隔离程序"
)
set(
    OS_USER_PAGE_FAULT_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_page_fault.elf"
    CACHE INTERNAL
    "Ring 3 页故障隔离程序"
)
set(
    OS_USER_SCHEDULER_WORKER_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_scheduler_worker.elf"
    CACHE INTERNAL
    "Ring 3 进程调度与地址空间隔离验收程序"
)
set(
    OS_USER_IPC_PRODUCER_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_ipc_producer.elf"
    CACHE INTERNAL
    "Ring 3 有界管道生产者验收程序"
)
set(
    OS_USER_IPC_CONSUMER_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_ipc_consumer.elf"
    CACHE INTERNAL
    "Ring 3 有界管道消费者验收程序"
)
set(
    OS_USER_SHELL_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_shell.elf"
    CACHE INTERNAL
    "Ring 3 交互式 Shell 用户环境"
)

os_add_user_program(
    os_user_smoke_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/smoke.cpp"
    "${OS_USER_SMOKE_ELF_PATH}"
    "链接 Ring 3 系统调用验收 ELF64 程序"
)
os_add_user_program(
    os_user_invalid_opcode_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/invalid_opcode.cpp"
    "${OS_USER_INVALID_OPCODE_ELF_PATH}"
    "链接 Ring 3 非法指令隔离 ELF64 程序"
)
os_add_user_program(
    os_user_page_fault_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/page_fault.cpp"
    "${OS_USER_PAGE_FAULT_ELF_PATH}"
    "链接 Ring 3 页故障隔离 ELF64 程序"
)
os_add_user_program(
    os_user_scheduler_worker_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/scheduler_worker.cpp"
    "${OS_USER_SCHEDULER_WORKER_ELF_PATH}"
    "链接 Ring 3 进程调度与地址空间隔离 ELF64 程序"
)
os_add_user_program(
    os_user_ipc_producer_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/ipc_producer.cpp"
    "${OS_USER_IPC_PRODUCER_ELF_PATH}"
    "链接 Ring 3 有界管道生产者 ELF64 程序"
)
os_add_user_program(
    os_user_ipc_consumer_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/ipc_consumer.cpp"
    "${OS_USER_IPC_CONSUMER_ELF_PATH}"
    "链接 Ring 3 有界管道消费者 ELF64 程序"
)
os_add_user_program(
    os_user_shell_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/shell_entry.cpp"
    "${OS_USER_SHELL_ELF_PATH}"
    "链接 Ring 3 交互式 Shell ELF64 程序"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shell.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shell_parser.cpp"
)
