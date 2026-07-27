set(
    OS_TEST_SUPPORT_SOURCES
    support/cpp/test_context.cpp
)

add_library(os_test_support STATIC ${OS_TEST_SUPPORT_SOURCES})
target_include_directories(
    os_test_support
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/support/cpp"
)
target_compile_options(
    os_test_support
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
)

function(os_add_foundation_test OS_TEST_TARGET OS_TEST_SOURCE OS_TEST_LABEL)
    add_executable(${OS_TEST_TARGET} ${OS_TEST_SOURCE})
    target_link_libraries(
        ${OS_TEST_TARGET}
        PRIVATE
            os_foundation_host
            os_test_support
    )
    target_compile_options(
        ${OS_TEST_TARGET}
        PRIVATE
            ${OS_COMMON_WARNING_FLAGS}
    )
    add_test(NAME ${OS_TEST_TARGET} COMMAND ${OS_TEST_TARGET})
    set_tests_properties(
        ${OS_TEST_TARGET}
        PROPERTIES
            LABELS "${OS_TEST_LABEL}"
    )
endfunction()

os_add_foundation_test(
    os_foundation_unit_tests
    unit/foundation/address_range_test.cpp
    "unit"
)

os_add_foundation_test(
    os_foundation_resource_lifecycle_primitives_unit_tests
    unit/foundation/resource_lifecycle_primitives_test.cpp
    "unit"
)

os_add_foundation_test(
    os_foundation_integration_tests
    integration/boot/boot_memory_layout_test.cpp
    "integration"
)

os_add_foundation_test(
    os_kernel_handoff_layout_integration_tests
    integration/boot/kernel_handoff_layout_test.cpp
    "integration"
)

os_add_foundation_test(
    os_foundation_randomized_tests
    randomized/foundation/address_range_randomized_test.cpp
    "randomized"
)

os_add_foundation_test(
    os_foundation_resource_lifecycle_primitives_randomized_tests
    randomized/foundation/resource_lifecycle_primitives_randomized_test.cpp
    "randomized"
)

add_executable(
    os_kernel_boot_info_unit_tests
    unit/kernel/boot_info_test.cpp
)
target_link_libraries(
    os_kernel_boot_info_unit_tests
    PRIVATE
        os_kernel_boot_protocol_host
        os_test_support
)
target_compile_options(
    os_kernel_boot_info_unit_tests
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
)
add_test(
    NAME os_kernel_boot_info_unit_tests
    COMMAND os_kernel_boot_info_unit_tests
)
set_tests_properties(
    os_kernel_boot_info_unit_tests
    PROPERTIES
        LABELS "unit"
)

add_executable(
    os_kernel_descriptor_layout_unit_tests
    unit/kernel/descriptor_layout_test.cpp
)
target_link_libraries(
    os_kernel_descriptor_layout_unit_tests
    PRIVATE
        os_kernel_architecture_model_host
        os_test_support
)
target_compile_options(
    os_kernel_descriptor_layout_unit_tests
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
)
add_test(
    NAME os_kernel_descriptor_layout_unit_tests
    COMMAND os_kernel_descriptor_layout_unit_tests
)
set_tests_properties(
    os_kernel_descriptor_layout_unit_tests
    PROPERTIES
        LABELS "unit"
)

add_executable(
    os_kernel_descriptor_layout_randomized_tests
    randomized/kernel/descriptor_layout_randomized_test.cpp
)
target_link_libraries(
    os_kernel_descriptor_layout_randomized_tests
    PRIVATE
        os_kernel_architecture_model_host
        os_test_support
)
target_compile_options(
    os_kernel_descriptor_layout_randomized_tests
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
)
add_test(
    NAME os_kernel_descriptor_layout_randomized_tests
    COMMAND os_kernel_descriptor_layout_randomized_tests
)
set_tests_properties(
    os_kernel_descriptor_layout_randomized_tests
    PROPERTIES
        LABELS "randomized"
)

add_executable(
    os_kernel_extended_state_layout_unit_tests
    unit/kernel/extended_state_layout_test.cpp
)
target_link_libraries(
    os_kernel_extended_state_layout_unit_tests
    PRIVATE
        os_kernel_architecture_model_host
        os_test_support
)
target_compile_options(
    os_kernel_extended_state_layout_unit_tests
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
)
add_test(
    NAME os_kernel_extended_state_layout_unit_tests
    COMMAND os_kernel_extended_state_layout_unit_tests
)
set_tests_properties(
    os_kernel_extended_state_layout_unit_tests
    PROPERTIES
        LABELS "unit"
)

function(
    os_add_kernel_architecture_test
    OS_TEST_TARGET
    OS_TEST_SOURCE
    OS_TEST_LABEL
)
    add_executable(${OS_TEST_TARGET} ${OS_TEST_SOURCE})
    target_link_libraries(
        ${OS_TEST_TARGET}
        PRIVATE
            os_kernel_architecture_model_host
            os_test_support
    )
    target_compile_options(
        ${OS_TEST_TARGET}
        PRIVATE
            ${OS_COMMON_WARNING_FLAGS}
    )
    add_test(NAME ${OS_TEST_TARGET} COMMAND ${OS_TEST_TARGET})
    set_tests_properties(
        ${OS_TEST_TARGET}
        PROPERTIES
            LABELS "${OS_TEST_LABEL}"
    )
