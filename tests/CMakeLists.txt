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
    os_abi_v2_contract_unit_tests
    unit/abi_v2_contract_test.cpp
)
target_link_libraries(
    os_abi_v2_contract_unit_tests
    PRIVATE
        os_system_abi
        os_test_support
)
target_compile_options(
    os_abi_v2_contract_unit_tests
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
)
add_test(
    NAME os_abi_v2_contract_unit_tests
    COMMAND os_abi_v2_contract_unit_tests
)
set_tests_properties(
    os_abi_v2_contract_unit_tests
    PROPERTIES
        LABELS "unit"
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

os_add_kernel_memory_test(
    os_kernel_virtual_memory_area_unit_tests
    unit/kernel/virtual_memory_area_test.cpp
    "unit"
)

os_add_kernel_memory_test(
    os_kernel_virtual_memory_area_randomized_tests
    randomized/kernel/virtual_memory_area_randomized_test.cpp
    "randomized"
)

os_add_kernel_memory_test(
    os_kernel_file_page_cache_unit_tests
    unit/kernel/file_page_cache_test.cpp
    "unit"
)

os_add_kernel_memory_test(
    os_kernel_file_page_cache_randomized_tests
    randomized/kernel/file_page_cache_randomized_test.cpp
    "randomized"
)

os_add_kernel_memory_test(
    os_kernel_file_page_cache_dynamic_lifecycle_integration_tests
    integration/boot/file_page_cache_dynamic_lifecycle_test.cpp
    "integration"
)

os_add_kernel_memory_test(
    os_kernel_file_page_prefetch_lifecycle_integration_tests
    integration/boot/file_page_prefetch_lifecycle_test.cpp
    "integration"
)

os_add_kernel_memory_test(
    os_kernel_file_readahead_unit_tests
    unit/kernel/file_readahead_test.cpp
    "unit"
)

os_add_kernel_memory_test(
    os_kernel_file_readahead_feedback_unit_tests
    unit/kernel/file_readahead_feedback_test.cpp
    "unit"
)

os_add_kernel_memory_test(
    os_kernel_file_readahead_stream_isolation_integration_tests
    integration/boot/file_readahead_stream_isolation_test.cpp
    "integration"
)

os_add_kernel_memory_test(
    os_kernel_file_readahead_randomized_tests
    randomized/kernel/file_readahead_randomized_test.cpp
    "randomized"
)

os_add_kernel_memory_test(
    os_kernel_file_readahead_feedback_randomized_tests
    randomized/kernel/file_readahead_feedback_randomized_test.cpp
    "randomized"
)

os_add_kernel_memory_test(
    os_kernel_file_cache_address_space_unit_tests
    unit/kernel/file_cache_address_space_test.cpp
    "unit"
)

os_add_kernel_memory_test(
    os_kernel_file_cache_address_space_lifecycle_integration_tests
    integration/boot/file_cache_address_space_lifecycle_test.cpp
    "integration"
)

os_add_kernel_memory_test(
    os_kernel_file_cache_address_space_randomized_tests
    randomized/kernel/file_cache_address_space_randomized_test.cpp
    "randomized"
)

os_add_kernel_memory_test(
    os_kernel_file_writeback_error_tracker_unit_tests
    unit/kernel/file_writeback_error_tracker_test.cpp
    "unit"
)

os_add_kernel_memory_test(
    os_kernel_file_writeback_error_tracker_randomized_tests
    randomized/kernel/file_writeback_error_tracker_randomized_test.cpp
    "randomized"
)

os_add_kernel_memory_test(
    os_kernel_user_page_reference_unit_tests
    unit/kernel/user_page_reference_test.cpp
    "unit"
)

os_add_kernel_memory_test(
    os_kernel_user_page_reference_randomized_tests
    randomized/kernel/user_page_reference_randomized_test.cpp
    "randomized"
)

os_add_kernel_memory_test(
    os_kernel_memory_pressure_unit_tests
    unit/kernel/memory_pressure_test.cpp
    "unit"
)

os_add_kernel_memory_test(
    os_kernel_memory_pressure_randomized_tests
    randomized/kernel/memory_pressure_randomized_test.cpp
    "randomized"
)

os_add_kernel_memory_test(
    os_kernel_background_reclaim_unit_tests
    unit/kernel/background_reclaim_test.cpp
    "unit"
)

os_add_kernel_memory_test(
    os_kernel_background_reclaim_randomized_tests
    randomized/kernel/background_reclaim_randomized_test.cpp
    "randomized"
)

os_add_kernel_memory_test(
    os_kernel_page_aging_unit_tests
    unit/kernel/page_aging_test.cpp
    "unit"
)

os_add_kernel_memory_test(
    os_kernel_page_aging_randomized_tests
    randomized/kernel/page_aging_randomized_test.cpp
    "randomized"
)

os_add_kernel_memory_test(
    os_kernel_swap_manager_unit_tests
    unit/kernel/swap_manager_test.cpp
    "unit"
)

os_add_kernel_memory_test(
    os_kernel_swap_manager_randomized_tests
    randomized/kernel/swap_manager_randomized_test.cpp
    "randomized"
)

os_add_kernel_memory_test(
    os_kernel_swap_storage_unit_tests
    unit/kernel/swap_storage_test.cpp
    "unit"
)

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
os_add_page_table_test(
    os_kernel_user_virtual_memory_lifecycle_integration_tests
    integration/boot/user_virtual_memory_lifecycle_test.cpp
    "integration"
)
os_add_page_table_test(
    os_kernel_shared_user_page_lifecycle_integration_tests
    integration/boot/shared_user_page_lifecycle_test.cpp
    "integration"
)
os_add_page_table_test(
    os_kernel_copy_on_write_page_table_lifecycle_integration_tests
    integration/boot/copy_on_write_page_table_lifecycle_test.cpp
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

function(os_add_kernel_time_test OS_TEST_TARGET OS_TEST_SOURCE OS_TEST_LABEL)
    add_executable(${OS_TEST_TARGET} ${OS_TEST_SOURCE})
    target_link_libraries(
        ${OS_TEST_TARGET}
        PRIVATE
            os_kernel_time_model_host
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

os_add_kernel_time_test(
    os_kernel_monotonic_clock_unit_tests
    unit/kernel/monotonic_clock_test.cpp
    "unit"
)
os_add_kernel_time_test(
    os_kernel_realtime_clock_unit_tests
    unit/kernel/realtime_clock_test.cpp
    "unit"
)
os_add_kernel_time_test(
    os_kernel_deadline_queue_unit_tests
    unit/kernel/deadline_queue_test.cpp
    "unit"
)
os_add_kernel_time_test(
    os_kernel_deadline_queue_randomized_tests
    randomized/kernel/deadline_queue_randomized_test.cpp
    "randomized"
)

os_add_kernel_device_test(
    os_kernel_device_model_unit_tests
    unit/kernel/device_model_test.cpp
    "unit"
)
os_add_kernel_device_test(
    os_kernel_vga_text_console_unit_tests
    unit/kernel/vga_text_console_test.cpp
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
os_add_kernel_device_test(
    os_kernel_block_request_unit_tests
    unit/kernel/block_request_test.cpp
    "unit"
)
os_add_kernel_device_test(
    os_kernel_block_request_completion_integration_tests
    integration/boot/block_request_completion_test.cpp
    "integration"
)
os_add_kernel_device_test(
    os_kernel_block_request_randomized_tests
    randomized/kernel/block_request_randomized_test.cpp
    "randomized"
)
os_add_kernel_device_test(
    os_kernel_asynchronous_block_device_unit_tests
    unit/kernel/asynchronous_block_device_test.cpp
    "unit"
)
os_add_kernel_device_test(
    os_kernel_asynchronous_block_device_adapter_integration_tests
    integration/boot/asynchronous_block_device_adapter_test.cpp
    "integration"
)
os_add_kernel_device_test(
    os_kernel_asynchronous_block_device_randomized_tests
    randomized/kernel/asynchronous_block_device_randomized_test.cpp
    "randomized"
)
os_add_kernel_device_test(
    os_kernel_pci_model_unit_tests
    unit/kernel/pci_model_test.cpp
    "unit"
)
os_add_kernel_device_test(
    os_kernel_pci_model_randomized_tests
    randomized/kernel/pci_model_randomized_test.cpp
    "randomized"
)
os_add_kernel_device_test(
    os_kernel_nvme_model_unit_tests
    unit/kernel/nvme_model_test.cpp
    "unit"
)
os_add_kernel_device_test(
    os_kernel_nvme_model_randomized_tests
    randomized/kernel/nvme_model_randomized_test.cpp
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
    os_kernel_work_queue_unit_tests
    unit/kernel/work_queue_test.cpp
    "unit"
)
os_add_kernel_process_test(
    os_kernel_block_io_unit_tests
    unit/kernel/block_io_test.cpp
    "unit"
)
os_add_kernel_process_test(
    os_kernel_file_page_load_unit_tests
    unit/kernel/file_page_load_test.cpp
    "unit"
)
os_add_kernel_process_test(
    os_kernel_file_page_writeback_unit_tests
    unit/kernel/file_page_writeback_test.cpp
    "unit"
)
os_add_kernel_process_test(
    os_kernel_file_readahead_request_unit_tests
    unit/kernel/file_readahead_request_test.cpp
    "unit"
)
os_add_kernel_process_test(
    os_kernel_private_futex_unit_tests
    unit/kernel/private_futex_test.cpp
    "unit"
)
os_add_kernel_process_test(
    os_kernel_process_tree_unit_tests
    unit/kernel/process_tree_test.cpp
    "unit"
)
os_add_kernel_process_test(
    os_kernel_signal_manager_unit_tests
    unit/kernel/signal_manager_test.cpp
    "unit"
)
os_add_kernel_process_test(
    os_kernel_job_control_unit_tests
    unit/kernel/job_control_test.cpp
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
    os_kernel_block_io_wait_queue_integration_tests
    integration/boot/block_io_wait_queue_test.cpp
    "integration"
)
os_add_kernel_process_test(
    os_kernel_file_page_load_wait_queue_integration_tests
    integration/boot/file_page_load_wait_queue_test.cpp
    "integration"
)
os_add_kernel_process_test(
    os_kernel_file_page_writeback_wait_queue_integration_tests
    integration/boot/file_page_writeback_wait_queue_test.cpp
    "integration"
)
os_add_kernel_process_test(
    os_kernel_deadline_scheduling_integration_tests
    integration/boot/deadline_scheduling_integration_test.cpp
    "integration"
)
os_add_kernel_process_test(
    os_kernel_signal_wait_integration_tests
    integration/boot/signal_wait_integration_test.cpp
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
    os_kernel_work_queue_randomized_tests
    randomized/kernel/work_queue_randomized_test.cpp
    "randomized"
)
os_add_kernel_process_test(
    os_kernel_block_io_randomized_tests
    randomized/kernel/block_io_randomized_test.cpp
    "randomized"
)
os_add_kernel_process_test(
    os_kernel_file_page_load_randomized_tests
    randomized/kernel/file_page_load_randomized_test.cpp
    "randomized"
)
os_add_kernel_process_test(
    os_kernel_file_page_writeback_randomized_tests
    randomized/kernel/file_page_writeback_randomized_test.cpp
    "randomized"
)
os_add_kernel_process_test(
    os_kernel_file_readahead_request_randomized_tests
    randomized/kernel/file_readahead_request_randomized_test.cpp
    "randomized"
)
os_add_kernel_process_test(
    os_kernel_private_futex_randomized_tests
    randomized/kernel/private_futex_randomized_test.cpp
    "randomized"
)
os_add_kernel_process_test(
    os_kernel_process_models_randomized_tests
    randomized/kernel/process_models_randomized_test.cpp
    "randomized"
)
os_add_kernel_process_test(
    os_kernel_signal_manager_randomized_tests
    randomized/kernel/signal_manager_randomized_test.cpp
    "randomized"
)
os_add_kernel_process_test(
    os_kernel_job_control_randomized_tests
    randomized/kernel/job_control_randomized_test.cpp
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
os_add_kernel_ipc_test(
    os_kernel_dynamic_pipe_unit_tests
    unit/kernel/dynamic_pipe_test.cpp
    "unit"
)
os_add_kernel_ipc_test(
    os_kernel_dynamic_pipe_capacity_integration_tests
    integration/boot/dynamic_pipe_capacity_test.cpp
    "integration"
)
os_add_kernel_ipc_test(
    os_kernel_dynamic_pipe_randomized_tests
    randomized/kernel/dynamic_pipe_randomized_test.cpp
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
    os_kernel_terminal_unit_tests
    unit/kernel/terminal_test.cpp
    "unit"
)

add_executable(
    os_kernel_terminal_job_control_integration_tests
    integration/boot/terminal_job_control_integration_test.cpp
)
target_link_libraries(
    os_kernel_terminal_job_control_integration_tests
    PRIVATE
        os_kernel_process_model_host
        os_kernel_user_environment_model_host
        os_test_support
)
target_compile_options(
    os_kernel_terminal_job_control_integration_tests
    PRIVATE
        ${OS_COMMON_WARNING_FLAGS}
)
add_test(
    NAME os_kernel_terminal_job_control_integration_tests
    COMMAND os_kernel_terminal_job_control_integration_tests
)
set_tests_properties(
    os_kernel_terminal_job_control_integration_tests
    PROPERTIES
        LABELS "integration"
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
    os_kernel_user_file_backing_unit_tests
    unit/kernel/user_file_backing_test.cpp
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
os_add_user_shell_test(
    os_user_shell_execution_unit_tests
    unit/user/shell_execution_test.cpp
    "unit"
)
os_add_user_shell_test(
    os_user_shell_execution_randomized_tests
    randomized/user/shell_execution_randomized_test.cpp
    "randomized"
)
os_add_user_shell_test(
    os_user_shell_environment_unit_tests
    unit/user/shell_environment_test.cpp
    "unit"
)
os_add_user_shell_test(
    os_user_shell_glob_unit_tests
    unit/user/shell_glob_test.cpp
    "unit"
)
os_add_user_shell_test(
    os_user_shell_line_editor_unit_tests
    unit/user/shell_line_editor_test.cpp
    "unit"
)
os_add_user_shell_test(
    os_user_shell_environment_glob_randomized_tests
    randomized/user/shell_environment_glob_randomized_test.cpp
    "randomized"
)
os_add_user_shell_test(
    os_user_shell_line_editor_randomized_tests
    randomized/user/shell_line_editor_randomized_test.cpp
    "randomized"
)

function(
    os_add_user_heap_test
    OS_TEST_TARGET
    OS_TEST_SOURCE
    OS_TEST_LABEL
)
    add_executable(${OS_TEST_TARGET} ${OS_TEST_SOURCE})
    target_link_libraries(
        ${OS_TEST_TARGET}
        PRIVATE
            os_user_heap_model_host
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

os_add_user_heap_test(
    os_user_heap_unit_tests
    unit/user/user_heap_test.cpp
    "unit"
)

os_add_user_heap_test(
    os_user_heap_randomized_tests
    randomized/user/user_heap_randomized_test.cpp
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
    os_kernel_credentials_unit_tests
    unit/kernel/credentials_test.cpp
    "unit"
)
os_add_kernel_file_system_test(
    os_kernel_root_journal_unit_tests
    unit/kernel/root_journal_test.cpp
    "unit"
)
os_add_kernel_file_system_test(
    os_kernel_root_journal_crash_recovery_integration_tests
    integration/boot/root_journal_crash_recovery_test.cpp
    "integration"
)
os_add_kernel_file_system_test(
    os_kernel_root_journal_randomized_tests
    randomized/kernel/root_journal_randomized_test.cpp
    "randomized"
)
os_add_kernel_file_system_test(
    os_kernel_vfs_unit_tests
    unit/kernel/vfs_test.cpp
    "unit"
)
os_add_kernel_file_system_test(
    os_kernel_vfs_namespace_cache_unit_tests
    unit/kernel/vfs_namespace_cache_test.cpp
    "unit"
)
os_add_kernel_file_system_test(
    os_kernel_vfs_inode_metadata_cache_unit_tests
    unit/kernel/vfs_inode_metadata_cache_test.cpp
    "unit"
)
os_add_kernel_file_system_test(
    os_kernel_vfs_permission_unit_tests
    unit/kernel/vfs_permission_test.cpp
    "unit"
)
os_add_kernel_file_system_test(
    os_kernel_devfs_unit_tests
    unit/kernel/devfs_test.cpp
    "unit"
)
os_add_kernel_file_system_test(
    os_kernel_procfs_unit_tests
    unit/kernel/procfs_test.cpp
    "unit"
)
os_add_kernel_file_system_test(
    os_kernel_pseudo_file_system_integration_tests
    integration/boot/pseudo_file_system_test.cpp
    "integration"
)
os_add_kernel_file_system_test(
    os_kernel_procfs_randomized_tests
    randomized/kernel/procfs_randomized_test.cpp
    "randomized"
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
    os_kernel_vfs_namespace_cache_isolation_integration_tests
    integration/boot/vfs_namespace_cache_isolation_test.cpp
    "integration"
)
os_add_kernel_file_system_test(
    os_kernel_vfs_inode_metadata_cache_integration_tests
    integration/boot/vfs_inode_metadata_cache_integration_test.cpp
    "integration"
)
os_add_kernel_file_system_test(
    os_kernel_vfs_dentry_cache_production_integration_tests
    integration/boot/vfs_dentry_cache_production_test.cpp
    "integration"
)
target_link_libraries(
    os_kernel_vfs_dentry_cache_production_integration_tests
    PRIVATE
        Threads::Threads
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
os_add_kernel_file_system_test(
    os_kernel_root_file_system_high_lba_integration_tests
    integration/boot/root_file_system_high_lba_test.cpp
    "integration"
)
set_tests_properties(
    os_kernel_root_file_system_high_lba_integration_tests
    PROPERTIES
        TIMEOUT 60
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
os_add_kernel_file_system_test(
    os_kernel_vfs_namespace_cache_randomized_tests
    randomized/kernel/vfs_namespace_cache_randomized_test.cpp
    "randomized"
)
os_add_kernel_file_system_test(
    os_kernel_vfs_inode_metadata_cache_randomized_tests
    randomized/kernel/vfs_inode_metadata_cache_randomized_test.cpp
    "randomized"
)
set(OS_TEST_VFS_NAMESPACE_RANDOMIZED_TIMEOUT_SECONDS 300)
set_tests_properties(
    os_kernel_vfs_namespace_randomized_tests
    PROPERTIES
        TIMEOUT "${OS_TEST_VFS_NAMESPACE_RANDOMIZED_TIMEOUT_SECONDS}"
)

find_package(Threads REQUIRED)
os_add_kernel_memory_test(
    os_kernel_file_page_cache_loading_merge_integration_tests
    integration/boot/file_page_cache_loading_merge_test.cpp
    "integration"
)
target_link_libraries(
    os_kernel_file_page_cache_loading_merge_integration_tests
    PRIVATE
        os_kernel_process_model_host
        Threads::Threads
)
os_add_kernel_memory_test(
    os_kernel_file_page_writeback_concurrency_integration_tests
    integration/boot/file_page_writeback_concurrency_test.cpp
    "integration"
)
target_link_libraries(
    os_kernel_file_page_writeback_concurrency_integration_tests
    PRIVATE
        os_kernel_process_model_host
        Threads::Threads
)
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
os_add_user_elf_audit_test(
    os_user_memory_probe_elf_layout
    "${OS_USER_MEMORY_PROBE_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_oom_probe_elf_layout
    "${OS_USER_OOM_PROBE_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_fork_probe_elf_layout
    "${OS_USER_FORK_PROBE_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_thread_probe_elf_layout
    "${OS_USER_THREAD_PROBE_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_time_probe_elf_layout
    "${OS_USER_TIME_PROBE_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_signal_probe_elf_layout
    "${OS_USER_SIGNAL_PROBE_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_memory_guard_probe_elf_layout
    "${OS_USER_MEMORY_GUARD_PROBE_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_memory_protection_probe_elf_layout
    "${OS_USER_MEMORY_PROTECTION_PROBE_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_tool_probe_elf_layout
    "${OS_USER_TOOL_PROBE_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_security_probe_elf_layout
    "${OS_USER_SECURITY_PROBE_ELF_PATH}"
)
os_add_user_elf_audit_test(
    os_user_security_exec_target_elf_layout
    "${OS_USER_SECURITY_EXEC_TARGET_ELF_PATH}"
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

add_test(
    NAME os_swap_disk_layout
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        audit-swap-image
        "${OS_SWAP_DISK_IMAGE}"
)
set_tests_properties(
    os_swap_disk_layout
    PROPERTIES
        LABELS "integration"
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

set(OS_TEST_QEMU_PRIMARY_GUEST_MEMORY_MEBIBYTES 4096)
set(OS_TEST_QEMU_PRIMARY_TIMEOUT_SECONDS 430)
set(OS_TEST_QEMU_PERSISTENCE_TIMEOUT_SECONDS 750)

add_test(
    NAME os_qemu_primary_smoke
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

add_test(
    NAME os_qemu_memory_reclaim_pressure
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_KERNEL_RECLAIM_PRESSURE_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --memory-mebibytes
        "${OS_TEST_QEMU_PRIMARY_GUEST_MEMORY_MEBIBYTES}"
        --swap-disk-image
        "${OS_SWAP_DISK_IMAGE}"
        --expected-outcome
        reclaim-pressure
)
set_tests_properties(
    os_qemu_memory_reclaim_pressure
    PROPERTIES
        LABELS "system;memory;reclaim"
        TIMEOUT "${OS_TEST_QEMU_PRIMARY_TIMEOUT_SECONDS}"
)
add_test(
    NAME os_qemu_nvme_memory_reclaim_pressure
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_KERNEL_RECLAIM_PRESSURE_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --memory-mebibytes
        "${OS_TEST_QEMU_PRIMARY_GUEST_MEMORY_MEBIBYTES}"
        --swap-disk-image
        "${OS_SWAP_DISK_IMAGE}"
        --nvme-disk-image
        "${OS_KERNEL_RECLAIM_PRESSURE_DISK_IMAGE}"
        --nvme-swap-disk-image
        "${OS_SWAP_DISK_IMAGE}"
        --expected-outcome
        reclaim-pressure
)
set_tests_properties(
    os_qemu_nvme_memory_reclaim_pressure
    PROPERTIES
        LABELS "system;nvme;memory;reclaim"
        TIMEOUT "${OS_TEST_QEMU_PRIMARY_TIMEOUT_SECONDS}"
)
add_test(
    NAME os_qemu_oom_pressure
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_KERNEL_OOM_PRESSURE_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --memory-mebibytes
        "${OS_TEST_QEMU_PRIMARY_GUEST_MEMORY_MEBIBYTES}"
        --swap-disk-image
        "${OS_SWAP_DISK_IMAGE}"
        --expected-outcome
        oom-pressure
)
set_tests_properties(
    os_qemu_oom_pressure
    PROPERTIES
        LABELS "system;memory;oom"
        TIMEOUT "${OS_TEST_QEMU_PRIMARY_TIMEOUT_SECONDS}"
)
add_test(
    NAME os_qemu_nvme_oom_pressure
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_KERNEL_OOM_PRESSURE_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --memory-mebibytes
        "${OS_TEST_QEMU_PRIMARY_GUEST_MEMORY_MEBIBYTES}"
        --swap-disk-image
        "${OS_SWAP_DISK_IMAGE}"
        --nvme-disk-image
        "${OS_KERNEL_OOM_PRESSURE_DISK_IMAGE}"
        --nvme-swap-disk-image
        "${OS_SWAP_DISK_IMAGE}"
        --expected-outcome
        oom-pressure
)
set_tests_properties(
    os_qemu_nvme_oom_pressure
    PROPERTIES
        LABELS "system;nvme;memory;oom"
        TIMEOUT "${OS_TEST_QEMU_PRIMARY_TIMEOUT_SECONDS}"
)
set_tests_properties(
    os_qemu_primary_smoke
    PROPERTIES
        LABELS "system;primary-smoke"
        TIMEOUT "${OS_TEST_QEMU_PRIMARY_TIMEOUT_SECONDS}"
)

add_test(
    NAME os_qemu_nvme_io
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
        --nvme-disk-image
        "${OS_EMPTY_DISK_IMAGE}"
        --expected-outcome
        nvme-io
)
set_tests_properties(
    os_qemu_nvme_io
    PROPERTIES
        LABELS "system;nvme"
        TIMEOUT "${OS_TEST_QEMU_PRIMARY_TIMEOUT_SECONDS}"
)

add_test(
    NAME os_qemu_nvme_io_error_recovery
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
        --nvme-disk-image
        "${OS_EMPTY_DISK_IMAGE}"
        --nvme-failure-event
        write_aio
        --expected-outcome
        nvme-io-error
)
set_tests_properties(
    os_qemu_nvme_io_error_recovery
    PROPERTIES
        LABELS "system;nvme;failure-path"
        TIMEOUT "${OS_TEST_QEMU_PRIMARY_TIMEOUT_SECONDS}"
)

add_test(
    NAME os_qemu_nvme_io_timeout_recovery
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_KERNEL_NVME_TIMEOUT_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --memory-mebibytes
        "${OS_TEST_QEMU_PRIMARY_GUEST_MEMORY_MEBIBYTES}"
        --nvme-disk-image
        "${OS_EMPTY_DISK_IMAGE}"
        --expected-outcome
        nvme-io-timeout
)
set_tests_properties(
    os_qemu_nvme_io_timeout_recovery
    PROPERTIES
        LABELS "system;nvme;failure-path"
        TIMEOUT "${OS_TEST_QEMU_PRIMARY_TIMEOUT_SECONDS}"
)

function(os_add_qemu_nvme_storage_test OS_TEST_NAME OS_MEMORY_MEBIBYTES OS_TIMEOUT_SECONDS)
    add_test(
        NAME ${OS_TEST_NAME}
        COMMAND
            "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/os.py"
            qemu-firmware
            "${OS_FIRMWARE_IMAGE}"
            "${OS_BOOT_DISK_IMAGE}"
            "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
            "${OS_DISK_IMAGE_SIZE_BYTES}"
            --memory-mebibytes
            "${OS_MEMORY_MEBIBYTES}"
            --nvme-disk-image
            "${OS_BOOT_DISK_IMAGE}"
            --nvme-swap-disk-image
            "${OS_SWAP_DISK_IMAGE}"
            --expected-outcome
            nvme-storage
    )
    set_tests_properties(
        ${OS_TEST_NAME}
        PROPERTIES
            LABELS "system;nvme;storage"
            TIMEOUT "${OS_TIMEOUT_SECONDS}"
    )
endfunction()

os_add_qemu_nvme_storage_test(
    os_qemu_nvme_storage_primary
    "${OS_TEST_QEMU_PRIMARY_GUEST_MEMORY_MEBIBYTES}"
    "${OS_TEST_QEMU_PRIMARY_TIMEOUT_SECONDS}"
)

add_test(
    NAME os_qemu_nvme_ata_fallback
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
        nvme-fallback
)
set_tests_properties(
    os_qemu_nvme_ata_fallback
    PROPERTIES
        LABELS "system;nvme;fallback"
        TIMEOUT "${OS_TEST_QEMU_PRIMARY_TIMEOUT_SECONDS}"
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
        TIMEOUT "${OS_TEST_QEMU_PRIMARY_TIMEOUT_SECONDS}"
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
    NAME os_qemu_nvme_file_system_persistence
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-nvme-file-system-persistence
        "${OS_FIRMWARE_IMAGE}"
        "${OS_BOOT_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --swap-disk-image
        "${OS_SWAP_DISK_IMAGE}"
)
set_tests_properties(
    os_qemu_nvme_file_system_persistence
    PROPERTIES
        LABELS "system;nvme;storage;persistence"
        TIMEOUT "${OS_TEST_QEMU_PERSISTENCE_TIMEOUT_SECONDS}"
)

add_test(
    NAME os_qemu_firmware_vga_initialization_failure
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_VGA_FAILURE_IMAGE}"
        "${OS_BOOT_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --expected-outcome
        vga-failure
)
set_tests_properties(
    os_qemu_firmware_vga_initialization_failure
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

set(OS_TEST_QEMU_RESOURCE_LOCK_NAME os_qemu_system_x86_64)
set(
    OS_TEST_QEMU_VGA_TEST_NAMES
    os_qemu_hardware_smoke
    os_qemu_rejects_invalid_image_size
    os_qemu_primary_smoke
    os_qemu_memory_reclaim_pressure
    os_qemu_nvme_memory_reclaim_pressure
    os_qemu_oom_pressure
    os_qemu_nvme_oom_pressure
    os_qemu_nvme_io
    os_qemu_nvme_io_error_recovery
    os_qemu_nvme_io_timeout_recovery
    os_qemu_nvme_storage_primary
    os_qemu_nvme_ata_fallback
    os_qemu_native_system_call_unsupported
    os_qemu_stage1_load_success
    os_qemu_file_system_persistence
    os_qemu_nvme_file_system_persistence
    os_qemu_firmware_vga_initialization_failure
    os_qemu_firmware_ide_busy_timeout_failure
    os_qemu_firmware_ide_error_failure
    os_qemu_stage1_header_failure
    os_qemu_stage1_checksum_failure
    os_qemu_stage1_memory_map_failure
    os_qemu_kernel_header_failure
    os_qemu_kernel_checksum_failure
    os_qemu_kernel_elf_failure
    os_qemu_kernel_ata_timeout_failure
    os_qemu_kernel_ata_error_failure
    os_qemu_kernel_invalid_opcode_panic
    os_qemu_kernel_page_fault_panic
    os_qemu_kernel_write_protection_panic
    os_qemu_user_invalid_opcode_isolation
    os_qemu_user_page_fault_isolation
    os_qemu_user_invalid_elf_rejection
)
set_tests_properties(
    ${OS_TEST_QEMU_VGA_TEST_NAMES}
    PROPERTIES
        RESOURCE_LOCK "${OS_TEST_QEMU_RESOURCE_LOCK_NAME}"
        RUN_SERIAL TRUE
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

add_test(
    NAME os_release_identity_check
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        audit-release-identity
)
set_tests_properties(
    os_release_identity_check
    PROPERTIES
        LABELS "integration"
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
        TIMEOUT 240
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
