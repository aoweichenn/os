find_program(OS_STAGE1_NASM_EXECUTABLE nasm REQUIRED)

set(
    OS_STAGE1_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/entry.asm"
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
        -f
        bin
        -o
        "${OS_STAGE1_BINARY}"
        "${OS_STAGE1_SOURCE}"
    DEPENDS "${OS_STAGE1_SOURCE}"
    COMMENT "汇编自研 16 位 Stage 1 入口"
    VERBATIM
)

add_custom_target(os_stage1_binary DEPENDS "${OS_STAGE1_BINARY}")

add_custom_command(
    OUTPUT
        "${OS_STAGE1_DISK_IMAGE}"
        "${OS_STAGE1_INVALID_HEADER_DISK_IMAGE}"
        "${OS_STAGE1_INVALID_CHECKSUM_DISK_IMAGE}"
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        create-stage1-images
        "${OS_STAGE1_BINARY}"
        "${OS_IMAGE_OUTPUT_DIRECTORY}"
        "${OS_DISK_IMAGE_SIZE_BYTES}"
    DEPENDS
        "${OS_STAGE1_BINARY}"
        "${PROJECT_SOURCE_DIR}/tools/os.py"
        "${PROJECT_SOURCE_DIR}/tools/os_tools/stage1_image.py"
    COMMENT "生成自研 Stage 1 正常与损坏磁盘镜像"
    VERBATIM
)

add_custom_target(
    os_stage1_images
    DEPENDS
        "${OS_STAGE1_DISK_IMAGE}"
        "${OS_STAGE1_INVALID_HEADER_DISK_IMAGE}"
        "${OS_STAGE1_INVALID_CHECKSUM_DISK_IMAGE}"
)