endfunction()

os_add_kernel_architecture_test(
    os_kernel_processor_features_unit_tests
    unit/kernel/processor_features_test.cpp
    "unit"
)
os_add_kernel_architecture_test(
    os_kernel_user_context_unit_tests
    unit/kernel/user_context_test.cpp
    "unit"
)
os_add_kernel_architecture_test(
    os_kernel_native_system_call_integration_tests
    integration/boot/kernel_native_system_call_test.cpp
    "integration"
)
os_add_kernel_architecture_test(
    os_kernel_user_context_randomized_tests
    randomized/kernel/user_context_randomized_test.cpp
    "randomized"
)

function(os_add_kernel_memory_test OS_TEST_TARGET OS_TEST_SOURCE OS_TEST_LABEL)
    add_executable(${OS_TEST_TARGET} ${OS_TEST_SOURCE})
    target_link_libraries(
        ${OS_TEST_TARGET}
        PRIVATE
            os_kernel_memory_model_host
            os_test_support
    )
    target_compile_options(
        ${OS_TEST_TARGET}
        PRIVATE
            ${OS_COMMON_WARNING_FLAGS}
    )
    add_test(NAME ${OS_TEST_TARGET} COMMAND ${OS_TEST_TARGET})
    set_tests_properties(
        ${OS_TEST_TARGET}
        PROPERTIES
            LABELS "${OS_TEST_LABEL}"
    )
endfunction()

add_library(
    os_kernel_stack_test_support
    STATIC
        support/cpp/kernel_stack_test_environment.cpp
        support/cpp/kernel_stack_test_environment.hpp
)
target_include_directories(
    os_kernel_stack_test_support
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/support/cpp"
)
target_link_libraries(
    os_kernel_stack_test_support
    PUBLIC
        os_kernel_memory_model_host
)
target_compile_options(
    os_kernel_stack_test_support
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
)

add_library(
    os_page_table_test_support
    STATIC
        support/cpp/page_table_test_environment.cpp
        support/cpp/page_table_test_environment.hpp
)
target_include_directories(
    os_page_table_test_support
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/support/cpp"
)
target_link_libraries(
    os_page_table_test_support
    PUBLIC
        os_kernel_memory_model_host
)
target_compile_options(
    os_page_table_test_support
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
)

function(os_add_page_table_test OS_TEST_TARGET OS_TEST_SOURCE OS_TEST_LABEL)
    add_executable(${OS_TEST_TARGET} ${OS_TEST_SOURCE})
    target_link_libraries(
        ${OS_TEST_TARGET}
        PRIVATE
            os_page_table_test_support
            os_test_support
    )
    target_compile_options(
        ${OS_TEST_TARGET}
        PRIVATE
            ${OS_COMMON_WARNING_FLAGS}
    )
    add_test(NAME ${OS_TEST_TARGET} COMMAND ${OS_TEST_TARGET})
    set_tests_properties(
        ${OS_TEST_TARGET}
        PROPERTIES
            LABELS "${OS_TEST_LABEL}"
    )
endfunction()

function(os_add_kernel_stack_test OS_TEST_TARGET OS_TEST_SOURCE OS_TEST_LABEL)
    add_executable(${OS_TEST_TARGET} ${OS_TEST_SOURCE})
    target_link_libraries(
        ${OS_TEST_TARGET}
        PRIVATE
            os_kernel_stack_test_support
            os_test_support
    )
    target_compile_options(
        ${OS_TEST_TARGET}
        PRIVATE
            ${OS_COMMON_WARNING_FLAGS}
    )
    add_test(NAME ${OS_TEST_TARGET} COMMAND ${OS_TEST_TARGET})
    set_tests_properties(
        ${OS_TEST_TARGET}
        PROPERTIES
            LABELS "${OS_TEST_LABEL}"
    )
endfunction()

