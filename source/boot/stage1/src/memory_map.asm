[bits 64]
default abs

os_stage1_discover_physical_memory:
%ifdef OS_STAGE1_TEST_FORCE_MEMORY_MAP_INVALID
    ; 故障镜像在真实 fw_cfg 访问前失败，用于验证发现失败不会越过交接边界。
    clc
    ret
%endif

    ; fw_cfg 的签名必须为 “QEMU”，否则当前硬件接口不满足启动契约。
    mov dx, OS_STAGE1_FW_CFG_SELECTOR_PORT
    mov ax, OS_STAGE1_FW_CFG_SIGNATURE_SELECTOR
    out dx, ax
    mov dx, OS_STAGE1_FW_CFG_DATA_PORT
    mov rdi, OS_STAGE1_FW_CFG_SCRATCH_ADDRESS
    mov rcx, 4
    rep insb
    cmp dword [OS_STAGE1_FW_CFG_SCRATCH_ADDRESS], \
        OS_STAGE1_FW_CFG_SIGNATURE_VALUE
    jne os_stage1_discover_physical_memory_failed

    ; 文件目录的整数使用大端序，逐字节转换后再参与 64 位边界检查。
    mov dx, OS_STAGE1_FW_CFG_SELECTOR_PORT
    mov ax, OS_STAGE1_FW_CFG_FILE_DIRECTORY_SELECTOR
    out dx, ax
    mov dx, OS_STAGE1_FW_CFG_DATA_PORT
    call os_stage1_fw_cfg_read_big_endian_u32
    test rax, rax
    jz os_stage1_discover_physical_memory_failed
    cmp rax, OS_STAGE1_FW_CFG_FILE_DIRECTORY_MAXIMUM_ENTRY_COUNT
    ja os_stage1_discover_physical_memory_failed
    mov r12, rax

os_stage1_find_e820_file:
    ; 目录采用流式遍历，只保留当前名称，避免把宿主提供的目录整体复制进低端内存。
    test r12, r12
    jz os_stage1_discover_physical_memory_failed
    call os_stage1_fw_cfg_read_big_endian_u32
    mov r13, rax
    call os_stage1_fw_cfg_read_big_endian_u16
    mov r14, rax
    call os_stage1_fw_cfg_read_big_endian_u16

    mov rdi, OS_STAGE1_FW_CFG_SCRATCH_ADDRESS
    mov rcx, OS_STAGE1_FW_CFG_FILE_DIRECTORY_NAME_SIZE_BYTES
    rep insb
    mov rax, OS_STAGE1_FW_CFG_E820_NAME_LOW
    cmp qword [OS_STAGE1_FW_CFG_SCRATCH_ADDRESS], rax
    jne os_stage1_find_e820_file_next
    cmp byte [ \
        OS_STAGE1_FW_CFG_SCRATCH_ADDRESS \
        + OS_STAGE1_FW_CFG_E820_NAME_TERMINATOR_OFFSET \
    ], 0
    je os_stage1_read_e820_file

os_stage1_find_e820_file_next:
    dec r12
    jmp os_stage1_find_e820_file

os_stage1_read_e820_file:
    ; QEMU 的 etc/e820 每项为 20 字节，项目 ABI 在尾部补 4 字节属性形成 24 字节项。
    test r13, r13
    jz os_stage1_discover_physical_memory_failed
    mov rax, r13
    xor rdx, rdx
    mov rcx, OS_STAGE1_PHYSICAL_MEMORY_MAP_QEMU_ENTRY_SIZE_BYTES
    div rcx
    test rdx, rdx
    jnz os_stage1_discover_physical_memory_failed
    test rax, rax
    jz os_stage1_discover_physical_memory_failed
    cmp rax, OS_STAGE1_PHYSICAL_MEMORY_MAP_MAXIMUM_ENTRY_COUNT
    ja os_stage1_discover_physical_memory_failed
    mov [ \
        OS_STAGE1_MEMORY_MAP_METADATA_ADDRESS \
        + OS_STAGE1_MEMORY_MAP_ENTRY_COUNT_OFFSET \
    ], rax
    mov r12, rax

    mov dx, OS_STAGE1_FW_CFG_SELECTOR_PORT
    mov ax, r14w
    out dx, ax
    mov dx, OS_STAGE1_FW_CFG_DATA_PORT
    mov rdi, OS_STAGE1_PHYSICAL_MEMORY_MAP_ADDRESS

os_stage1_read_e820_entry:
    mov rcx, OS_STAGE1_PHYSICAL_MEMORY_MAP_QEMU_ENTRY_SIZE_BYTES
    rep insb
    mov dword [rdi], 0
    add rdi, OS_STAGE1_PHYSICAL_MEMORY_MAP_ENTRY_SIZE_BYTES \
        - OS_STAGE1_PHYSICAL_MEMORY_MAP_QEMU_ENTRY_SIZE_BYTES
    dec r12
    jnz os_stage1_read_e820_entry
    ; fw_cfg 不承诺按基址排序，内核 ABI 要求交接前得到单调且可验证的内存图。
    call os_stage1_sort_physical_memory_map
    stc
    ret

