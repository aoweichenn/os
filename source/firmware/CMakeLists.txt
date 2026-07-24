find_program(OS_FIRMWARE_NASM_EXECUTABLE nasm REQUIRED)
find_program(OS_FIRMWARE_LLD_EXECUTABLE ld.lld REQUIRED)
find_program(OS_FIRMWARE_OBJCOPY_EXECUTABLE llvm-objcopy REQUIRED)

set(
    OS_FIRMWARE_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/reset_and_serial.asm"
)
set(
    OS_FIRMWARE_LINKER_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/linker/rom.ld"
)
set(
    OS_FIRMWARE_BUILD_DIRECTORY
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

function(
    os_add_firmware_image
    OS_FIRMWARE_TARGET
    OS_FIRMWARE_OUTPUT
    OS_FIRMWARE_TEST_DEFINE
)
    set(
        OS_FIRMWARE_OBJECT
        "${OS_FIRMWARE_BUILD_DIRECTORY}/${OS_FIRMWARE_TARGET}.o"
    )
    set(
        OS_FIRMWARE_ELF
        "${OS_FIRMWARE_BUILD_DIRECTORY}/${OS_FIRMWARE_TARGET}.elf"
    )

    set(OS_FIRMWARE_NASM_DEFINITIONS)
    if(NOT "${OS_FIRMWARE_TEST_DEFINE}" STREQUAL "")
        list(
            APPEND
            OS_FIRMWARE_NASM_DEFINITIONS
            "-D${OS_FIRMWARE_TEST_DEFINE}=1"
        )
    endif()

    add_custom_command(
        OUTPUT "${OS_FIRMWARE_OBJECT}"
        COMMAND
            "${CMAKE_COMMAND}"
            -E
            make_directory
            "${OS_FIRMWARE_BUILD_DIRECTORY}"
        COMMAND
            "${OS_FIRMWARE_NASM_EXECUTABLE}"
            -f
            elf32
            ${OS_FIRMWARE_NASM_DEFINITIONS}
            -o
            "${OS_FIRMWARE_OBJECT}"
            "${OS_FIRMWARE_SOURCE}"
        DEPENDS "${OS_FIRMWARE_SOURCE}"
        COMMENT "汇编 ${OS_FIRMWARE_TARGET} 的 16 位复位入口"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${OS_FIRMWARE_ELF}"
        COMMAND
            "${OS_FIRMWARE_LLD_EXECUTABLE}"
            -m
            elf_i386
            -T
            "${OS_FIRMWARE_LINKER_SCRIPT}"
            -o
            "${OS_FIRMWARE_ELF}"
            "${OS_FIRMWARE_OBJECT}"
        DEPENDS
            "${OS_FIRMWARE_OBJECT}"
            "${OS_FIRMWARE_LINKER_SCRIPT}"
        COMMENT "链接 ${OS_FIRMWARE_TARGET} 的 128 KiB ROM 布局"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${OS_FIRMWARE_OUTPUT}"
        COMMAND
            "${CMAKE_COMMAND}"
            -E
            make_directory
            "${OS_IMAGE_OUTPUT_DIRECTORY}"
        COMMAND
            "${OS_FIRMWARE_OBJCOPY_EXECUTABLE}"
            --gap-fill=0xff
            -O
            binary
            "${OS_FIRMWARE_ELF}"
            "${OS_FIRMWARE_OUTPUT}"
        DEPENDS "${OS_FIRMWARE_ELF}"
        COMMENT "生成 ${OS_FIRMWARE_TARGET} 裸 ROM 镜像"
        VERBATIM
    )

    add_custom_target(
        ${OS_FIRMWARE_TARGET}
        DEPENDS "${OS_FIRMWARE_OUTPUT}"
    )
endfunction()

os_add_firmware_image(
    os_firmware
    "${OS_FIRMWARE_IMAGE}"
    ""
)

# 该测试变体只改变串口就绪位的读取结果，用于稳定覆盖超时失败路径。
os_add_firmware_image(
    os_firmware_serial_failure
    "${OS_FIRMWARE_SERIAL_FAILURE_IMAGE}"
    "OS_FIRMWARE_TEST_FORCE_SERIAL_FAILURE"
)
