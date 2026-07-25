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
    os_kernel_heap_and_page_layout_unit_tests
    unit/kernel/heap_and_page_layout_test.cpp
    "unit"
)
os_add_kernel_memory_test(
    os_kernel_memory_bootstrap_integration_tests
    integration/boot/kernel_memory_bootstrap_test.cpp
    "integration"
)
os_add_kernel_memory_test(
    os_kernel_memory_management_randomized_tests
    randomized/kernel/memory_management_randomized_test.cpp
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
    os_kernel_process_scheduler_unit_tests
    unit/kernel/process_scheduler_test.cpp
    "unit"
)
os_add_kernel_process_test(
    os_kernel_process_scheduling_integration_tests
    integration/boot/kernel_process_scheduling_test.cpp
    "integration"
)
os_add_kernel_process_test(
    os_kernel_process_scheduler_randomized_tests
    randomized/kernel/process_scheduler_randomized_test.cpp
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
        "${OS_KERNEL_ELF_PATH}"
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
        --expected-outcome
        success
)
set_tests_properties(
    os_qemu_stage1_load_success
    PROPERTIES
        LABELS "system"
        TIMEOUT 10
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