os_add_kernel_memory_test(
    os_kernel_physical_memory_map_unit_tests
    unit/kernel/physical_memory_map_test.cpp
    "unit"
)
os_add_kernel_memory_test(
    os_kernel_physical_frame_allocator_unit_tests
    unit/kernel/physical_frame_allocator_test.cpp
    "unit"
)
os_add_kernel_memory_test(
    os_kernel_buddy_frame_allocator_unit_tests
    unit/kernel/buddy_frame_allocator_test.cpp
    "unit"
)
os_add_kernel_memory_test(
    os_kernel_heap_and_page_layout_unit_tests
    unit/kernel/heap_and_page_layout_test.cpp
    "unit"
)
os_add_kernel_memory_test(
    os_kernel_heap_unit_tests
    unit/kernel/kernel_heap_test.cpp
    "unit"
)
os_add_kernel_memory_test(
    os_kernel_type_cache_unit_tests
    unit/kernel/kernel_type_cache_test.cpp
    "unit"
)
os_add_kernel_memory_test(
    os_kernel_virtual_address_allocator_unit_tests
    unit/kernel/kernel_virtual_address_allocator_test.cpp
    "unit"
)
os_add_kernel_memory_test(
    os_kernel_resource_snapshot_unit_tests
    unit/kernel/resource_snapshot_test.cpp
    "unit"
)
os_add_page_table_test(
    os_kernel_page_table_reclamation_unit_tests
    unit/kernel/page_table_reclamation_test.cpp
    "unit"
)
os_add_kernel_memory_test(
    os_kernel_memory_bootstrap_integration_tests
    integration/boot/kernel_memory_bootstrap_test.cpp
    "integration"
)
os_add_kernel_memory_test(
    os_kernel_buddy_frame_allocator_lifecycle_integration_tests
    integration/boot/buddy_frame_allocator_lifecycle_test.cpp
    "integration"
)
os_add_kernel_memory_test(
    os_kernel_heap_lifecycle_integration_tests
    integration/boot/kernel_heap_lifecycle_test.cpp
    "integration"
)
os_add_kernel_memory_test(
    os_kernel_type_cache_lifecycle_integration_tests
    integration/boot/kernel_type_cache_lifecycle_test.cpp
    "integration"
)
os_add_kernel_memory_test(
    os_kernel_virtual_address_mapping_lifecycle_integration_tests
    integration/boot/kernel_virtual_address_mapping_lifecycle_test.cpp
    "integration"
)
os_add_page_table_test(
    os_kernel_page_table_reclamation_lifecycle_integration_tests
    integration/boot/page_table_reclamation_lifecycle_test.cpp
    "integration"
)
os_add_kernel_memory_test(
    os_kernel_memory_management_randomized_tests
    randomized/kernel/memory_management_randomized_test.cpp
    "randomized"
)
os_add_kernel_memory_test(
    os_kernel_buddy_frame_allocator_randomized_tests
    randomized/kernel/buddy_frame_allocator_randomized_test.cpp
    "randomized"
)
os_add_kernel_memory_test(
    os_kernel_heap_randomized_tests
    randomized/kernel/kernel_heap_randomized_test.cpp
    "randomized"
)
os_add_kernel_memory_test(
    os_kernel_type_cache_randomized_tests
    randomized/kernel/kernel_type_cache_randomized_test.cpp
    "randomized"
)
os_add_kernel_memory_test(
    os_kernel_virtual_address_allocator_randomized_tests
    randomized/kernel/kernel_virtual_address_allocator_randomized_test.cpp
    "randomized"
)
os_add_page_table_test(
    os_kernel_page_table_reclamation_randomized_tests
    randomized/kernel/page_table_reclamation_randomized_test.cpp
    "randomized"
)
os_add_kernel_stack_test(
    os_kernel_stack_manager_unit_tests
    unit/kernel/kernel_stack_manager_test.cpp
    "unit"
)
os_add_kernel_stack_test(
    os_kernel_stack_lifecycle_integration_tests
    integration/boot/kernel_stack_lifecycle_test.cpp
    "integration"
)
os_add_kernel_stack_test(
    os_kernel_resource_snapshot_lifecycle_integration_tests
    integration/boot/resource_snapshot_lifecycle_test.cpp
    "integration"
)
os_add_kernel_stack_test(
    os_kernel_stack_manager_randomized_tests
    randomized/kernel/kernel_stack_manager_randomized_test.cpp
    "randomized"
)

function(os_add_kernel_device_test OS_TEST_TARGET OS_TEST_SOURCE OS_TEST_LABEL)
    add_executable(${OS_TEST_TARGET} ${OS_TEST_SOURCE})
    target_link_libraries(
        ${OS_TEST_TARGET}
        PRIVATE
            os_kernel_device_model_host
            os_test_support
    )
    target_compile_options(
        ${OS_TEST_TARGET}
        PRIVATE
            ${OS_COMMON_WARNING_FLAGS}
    )
    add_test(NAME ${OS_TEST_TARGET} COMMAND ${OS_TEST_TARGET})
    set_tests_properties(
        ${OS_TEST_TARGET}
        PROPERTIES
            LABELS "${OS_TEST_LABEL}"
    )
endfunction()

os_add_kernel_device_test(
    os_kernel_device_model_unit_tests
    unit/kernel/device_model_test.cpp
    "unit"
)
os_add_kernel_device_test(
    os_kernel_device_bootstrap_integration_tests
    integration/boot/kernel_device_bootstrap_test.cpp
    "integration"
)
os_add_kernel_device_test(
    os_kernel_interrupt_device_randomized_tests
    randomized/kernel/interrupt_device_randomized_test.cpp
    "randomized"
)

function(os_add_kernel_user_test OS_TEST_TARGET OS_TEST_SOURCE OS_TEST_LABEL)
    add_executable(${OS_TEST_TARGET} ${OS_TEST_SOURCE})
    target_link_libraries(
        ${OS_TEST_TARGET}
        PRIVATE
            os_kernel_user_model_host
            os_kernel_architecture_model_host
            os_system_abi
            os_test_support
    )
    target_compile_options(
        ${OS_TEST_TARGET}
        PRIVATE
            ${OS_COMMON_WARNING_FLAGS}
    )
    add_test(NAME ${OS_TEST_TARGET} COMMAND ${OS_TEST_TARGET})
    set_tests_properties(
        ${OS_TEST_TARGET}
        PROPERTIES
            LABELS "${OS_TEST_LABEL}"
    )
