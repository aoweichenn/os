find_program(OS_STAGE1_NASM_EXECUTABLE nasm REQUIRED)

set(
    OS_STAGE1_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/entry.asm"
)
set(
    OS_STAGE1_KERNEL_LOADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel_loader.asm"
)
set(
    OS_STAGE1_MEMORY_MAP_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/memory_map.asm"
)
set(
    OS_STAGE1_KERNEL_LOADER_CONSTANTS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/kernel_loader.inc"
)
set(
    OS_STAGE1_BINARY
    "${CMAKE_CURRENT_BINARY_DIR}/generated/stage1.bin"
)

add_custom_command(
    OUTPUT "${OS_STAGE1_BINARY}"
    COMMAND
        "${CMAKE_COMMAND}"
        -E
        make_directory
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND
        "${OS_STAGE1_NASM_EXECUTABLE}"
        -I
        "${CMAKE_CURRENT_SOURCE_DIR}/include/"
        -I
        "${CMAKE_CURRENT_SOURCE_DIR}/src/"
        -f
        bin
        -o
        "${OS_STAGE1_BINARY}"
        "${OS_STAGE1_SOURCE}"
    DEPENDS
        "${OS_STAGE1_SOURCE}"
        "${OS_STAGE1_KERNEL_LOADER_SOURCE}"
        "${OS_STAGE1_MEMORY_MAP_SOURCE}"
        "${OS_STAGE1_KERNEL_LOADER_CONSTANTS}"
    COMMENT "汇编自研 Stage 1 模式切换与 ELF64 内核加载器"
    VERBATIM
)

add_custom_target(os_stage1_binary DEPENDS "${OS_STAGE1_BINARY}")

function(
    os_add_stage1_failure_binary
    OS_STAGE1_FAILURE_BINARY
    OS_STAGE1_FAILURE_DEFINITION
    OS_STAGE1_FAILURE_COMMENT
)
    add_custom_command(
        OUTPUT "${OS_STAGE1_FAILURE_BINARY}"
        COMMAND
            "${CMAKE_COMMAND}"
            -E
            make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/generated"
        COMMAND
            "${OS_STAGE1_NASM_EXECUTABLE}"
            -I
            "${CMAKE_CURRENT_SOURCE_DIR}/include/"
            -I
            "${CMAKE_CURRENT_SOURCE_DIR}/src/"
            -D
            "${OS_STAGE1_FAILURE_DEFINITION}=1"
            -f
            bin
            -o
            "${OS_STAGE1_FAILURE_BINARY}"
            "${OS_STAGE1_SOURCE}"
        DEPENDS
            "${OS_STAGE1_SOURCE}"
            "${OS_STAGE1_KERNEL_LOADER_SOURCE}"
            "${OS_STAGE1_MEMORY_MAP_SOURCE}"
            "${OS_STAGE1_KERNEL_LOADER_CONSTANTS}"
        COMMENT "${OS_STAGE1_FAILURE_COMMENT}"
        VERBATIM
    )
endfunction()

set(
    OS_STAGE1_KERNEL_ATA_TIMEOUT_BINARY
    "${CMAKE_CURRENT_BINARY_DIR}/generated/stage1_kernel_ata_timeout.bin"
)
set(
    OS_STAGE1_KERNEL_ATA_ERROR_BINARY
    "${CMAKE_CURRENT_BINARY_DIR}/generated/stage1_kernel_ata_error.bin"
)
set(
    OS_STAGE1_MEMORY_MAP_INVALID_BINARY
    "${CMAKE_CURRENT_BINARY_DIR}/generated/stage1_memory_map_invalid.bin"
)
os_add_stage1_failure_binary(
    "${OS_STAGE1_KERNEL_ATA_TIMEOUT_BINARY}"
    OS_STAGE1_TEST_FORCE_KERNEL_ATA_TIMEOUT
    "汇编 Stage 1 Kernel ATA 超时测试镜像"
)
os_add_stage1_failure_binary(
    "${OS_STAGE1_KERNEL_ATA_ERROR_BINARY}"
    OS_STAGE1_TEST_FORCE_KERNEL_ATA_ERROR
    "汇编 Stage 1 Kernel ATA 设备错误测试镜像"
)
os_add_stage1_failure_binary(
    "${OS_STAGE1_MEMORY_MAP_INVALID_BINARY}"
    OS_STAGE1_TEST_FORCE_MEMORY_MAP_INVALID
    "汇编 Stage 1 物理内存图发现失败测试镜像"
)

function(
    os_add_stage1_failure_disk
    OS_STAGE1_FAILURE_DISK_IMAGE
    OS_STAGE1_FAILURE_BINARY
)
    get_filename_component(
        OS_STAGE1_FAILURE_OUTPUT_DIRECTORY
        "${OS_STAGE1_FAILURE_DISK_IMAGE}"
        DIRECTORY
    )
    add_custom_command(
        OUTPUT "${OS_STAGE1_FAILURE_DISK_IMAGE}"
        COMMAND
            "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/os.py"
            create-boot-images
            "${OS_STAGE1_FAILURE_BINARY}"
            "${OS_KERNEL_ELF_PATH}"
            "${OS_STAGE1_FAILURE_OUTPUT_DIRECTORY}"
            "${OS_DISK_IMAGE_SIZE_BYTES}"
        DEPENDS
            "${OS_STAGE1_FAILURE_BINARY}"
            "${OS_KERNEL_ELF_PATH}"
            "${PROJECT_SOURCE_DIR}/tools/os.py"
            "${PROJECT_SOURCE_DIR}/tools/os_tools/boot_image.py"
            "${PROJECT_SOURCE_DIR}/tools/os_tools/kernel_elf.py"
            "${PROJECT_SOURCE_DIR}/tools/os_tools/kernel_image.py"
            "${PROJECT_SOURCE_DIR}/tools/os_tools/stage1_image.py"
        COMMENT "生成 Stage 1 Kernel ATA 失败路径磁盘镜像"
        VERBATIM
    )