os_stage1_discover_physical_memory_failed:
    clc
    ret

os_stage1_fw_cfg_read_big_endian_u32:
    xor eax, eax
    mov rcx, 4

os_stage1_fw_cfg_read_big_endian_u32_byte:
    shl eax, 8
    in al, dx
    loop os_stage1_fw_cfg_read_big_endian_u32_byte
    ret

os_stage1_fw_cfg_read_big_endian_u16:
    xor eax, eax
    mov rcx, 2

os_stage1_fw_cfg_read_big_endian_u16_byte:
    shl eax, 8
    in al, dx
    loop os_stage1_fw_cfg_read_big_endian_u16_byte
    ret

os_stage1_sort_physical_memory_map:
    mov r12, [ \
        OS_STAGE1_MEMORY_MAP_METADATA_ADDRESS \
        + OS_STAGE1_MEMORY_MAP_ENTRY_COUNT_OFFSET \
    ]
    dec r12
    jz os_stage1_sort_physical_memory_map_complete

os_stage1_sort_physical_memory_map_outer:
    mov rsi, OS_STAGE1_PHYSICAL_MEMORY_MAP_ADDRESS
    mov r13, r12

os_stage1_sort_physical_memory_map_inner:
    mov rax, [rsi]
    cmp rax, [rsi + OS_STAGE1_PHYSICAL_MEMORY_MAP_ENTRY_SIZE_BYTES]
    jbe os_stage1_sort_physical_memory_map_next
    mov rdi, rsi
    add rdi, OS_STAGE1_PHYSICAL_MEMORY_MAP_ENTRY_SIZE_BYTES
    mov rcx, OS_STAGE1_PHYSICAL_MEMORY_MAP_ENTRY_SIZE_BYTES \
        / OS_STAGE1_PHYSICAL_MEMORY_MAP_SWAP_WIDTH_BYTES

os_stage1_sort_physical_memory_map_swap:
    mov rax, [rsi]
    mov rdx, [rdi]
    mov [rsi], rdx
    mov [rdi], rax
    add rsi, OS_STAGE1_PHYSICAL_MEMORY_MAP_SWAP_WIDTH_BYTES
    add rdi, OS_STAGE1_PHYSICAL_MEMORY_MAP_SWAP_WIDTH_BYTES
    loop os_stage1_sort_physical_memory_map_swap
    sub rsi, OS_STAGE1_PHYSICAL_MEMORY_MAP_ENTRY_SIZE_BYTES

os_stage1_sort_physical_memory_map_next:
    add rsi, OS_STAGE1_PHYSICAL_MEMORY_MAP_ENTRY_SIZE_BYTES
    dec r13
    jnz os_stage1_sort_physical_memory_map_inner
    dec r12
    jnz os_stage1_sort_physical_memory_map_outer

os_stage1_sort_physical_memory_map_complete:
    ret

os_stage1_validate_kernel_staging_memory:
    ; 高端暂存区必须完整落在同一条可用 RAM 记录中，不能只依赖 QEMU
    ; 的默认内存大小，也不能跨越保留区间。
    mov r12, [ \
        OS_STAGE1_MEMORY_MAP_METADATA_ADDRESS \
        + OS_STAGE1_MEMORY_MAP_ENTRY_COUNT_OFFSET \
    ]
    mov rsi, OS_STAGE1_PHYSICAL_MEMORY_MAP_ADDRESS

os_stage1_validate_kernel_staging_memory_entry:
    test r12, r12
    jz os_stage1_validate_kernel_staging_memory_invalid
    cmp dword [ \
        rsi + OS_STAGE1_PHYSICAL_MEMORY_MAP_ENTRY_TYPE_OFFSET \
    ], OS_STAGE1_PHYSICAL_MEMORY_MAP_USABLE_TYPE
    jne os_stage1_validate_kernel_staging_memory_next

    mov rax, [ \
        rsi + OS_STAGE1_PHYSICAL_MEMORY_MAP_ENTRY_BASE_OFFSET \
    ]
    cmp rax, OS_STAGE1_KERNEL_STAGING_ADDRESS
    ja os_stage1_validate_kernel_staging_memory_next
    mov rdx, [ \
        rsi + OS_STAGE1_PHYSICAL_MEMORY_MAP_ENTRY_LENGTH_OFFSET \
    ]
    add rdx, rax
    jc os_stage1_validate_kernel_staging_memory_next
    cmp rdx, OS_STAGE1_KERNEL_STAGING_END_ADDRESS
    jae os_stage1_validate_kernel_staging_memory_valid

os_stage1_validate_kernel_staging_memory_next:
    add rsi, OS_STAGE1_PHYSICAL_MEMORY_MAP_ENTRY_SIZE_BYTES
    dec r12
    jmp os_stage1_validate_kernel_staging_memory_entry

os_stage1_validate_kernel_staging_memory_valid:
    stc
    ret

os_stage1_validate_kernel_staging_memory_invalid:
    clc
    ret