endfunction()

os_add_kernel_user_test(
    os_kernel_user_elf_unit_tests
    unit/kernel/user_elf_test.cpp
    "unit"
)
os_add_kernel_user_test(
    os_kernel_user_boundary_integration_tests
    integration/boot/kernel_user_boundary_test.cpp
    "integration"
)
os_add_kernel_user_test(
    os_kernel_user_elf_randomized_tests
    randomized/kernel/user_elf_randomized_test.cpp
    "randomized"
)

function(os_add_kernel_process_test OS_TEST_TARGET OS_TEST_SOURCE OS_TEST_LABEL)
    add_executable(${OS_TEST_TARGET} ${OS_TEST_SOURCE})
    target_link_libraries(
        ${OS_TEST_TARGET}
        PRIVATE
            os_kernel_process_model_host
            os_test_support
    )
    target_compile_options(
        ${OS_TEST_TARGET}
        PRIVATE
            ${OS_COMMON_WARNING_FLAGS}
    )
    add_test(NAME ${OS_TEST_TARGET} COMMAND ${OS_TEST_TARGET})
    set_tests_properties(
        ${OS_TEST_TARGET}
        PROPERTIES
            LABELS "${OS_TEST_LABEL}"
    )
endfunction()

os_add_kernel_process_test(
    os_kernel_thread_scheduler_unit_tests
    unit/kernel/thread_scheduler_test.cpp
    "unit"
)
os_add_kernel_process_test(
    os_kernel_process_tree_unit_tests
    unit/kernel/process_tree_test.cpp
    "unit"
)
os_add_kernel_process_test(
    os_kernel_program_arguments_unit_tests
    unit/kernel/program_arguments_test.cpp
    "unit"
)
os_add_kernel_process_test(
    os_kernel_thread_scheduling_integration_tests
    integration/boot/kernel_thread_scheduling_test.cpp
    "integration"
)
os_add_kernel_process_test(
    os_kernel_process_lifecycle_integration_tests
    integration/boot/kernel_process_lifecycle_test.cpp
    "integration"
)
os_add_kernel_process_test(
    os_kernel_thread_scheduler_randomized_tests
    randomized/kernel/thread_scheduler_randomized_test.cpp
    "randomized"
)
os_add_kernel_process_test(
    os_kernel_process_models_randomized_tests
    randomized/kernel/process_models_randomized_test.cpp
    "randomized"
)

function(os_add_kernel_ipc_test OS_TEST_TARGET OS_TEST_SOURCE OS_TEST_LABEL)
    add_executable(${OS_TEST_TARGET} ${OS_TEST_SOURCE})
    target_link_libraries(
        ${OS_TEST_TARGET}
        PRIVATE
            os_kernel_ipc_model_host
            os_test_support
    )
    target_compile_options(
        ${OS_TEST_TARGET}
        PRIVATE
            ${OS_COMMON_WARNING_FLAGS}
    )
    add_test(NAME ${OS_TEST_TARGET} COMMAND ${OS_TEST_TARGET})
    set_tests_properties(
        ${OS_TEST_TARGET}
        PROPERTIES
            LABELS "${OS_TEST_LABEL}"
    )
endfunction()

os_add_kernel_ipc_test(
    os_kernel_pipe_unit_tests
    unit/kernel/pipe_test.cpp
    "unit"
)
os_add_kernel_ipc_test(
    os_kernel_pipe_randomized_tests
    randomized/kernel/pipe_randomized_test.cpp
    "randomized"
)

function(
    os_add_kernel_user_environment_test
    OS_TEST_TARGET
    OS_TEST_SOURCE
    OS_TEST_LABEL
)
    add_executable(${OS_TEST_TARGET} ${OS_TEST_SOURCE})
    target_link_libraries(
        ${OS_TEST_TARGET}
        PRIVATE
            os_kernel_user_environment_model_host
            os_test_support
    )
    target_compile_options(
        ${OS_TEST_TARGET}
        PRIVATE
            ${OS_COMMON_WARNING_FLAGS}
    )
    add_test(NAME ${OS_TEST_TARGET} COMMAND ${OS_TEST_TARGET})
    set_tests_properties(
        ${OS_TEST_TARGET}
        PROPERTIES
            LABELS "${OS_TEST_LABEL}"
    )
endfunction()

os_add_kernel_user_environment_test(
    os_kernel_console_input_unit_tests
    unit/kernel/console_input_test.cpp
    "unit"
)
function(
    os_add_kernel_object_model_test
    OS_TEST_TARGET
    OS_TEST_SOURCE
    OS_TEST_LABEL
)
    add_executable(${OS_TEST_TARGET} ${OS_TEST_SOURCE})
    target_link_libraries(
        ${OS_TEST_TARGET}
        PRIVATE
            os_kernel_object_model_host
            os_test_support
    )
    target_compile_options(
        ${OS_TEST_TARGET}
        PRIVATE
            ${OS_COMMON_WARNING_FLAGS}
    )
    add_test(NAME ${OS_TEST_TARGET} COMMAND ${OS_TEST_TARGET})
    set_tests_properties(
        ${OS_TEST_TARGET}
        PROPERTIES
            LABELS "${OS_TEST_LABEL}"
    )