endfunction()

os_add_stage1_failure_disk(
    "${OS_STAGE1_KERNEL_ATA_TIMEOUT_DISK_IMAGE}"
    "${OS_STAGE1_KERNEL_ATA_TIMEOUT_BINARY}"
)
os_add_stage1_failure_disk(
    "${OS_STAGE1_KERNEL_ATA_ERROR_DISK_IMAGE}"
    "${OS_STAGE1_KERNEL_ATA_ERROR_BINARY}"
)
os_add_stage1_failure_disk(
    "${OS_STAGE1_MEMORY_MAP_INVALID_DISK_IMAGE}"
    "${OS_STAGE1_MEMORY_MAP_INVALID_BINARY}"
)

function(
    os_add_kernel_fault_disk
    OS_KERNEL_FAULT_DISK_IMAGE
    OS_KERNEL_FAULT_ELF
    OS_KERNEL_FAULT_COMMENT
)
    get_filename_component(
        OS_KERNEL_FAULT_OUTPUT_DIRECTORY
        "${OS_KERNEL_FAULT_DISK_IMAGE}"
        DIRECTORY
    )
    add_custom_command(
        OUTPUT "${OS_KERNEL_FAULT_DISK_IMAGE}"
        COMMAND
            "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/os.py"
            create-boot-images
            "${OS_STAGE1_BINARY}"
            "${OS_KERNEL_FAULT_ELF}"
            "${OS_KERNEL_FAULT_OUTPUT_DIRECTORY}"
            "${OS_DISK_IMAGE_SIZE_BYTES}"
        DEPENDS
            "${OS_STAGE1_BINARY}"
            "${OS_KERNEL_FAULT_ELF}"
            "${PROJECT_SOURCE_DIR}/tools/os.py"
            "${PROJECT_SOURCE_DIR}/tools/os_tools/boot_image.py"
            "${PROJECT_SOURCE_DIR}/tools/os_tools/kernel_elf.py"
            "${PROJECT_SOURCE_DIR}/tools/os_tools/kernel_image.py"
            "${PROJECT_SOURCE_DIR}/tools/os_tools/stage1_image.py"
        COMMENT "${OS_KERNEL_FAULT_COMMENT}"
        VERBATIM
    )
endfunction()

os_add_kernel_fault_disk(
    "${OS_KERNEL_INVALID_OPCODE_DISK_IMAGE}"
    "${OS_KERNEL_INVALID_OPCODE_ELF_PATH}"
    "生成非法指令异常注入磁盘镜像"
)
os_add_kernel_fault_disk(
    "${OS_KERNEL_PAGE_FAULT_DISK_IMAGE}"
    "${OS_KERNEL_PAGE_FAULT_ELF_PATH}"
    "生成页故障异常注入磁盘镜像"
)
os_add_kernel_fault_disk(
    "${OS_KERNEL_WRITE_PROTECTION_DISK_IMAGE}"
    "${OS_KERNEL_WRITE_PROTECTION_ELF_PATH}"
    "生成内核写保护故障注入磁盘镜像"
)

add_custom_command(
    OUTPUT
        "${OS_BOOT_DISK_IMAGE}"
        "${OS_STAGE1_INVALID_HEADER_DISK_IMAGE}"
        "${OS_STAGE1_INVALID_CHECKSUM_DISK_IMAGE}"
        "${OS_KERNEL_INVALID_HEADER_DISK_IMAGE}"
        "${OS_KERNEL_INVALID_CHECKSUM_DISK_IMAGE}"
        "${OS_KERNEL_INVALID_ELF_DISK_IMAGE}"
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        create-boot-images
        "${OS_STAGE1_BINARY}"
        "${OS_KERNEL_ELF_PATH}"
        "${OS_IMAGE_OUTPUT_DIRECTORY}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
    DEPENDS
        "${OS_STAGE1_BINARY}"
        "${OS_KERNEL_ELF_PATH}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        "${PROJECT_SOURCE_DIR}/tools/os_tools/boot_image.py"
        "${PROJECT_SOURCE_DIR}/tools/os_tools/kernel_elf.py"
        "${PROJECT_SOURCE_DIR}/tools/os_tools/kernel_image.py"
        "${PROJECT_SOURCE_DIR}/tools/os_tools/stage1_image.py"
    COMMENT "生成包含 Stage 1 与 Kernel ELF 的正常和损坏磁盘镜像"
    VERBATIM
)

add_custom_target(
    os_stage1_images
    DEPENDS
        "${OS_BOOT_DISK_IMAGE}"
        "${OS_STAGE1_INVALID_HEADER_DISK_IMAGE}"
        "${OS_STAGE1_INVALID_CHECKSUM_DISK_IMAGE}"
        "${OS_KERNEL_INVALID_HEADER_DISK_IMAGE}"
        "${OS_KERNEL_INVALID_CHECKSUM_DISK_IMAGE}"
        "${OS_KERNEL_INVALID_ELF_DISK_IMAGE}"
        "${OS_STAGE1_KERNEL_ATA_TIMEOUT_DISK_IMAGE}"
        "${OS_STAGE1_KERNEL_ATA_ERROR_DISK_IMAGE}"
        "${OS_STAGE1_MEMORY_MAP_INVALID_DISK_IMAGE}"
        "${OS_KERNEL_INVALID_OPCODE_DISK_IMAGE}"
        "${OS_KERNEL_PAGE_FAULT_DISK_IMAGE}"
        "${OS_KERNEL_WRITE_PROTECTION_DISK_IMAGE}"
)
