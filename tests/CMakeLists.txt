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
    os_foundation_randomized_tests
    randomized/foundation/address_range_randomized_test.cpp
    "randomized"
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
    NAME os_qemu_firmware_reset_serial_success
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        qemu-firmware
        "${OS_FIRMWARE_IMAGE}"
        "${OS_EMPTY_DISK_IMAGE}"
        "${OS_FIRMWARE_IMAGE_SIZE_BYTES}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
        --expected-outcome
        success
)
set_tests_properties(
    os_qemu_firmware_reset_serial_success
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
        "${OS_EMPTY_DISK_IMAGE}"
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
    NAME os_book_source_check
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/books/x86-64-os-from-reset/source/latex/scripts/check_inputs.py"
)
set_tests_properties(
    os_book_source_check
    PROPERTIES
        LABELS "integration"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