endfunction()

os_add_kernel_object_model_test(
    os_kernel_file_table_unit_tests
    unit/kernel/file_table_test.cpp
    "unit"
)
os_add_kernel_object_model_test(
    os_kernel_file_description_lifecycle_integration_tests
    integration/boot/file_description_lifecycle_test.cpp
    "integration"
)
os_add_kernel_object_model_test(
    os_kernel_file_table_capacity_integration_tests
    integration/boot/file_table_capacity_test.cpp
    "integration"
)
os_add_kernel_object_model_test(
    os_kernel_file_table_randomized_tests
    randomized/kernel/file_table_randomized_test.cpp
    "randomized"
)

function(
    os_add_user_shell_test
    OS_TEST_TARGET
    OS_TEST_SOURCE
    OS_TEST_LABEL
)
    add_executable(${OS_TEST_TARGET} ${OS_TEST_SOURCE})
    target_link_libraries(
        ${OS_TEST_TARGET}
        PRIVATE
            os_user_shell_model_host
            os_test_support
    )
    target_compile_options(
        ${OS_TEST_TARGET}
        PRIVATE
            ${OS_COMMON_WARNING_FLAGS}
    )
    add_test(NAME ${OS_TEST_TARGET} COMMAND ${OS_TEST_TARGET})
    set_tests_properties(
        ${OS_TEST_TARGET}
        PROPERTIES
            LABELS "${OS_TEST_LABEL}"
    )
endfunction()

os_add_user_shell_test(
    os_user_shell_parser_unit_tests
    unit/user/shell_parser_test.cpp
    "unit"
)
os_add_user_shell_test(
    os_user_shell_parser_randomized_tests
    randomized/user/shell_parser_randomized_test.cpp
    "randomized"
)

function(os_add_kernel_file_system_test OS_TEST_TARGET OS_TEST_SOURCE OS_TEST_LABEL)
    add_executable(${OS_TEST_TARGET} ${OS_TEST_SOURCE})
    target_link_libraries(
        ${OS_TEST_TARGET}
        PRIVATE
            os_kernel_file_system_model_host
            os_test_support
    )
    target_compile_options(
        ${OS_TEST_TARGET}
        PRIVATE
            ${OS_COMMON_WARNING_FLAGS}
    )
    add_test(NAME ${OS_TEST_TARGET} COMMAND ${OS_TEST_TARGET})
    set_tests_properties(
        ${OS_TEST_TARGET}
        PROPERTIES
            LABELS "${OS_TEST_LABEL}"
    )
endfunction()

os_add_kernel_file_system_test(
    os_kernel_file_system_format_unit_tests
    unit/kernel/file_system_format_test.cpp
    "unit"
)
os_add_kernel_file_system_test(
    os_kernel_root_file_system_format_unit_tests
    unit/kernel/root_file_system_format_test.cpp
    "unit"
)
os_add_kernel_file_system_test(
    os_kernel_vfs_unit_tests
    unit/kernel/vfs_test.cpp
    "unit"
)
os_add_kernel_file_system_test(
    os_kernel_file_system_lifecycle_integration_tests
    integration/boot/kernel_file_system_lifecycle_test.cpp
    "integration"
)
os_add_kernel_file_system_test(
    os_kernel_vfs_backend_contract_integration_tests
    integration/boot/vfs_backend_contract_test.cpp
    "integration"
)
os_add_kernel_file_system_test(
    os_kernel_root_file_system_integration_tests
    integration/boot/root_file_system_test.cpp
    "integration"
)
os_add_kernel_file_system_test(
    os_kernel_root_file_system_capacity_integration_tests
    integration/boot/root_file_system_capacity_test.cpp
    "integration"
)
target_sources(
    os_kernel_root_file_system_capacity_integration_tests
    PRIVATE
        support/cpp/root_file_system_capacity_test_support.cpp
)
set_tests_properties(
    os_kernel_root_file_system_capacity_integration_tests
    PROPERTIES
        TIMEOUT 180
)
os_add_kernel_file_system_test(
    os_kernel_file_system_randomized_tests
    randomized/kernel/file_system_randomized_test.cpp
    "randomized"
)
os_add_kernel_file_system_test(
    os_kernel_vfs_namespace_randomized_tests
    randomized/kernel/vfs_namespace_randomized_test.cpp
    "randomized"
)
set(OS_TEST_VFS_NAMESPACE_RANDOMIZED_TIMEOUT_SECONDS 180)
set_tests_properties(
    os_kernel_vfs_namespace_randomized_tests
    PROPERTIES
        TIMEOUT "${OS_TEST_VFS_NAMESPACE_RANDOMIZED_TIMEOUT_SECONDS}"
)

find_package(Threads REQUIRED)
os_add_kernel_ipc_test(
    os_kernel_synchronization_integration_tests
    integration/boot/kernel_synchronization_test.cpp
    "integration"
)
target_link_libraries(
    os_kernel_synchronization_integration_tests
    PRIVATE
        Threads::Threads
)

