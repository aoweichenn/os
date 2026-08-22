find_program(OS_USER_CLANG_EXECUTABLE NAMES clang++ REQUIRED)
find_program(OS_USER_LLD_EXECUTABLE NAMES ld.lld REQUIRED)
find_program(OS_USER_NASM_EXECUTABLE NAMES nasm REQUIRED)

add_library(
    os_user_shell_model_host
    STATIC
        src/shell_parser.cpp
        src/shell_execution.cpp
        src/shell_environment.cpp
        src/shell_glob.cpp
        src/shell_line_editor.cpp
        include/os/user/shell_parser.hpp
        include/os/user/shell_execution.hpp
        include/os/user/shell_environment.hpp
        include/os/user/shell_glob.hpp
        include/os/user/shell_line_editor.hpp
)

add_library(
    os_user_heap_model_host
    STATIC
        src/user_heap.cpp
        include/os/user/user_heap.hpp
)
target_include_directories(
    os_user_heap_model_host
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(
    os_user_heap_model_host
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
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
            src/synchronization.cpp
            src/system_call.cpp
            src/thread.cpp
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
set(
    OS_USER_CORE_TOOL_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_core_tool.elf"
    CACHE INTERNAL
    "Ring 3 外置核心工具多调用程序"
)
set(
    OS_USER_INIT_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_init.elf"
    CACHE INTERNAL
    "磁盘加载的 PID 1 用户程序"
)
set(
    OS_USER_ORPHAN_PARENT_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_orphan_parent.elf"
    CACHE INTERNAL
    "孤儿进程重设父进程验收程序"
)
set(
    OS_USER_ORPHAN_CHILD_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_orphan_child.elf"
    CACHE INTERNAL
    "孤儿进程收养验收程序"
)
set(
    OS_USER_ARGUMENT_PROBE_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_argument_probe.elf"
    CACHE INTERNAL
    "128 KiB 参数与环境验收程序"
)
set(
    OS_USER_EXEC_PROBE_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_exec_probe.elf"
    CACHE INTERNAL
    "原子 exec 失败与成功路径验收程序"
)
set(
    OS_USER_EXEC_TARGET_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_exec_target.elf"
    CACHE INTERNAL
    "原子 exec 成功目标程序"
)
set(
    OS_USER_FILE_SYSTEM_PROBE_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_file_system_probe.elf"
    CACHE INTERNAL
    "rootfs 持久化验收程序"
)
set(
    OS_USER_MEMORY_PROBE_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_memory_probe.elf"
    CACHE INTERNAL
    "匿名 VMA、按需分页、栈增长和用户堆验收程序"
)
set(
    OS_USER_OOM_PROBE_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_oom_probe.elf"
    CACHE INTERNAL
    "OOM 受害者选择与回收验收程序"
)
set(
    OS_USER_FORK_PROBE_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_fork_probe.elf"
    CACHE INTERNAL
    "fork、COW、继承与 exec/wait 生命周期验收程序"
)
set(
    OS_USER_THREAD_PROBE_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_thread_probe.elf"
    CACHE INTERNAL
    "用户 Thread、FS-base TLS 与 private futex 验收程序"
)
set(
    OS_USER_TIME_PROBE_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_time_probe.elf"
    CACHE INTERNAL
    "用户单调时钟、睡眠与超时等待验收程序"
)
set(
    OS_USER_SIGNAL_PROBE_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_signal_probe.elf"
    CACHE INTERNAL
    "用户信号、进程组、阻塞重启与 sigreturn 隔离验收程序"
)
set(
    OS_USER_MEMORY_GUARD_PROBE_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_memory_guard_probe.elf"
    CACHE INTERNAL
    "用户栈 guard page 隔离验收程序"
)
set(
    OS_USER_MEMORY_PROTECTION_PROBE_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_memory_protection_probe.elf"
    CACHE INTERNAL
    "匿名 VMA 写保护隔离验收程序"
)
set(
    OS_USER_TOOL_PROBE_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_tool_probe.elf"
    CACHE INTERNAL
    "47 个独立用户工具 ELF 路径与 inode 验收程序"
)
set(
    OS_USER_SECURITY_PROBE_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_security_probe.elf"
    CACHE INTERNAL
    "本地身份、权限、链接与资源限制验收程序"
)
set(
    OS_USER_SECURITY_EXEC_TARGET_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_security_exec_target.elf"
    CACHE INTERNAL
    "fork/exec 凭据继承与 set-ID 验收目标"
)
set(
    OS_USER_TRUNCATED_ELF_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/user_truncated.elf"
    CACHE INTERNAL
    "截断到 ELF64 头之前的拒绝验收镜像"
)
set(OS_USER_TRUNCATED_ELF_SIZE_BYTES 63)

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
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shell_execution.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shell_environment.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shell_glob.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shell_line_editor.cpp"
)
os_add_user_program(
    os_user_core_tool_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/core_tool.cpp"
    "${OS_USER_CORE_TOOL_ELF_PATH}"
    "链接 Ring 3 外置核心工具多调用 ELF64 程序"
)
os_add_user_program(
    os_user_init_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/init.cpp"
    "${OS_USER_INIT_ELF_PATH}"
    "链接磁盘加载的 PID 1 ELF64 程序"
)
os_add_user_program(
    os_user_orphan_parent_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/orphan_parent.cpp"
    "${OS_USER_ORPHAN_PARENT_ELF_PATH}"
    "链接孤儿进程重设父进程验收 ELF64 程序"
)
os_add_user_program(
    os_user_orphan_child_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/orphan_child.cpp"
    "${OS_USER_ORPHAN_CHILD_ELF_PATH}"
    "链接孤儿进程收养验收 ELF64 程序"
)
os_add_user_program(
    os_user_argument_probe_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/argument_probe.cpp"
    "${OS_USER_ARGUMENT_PROBE_ELF_PATH}"
    "链接 128 KiB 参数与环境验收 ELF64 程序"
)
os_add_user_program(
    os_user_exec_probe_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/exec_probe.cpp"
    "${OS_USER_EXEC_PROBE_ELF_PATH}"
    "链接原子 exec 验收 ELF64 程序"
)
os_add_user_program(
    os_user_exec_target_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/exec_target.cpp"
    "${OS_USER_EXEC_TARGET_ELF_PATH}"
    "链接原子 exec 目标 ELF64 程序"
)
os_add_user_program(
    os_user_file_system_probe_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/fs_probe.cpp"
    "${OS_USER_FILE_SYSTEM_PROBE_ELF_PATH}"
    "链接 rootfs 持久化验收 ELF64 程序"
)
os_add_user_program(
    os_user_memory_probe_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/memory_probe.cpp"
    "${OS_USER_MEMORY_PROBE_ELF_PATH}"
    "链接匿名 VMA、按需分页、栈增长和用户堆验收 ELF64 程序"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/user_heap.cpp"
)
os_add_user_program(
    os_user_oom_probe_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/oom_probe.cpp"
    "${OS_USER_OOM_PROBE_ELF_PATH}"
    "链接 OOM 受害者选择与回收验收 ELF64 程序"
)
os_add_user_program(
    os_user_fork_probe_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/fork_probe.cpp"
    "${OS_USER_FORK_PROBE_ELF_PATH}"
    "链接 fork 与写时复制验收 ELF64 程序"
)
os_add_user_program(
    os_user_thread_probe_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/thread_probe.cpp"
    "${OS_USER_THREAD_PROBE_ELF_PATH}"
    "链接用户 Thread、FS-base TLS 与 private futex 验收 ELF64 程序"
)
os_add_user_program(
    os_user_time_probe_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/time_probe.cpp"
    "${OS_USER_TIME_PROBE_ELF_PATH}"
    "链接用户单调时钟、睡眠与超时等待 ELF64 程序"
)
os_add_user_program(
    os_user_signal_probe_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/signal_probe.cpp"
    "${OS_USER_SIGNAL_PROBE_ELF_PATH}"
    "链接用户信号、进程组、阻塞重启与 sigreturn 隔离 ELF64 程序"
)
os_add_user_program(
    os_user_memory_guard_probe_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/memory_guard_probe.cpp"
    "${OS_USER_MEMORY_GUARD_PROBE_ELF_PATH}"
    "链接用户栈 guard page 隔离 ELF64 程序"
)
os_add_user_program(
    os_user_memory_protection_probe_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/memory_protection_probe.cpp"
    "${OS_USER_MEMORY_PROTECTION_PROBE_ELF_PATH}"
    "链接匿名 VMA 写保护隔离 ELF64 程序"
)
os_add_user_program(
    os_user_tool_probe_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/tool_probe.cpp"
    "${OS_USER_TOOL_PROBE_ELF_PATH}"
    "链接 47 个独立用户工具 ELF 路径验收程序"
)
os_add_user_program(
    os_user_security_probe_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/security_probe.cpp"
    "${OS_USER_SECURITY_PROBE_ELF_PATH}"
    "链接本地身份、权限、链接与资源限制验收程序"
)
os_add_user_program(
    os_user_security_exec_target_elf
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/security_exec_target.cpp"
    "${OS_USER_SECURITY_EXEC_TARGET_ELF_PATH}"
    "链接 fork/exec 凭据继承与 set-ID 验收目标"
)

add_custom_command(
    OUTPUT "${OS_USER_TRUNCATED_ELF_PATH}"
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        copy-file-prefix
        "${OS_USER_EXEC_TARGET_ELF_PATH}"
        "${OS_USER_TRUNCATED_ELF_PATH}"
        "${OS_USER_TRUNCATED_ELF_SIZE_BYTES}"
    DEPENDS
        os_user_exec_target_elf
        "${OS_USER_EXEC_TARGET_ELF_PATH}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
    COMMENT "生成精确截断的 ELF64 拒绝验收镜像"
    VERBATIM
)
add_custom_target(
    os_user_truncated_elf
    ALL
    DEPENDS "${OS_USER_TRUNCATED_ELF_PATH}"
)
add_dependencies(os_user_truncated_elf os_user_exec_target_elf)