add_test(
    NAME os_freestanding_symbol_audit
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        audit-elf
        "$<TARGET_FILE:os_foundation_x86_64>"
)
set_tests_properties(
    os_freestanding_symbol_audit
    PROPERTIES
        LABELS "integration"
)

add_test(
    NAME os_kernel_elf_layout
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        audit-kernel-elf
        "${OS_KERNEL_PAYLOAD_ELF_PATH}"
)
set_tests_properties(
    os_kernel_elf_layout
    PROPERTIES
        LABELS "integration"
)

function(os_add_user_elf_audit_test OS_TEST_TARGET OS_USER_ELF_PATH)
    add_test(
        NAME ${OS_TEST_TARGET}
        COMMAND
            "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/os.py"
            audit-user-elf
            "${OS_USER_ELF_PATH}"
    )
    set_tests_properties(
        ${OS_TEST_TARGET}
        PROPERTIES
            LABELS "integration"
    )
endfunction()

os_add_user_elf_audit_test(
    os_user_smoke_elf_layout
    "${OS_USER_SMOKE_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_invalid_opcode_elf_layout
    "${OS_USER_INVALID_OPCODE_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_page_fault_elf_layout
    "${OS_USER_PAGE_FAULT_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_scheduler_worker_elf_layout
    "${OS_USER_SCHEDULER_WORKER_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_ipc_producer_elf_layout
    "${OS_USER_IPC_PRODUCER_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_ipc_consumer_elf_layout
    "${OS_USER_IPC_CONSUMER_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_shell_elf_layout
    "${OS_USER_SHELL_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_init_elf_layout
    "${OS_USER_INIT_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_orphan_parent_elf_layout
    "${OS_USER_ORPHAN_PARENT_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_orphan_child_elf_layout
    "${OS_USER_ORPHAN_CHILD_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_argument_probe_elf_layout
    "${OS_USER_ARGUMENT_PROBE_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_exec_probe_elf_layout
    "${OS_USER_EXEC_PROBE_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_exec_target_elf_layout
    "${OS_USER_EXEC_TARGET_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_fs_probe_elf_layout
    "${OS_USER_FILE_SYSTEM_PROBE_ELF_PATH}"
)

add_test(
    NAME os_qemu_hardware_smoke
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-smoke
        "${OS_EMPTY_FIRMWARE_IMAGE}"
        "${OS_EMPTY_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
)
set_tests_properties(
    os_qemu_hardware_smoke
    PROPERTIES
        LABELS "system"
        TIMEOUT 10
)

add_test(
    NAME os_qemu_rejects_invalid_image_size
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-smoke
        "${OS_EMPTY_FIRMWARE_IMAGE}"
        "${OS_EMPTY_DISK_IMAGE}"
        "${OS_INVALID_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
)
set_tests_properties(
    os_qemu_rejects_invalid_image_size
    PROPERTIES
        LABELS "system;failure-path"
        WILL_FAIL TRUE
)

add_test(
    NAME os_firmware_rom_layout
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        audit-firmware
        "${OS_FIRMWARE_IMAGE}"
)
set_tests_properties(
    os_firmware_rom_layout
    PROPERTIES
        LABELS "integration"
)

add_test(
    NAME os_stage1_disk_layout
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        audit-stage1
        "${OS_BOOT_DISK_IMAGE}"
)
set_tests_properties(
    os_stage1_disk_layout
    PROPERTIES
        LABELS "integration"
)

add_test(
    NAME os_kernel_disk_layout
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        audit-kernel-image
        "${OS_BOOT_DISK_IMAGE}"
)
set_tests_properties(
    os_kernel_disk_layout
    PROPERTIES
        LABELS "integration"
)

add_test(
    NAME os_kernel_disk_rejects_invalid_header
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        audit-kernel-image
        "${OS_KERNEL_INVALID_HEADER_DISK_IMAGE}"
)
set_tests_properties(
    os_kernel_disk_rejects_invalid_header
    PROPERTIES
        LABELS "integration;failure-path"
        WILL_FAIL TRUE
)

add_test(
    NAME os_kernel_disk_rejects_invalid_checksum
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        audit-kernel-image
        "${OS_KERNEL_INVALID_CHECKSUM_DISK_IMAGE}"
)
set_tests_properties(
    os_kernel_disk_rejects_invalid_checksum
    PROPERTIES
        LABELS "integration;failure-path"
        WILL_FAIL TRUE
)

add_test(
    NAME os_kernel_disk_rejects_invalid_elf
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        audit-kernel-image
        "${OS_KERNEL_INVALID_ELF_DISK_IMAGE}"
)
set_tests_properties(
    os_kernel_disk_rejects_invalid_elf
    PROPERTIES
        LABELS "integration;failure-path"
        WILL_FAIL TRUE
)

add_test(
    NAME os_stage1_rejects_invalid_header
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        audit-stage1
        "${OS_STAGE1_INVALID_HEADER_DISK_IMAGE}"
)
set_tests_properties(
    os_stage1_rejects_invalid_header
    PROPERTIES
        LABELS "integration;failure-path"
        WILL_FAIL TRUE
)

set(OS_TEST_QEMU_FUNCTIONAL_GUEST_MEMORY_MEBIBYTES 256)
set(OS_TEST_QEMU_PRIMARY_GUEST_MEMORY_MEBIBYTES 65536)
set(OS_TEST_QEMU_PERSISTENCE_TIMEOUT_SECONDS 60)

add_test(
    NAME os_qemu_functional_smoke
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_BOOT_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --memory-mebibytes
        "${OS_TEST_QEMU_FUNCTIONAL_GUEST_MEMORY_MEBIBYTES}"
        --expected-outcome
        success
)
set_tests_properties(
    os_qemu_functional_smoke
    PROPERTIES
        LABELS "system;functional-smoke"
        TIMEOUT 25
)

add_test(
    NAME os_qemu_native_system_call_unsupported
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_BOOT_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --cpu-model
        "qemu64,-syscall"
        --expected-outcome
        processor-feature-unsupported
)
set_tests_properties(
    os_qemu_native_system_call_unsupported
    PROPERTIES
        LABELS "system;failure-path"
        TIMEOUT 10
)

add_test(
    NAME os_qemu_stage1_load_success
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_BOOT_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --memory-mebibytes
        "${OS_TEST_QEMU_PRIMARY_GUEST_MEMORY_MEBIBYTES}"
        --expected-outcome
        success
)
set_tests_properties(
    os_qemu_stage1_load_success
    PROPERTIES
        LABELS "system"
        TIMEOUT 85
)

add_test(
    NAME os_qemu_file_system_persistence
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-file-system-persistence
        "${OS_FIRMWARE_IMAGE}"
        "${OS_BOOT_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
)
set_tests_properties(
    os_qemu_file_system_persistence
    PROPERTIES
        LABELS "system"
        TIMEOUT "${OS_TEST_QEMU_PERSISTENCE_TIMEOUT_SECONDS}"
)

add_test(
    NAME os_qemu_firmware_serial_timeout_failure
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_SERIAL_FAILURE_IMAGE}"
        "${OS_BOOT_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --expected-outcome
        serial-failure
)
set_tests_properties(
    os_qemu_firmware_serial_timeout_failure
    PROPERTIES
        LABELS "system;failure-path"
        TIMEOUT 10
)

add_test(
    NAME os_qemu_firmware_ide_busy_timeout_failure
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IDE_BUSY_FAILURE_IMAGE}"
        "${OS_BOOT_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --expected-outcome
        ide-timeout
)
set_tests_properties(
    os_qemu_firmware_ide_busy_timeout_failure
    PROPERTIES
        LABELS "system;failure-path"
        TIMEOUT 10
)

add_test(
    NAME os_qemu_firmware_ide_error_failure
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IDE_ERROR_FAILURE_IMAGE}"
        "${OS_BOOT_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --expected-outcome
        ide-error
)
set_tests_properties(
    os_qemu_firmware_ide_error_failure
    PROPERTIES
        LABELS "system;failure-path"
        TIMEOUT 10
)

add_test(
    NAME os_qemu_stage1_header_failure
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_STAGE1_INVALID_HEADER_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --expected-outcome
        stage1-header-invalid
)
set_tests_properties(
    os_qemu_stage1_header_failure
    PROPERTIES
        LABELS "system;failure-path"
        TIMEOUT 10
)

add_test(
    NAME os_qemu_stage1_checksum_failure
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_STAGE1_INVALID_CHECKSUM_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --expected-outcome
        stage1-checksum-invalid
)
set_tests_properties(
    os_qemu_stage1_checksum_failure
    PROPERTIES
        LABELS "system;failure-path"
        TIMEOUT 10
)

add_test(
    NAME os_qemu_stage1_memory_map_failure
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_STAGE1_MEMORY_MAP_INVALID_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --expected-outcome
        memory-map-invalid
)
set_tests_properties(
    os_qemu_stage1_memory_map_failure
    PROPERTIES
        LABELS "system;failure-path"
        TIMEOUT 10
)

add_test(
    NAME os_qemu_kernel_header_failure
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_KERNEL_INVALID_HEADER_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --expected-outcome
        kernel-header-invalid
)
set_tests_properties(
    os_qemu_kernel_header_failure
    PROPERTIES
        LABELS "system;failure-path"
        TIMEOUT 10
)

add_test(
    NAME os_qemu_kernel_checksum_failure
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_KERNEL_INVALID_CHECKSUM_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --expected-outcome
        kernel-checksum-invalid
)
set_tests_properties(
    os_qemu_kernel_checksum_failure
    PROPERTIES
        LABELS "system;failure-path"
        TIMEOUT 10
)

add_test(
    NAME os_qemu_kernel_elf_failure
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_KERNEL_INVALID_ELF_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --expected-outcome
        kernel-elf-invalid
)
set_tests_properties(
    os_qemu_kernel_elf_failure
    PROPERTIES
        LABELS "system;failure-path"
        TIMEOUT 10
)

add_test(
    NAME os_qemu_kernel_ata_timeout_failure
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_STAGE1_KERNEL_ATA_TIMEOUT_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --expected-outcome
        kernel-ata-timeout
)
set_tests_properties(
    os_qemu_kernel_ata_timeout_failure
    PROPERTIES
        LABELS "system;failure-path"
        TIMEOUT 10
)

add_test(
    NAME os_qemu_kernel_ata_error_failure
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_STAGE1_KERNEL_ATA_ERROR_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --expected-outcome
        kernel-ata-error
)
set_tests_properties(
    os_qemu_kernel_ata_error_failure
    PROPERTIES
        LABELS "system;failure-path"
        TIMEOUT 10
)

add_test(
    NAME os_qemu_kernel_invalid_opcode_panic
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_KERNEL_INVALID_OPCODE_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --expected-outcome
        kernel-invalid-opcode
)
set_tests_properties(
    os_qemu_kernel_invalid_opcode_panic
    PROPERTIES
        LABELS "system;failure-path"
        TIMEOUT 10
)

add_test(
    NAME os_qemu_kernel_page_fault_panic
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_KERNEL_PAGE_FAULT_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --expected-outcome
        kernel-page-fault
)
set_tests_properties(
    os_qemu_kernel_page_fault_panic
    PROPERTIES
        LABELS "system;failure-path"
        TIMEOUT 10
)

add_test(
    NAME os_qemu_kernel_write_protection_panic
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_KERNEL_WRITE_PROTECTION_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --expected-outcome
        kernel-write-protection
)
set_tests_properties(
    os_qemu_kernel_write_protection_panic
    PROPERTIES
        LABELS "system;failure-path"
        TIMEOUT 10
)

add_test(
    NAME os_qemu_user_invalid_opcode_isolation
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_KERNEL_USER_INVALID_OPCODE_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --expected-outcome
        user-invalid-opcode
)
set_tests_properties(
    os_qemu_user_invalid_opcode_isolation
    PROPERTIES
        LABELS "system;failure-path"
        TIMEOUT 10
)

add_test(
    NAME os_qemu_user_page_fault_isolation
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_KERNEL_USER_PAGE_FAULT_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --expected-outcome
        user-page-fault
)
set_tests_properties(
    os_qemu_user_page_fault_isolation
    PROPERTIES
        LABELS "system;failure-path"
        TIMEOUT 10
)

add_test(
    NAME os_qemu_user_invalid_elf_rejection
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_KERNEL_USER_INVALID_ELF_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --expected-outcome
        user-invalid-elf
)
set_tests_properties(
    os_qemu_user_invalid_elf_rejection
    PROPERTIES
        LABELS "system;failure-path"
        TIMEOUT 10
)

add_test(
    NAME os_python_tooling_unit_tests
    COMMAND
        "${Python3_EXECUTABLE}"
        -m
        unittest
        discover
        -s
        tests/tooling
        -p
        "test_*.py"
)
set_tests_properties(
    os_python_tooling_unit_tests
    PROPERTIES
        LABELS "unit"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

add_test(
    NAME os_learning_diagram_geometry_check
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/check_learning_diagrams.py"
        --self-test
)
set_tests_properties(
    os_learning_diagram_geometry_check
    PROPERTIES
        LABELS "unit"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

find_program(
    OS_RUN_CLANG_TIDY_EXECUTABLE
    NAMES run-clang-tidy
    REQUIRED
)
set(OS_CPP_IDENTIFIER_AUDIT_PARALLEL_JOB_COUNT 4)
add_test(
    NAME os_cpp_identifier_naming_check
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os_tools/cpp_identifier_naming.py"
        --project-root
        "${PROJECT_SOURCE_DIR}"
        --build-directory
        "${PROJECT_BINARY_DIR}"
        --run-clang-tidy
        "${OS_RUN_CLANG_TIDY_EXECUTABLE}"
        --jobs
        "${OS_CPP_IDENTIFIER_AUDIT_PARALLEL_JOB_COUNT}"
)
set_tests_properties(
    os_cpp_identifier_naming_check
    PROPERTIES
        LABELS "integration"
        TIMEOUT 120
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

add_test(
    NAME os_firmware_randomized_tests
    COMMAND
        "${Python3_EXECUTABLE}"
        -m
        unittest
        discover
        -s
        tests/randomized/firmware
        -p
        "test_*.py"
)
set_tests_properties(
    os_firmware_randomized_tests
    PROPERTIES
        LABELS "randomized"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

add_test(
    NAME os_stage1_randomized_tests
    COMMAND
        "${Python3_EXECUTABLE}"
        -m
        unittest
        discover
        -s
        tests/randomized/boot
        -p
        "test_*.py"
)
set_tests_properties(
    os_stage1_randomized_tests
    PROPERTIES
        LABELS "randomized"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

add_test(
    NAME os_kernel_randomized_tests
    COMMAND
        "${Python3_EXECUTABLE}"
        -m
        unittest
        discover
        -s
        tests/randomized/kernel
        -p
        "test_*.py"
)
set_tests_properties(
    os_kernel_randomized_tests
    PROPERTIES
        LABELS "randomized"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

add_test(
    NAME os_book_source_check
    COMMAND
        "${CMAKE_COMMAND}"
        -E
        chdir
        "${PROJECT_SOURCE_DIR}/books/x86-64-os-from-reset"
        make
        source-check
)
set_tests_properties(
    os_book_source_check
    PROPERTIES
        LABELS "integration"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
