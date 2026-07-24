[bits 64]
default abs

os_stage1_load_kernel:
    mov eax, OS_STAGE1_KERNEL_DESCRIPTOR_LBA
    mov rdi, OS_STAGE1_KERNEL_DESCRIPTOR_ADDRESS
    call os_stage1_read_ata_sector
    jnc os_stage1_load_kernel_failed

    call os_stage1_validate_kernel_descriptor
    jnc os_stage1_load_kernel_failed
    mov rsi, OS_STAGE1_LOAD_PHYSICAL_BASE \
        + os_stage1_kernel_header_valid_message
    call os_stage1_long_write_string

    call os_stage1_read_kernel_payload
    jnc os_stage1_load_kernel_failed
    call os_stage1_validate_kernel_payload
    jnc os_stage1_load_kernel_failed
    mov rsi, OS_STAGE1_LOAD_PHYSICAL_BASE \
        + os_stage1_kernel_payload_valid_message
    call os_stage1_long_write_string

    call os_stage1_validate_kernel_elf
    jnc os_stage1_load_kernel_failed
    mov rsi, OS_STAGE1_LOAD_PHYSICAL_BASE \
        + os_stage1_kernel_elf_valid_message
    call os_stage1_long_write_string

    call os_stage1_copy_kernel_segments
    call os_stage1_build_boot_info
    mov rsi, OS_STAGE1_LOAD_PHYSICAL_BASE \
        + os_stage1_kernel_segments_loaded_message
    call os_stage1_long_write_string
    mov rsi, OS_STAGE1_LOAD_PHYSICAL_BASE \
        + os_stage1_boot_info_ready_message
    call os_stage1_long_write_string

    stc
    ret

os_stage1_load_kernel_failed:
    clc
    ret

os_stage1_validate_kernel_descriptor:
    mov rsi, OS_STAGE1_KERNEL_DESCRIPTOR_ADDRESS
    mov rax, OS_STAGE1_KERNEL_DESCRIPTOR_MAGIC
    cmp qword [ \
        rsi + OS_STAGE1_KERNEL_DESCRIPTOR_MAGIC_OFFSET \
    ], rax
    jne os_stage1_kernel_descriptor_invalid
    cmp word [ \
        rsi + OS_STAGE1_KERNEL_DESCRIPTOR_VERSION_OFFSET \
    ], OS_STAGE1_KERNEL_DESCRIPTOR_VERSION
    jne os_stage1_kernel_descriptor_invalid
    cmp word [ \
        rsi + OS_STAGE1_KERNEL_DESCRIPTOR_HEADER_SIZE_OFFSET \
    ], OS_STAGE1_KERNEL_DESCRIPTOR_HEADER_SIZE_BYTES
    jne os_stage1_kernel_descriptor_invalid
    cmp dword [ \
        rsi + OS_STAGE1_KERNEL_DESCRIPTOR_FLAGS_OFFSET \
    ], OS_STAGE1_KERNEL_DESCRIPTOR_FLAGS_NONE
    jne os_stage1_kernel_descriptor_invalid
    cmp qword [ \
        rsi + OS_STAGE1_KERNEL_DESCRIPTOR_PAYLOAD_LBA_OFFSET \
    ], OS_STAGE1_KERNEL_PAYLOAD_LBA
    jne os_stage1_kernel_descriptor_invalid

    mov rax, [ \
        rsi + OS_STAGE1_KERNEL_DESCRIPTOR_FILE_SIZE_OFFSET \
    ]
    test rax, rax
    jz os_stage1_kernel_descriptor_invalid
    cmp rax, OS_STAGE1_KERNEL_MAXIMUM_FILE_SIZE_BYTES
    ja os_stage1_kernel_descriptor_invalid
    add rax, OS_STAGE1_KERNEL_DESCRIPTOR_SECTOR_ROUNDING_BYTES
    jc os_stage1_kernel_descriptor_invalid
    shr rax, OS_STAGE1_KERNEL_DESCRIPTOR_SECTOR_SIZE_SHIFT
    cmp rax, [ \
        rsi + OS_STAGE1_KERNEL_DESCRIPTOR_SECTOR_COUNT_OFFSET \
    ]
    jne os_stage1_kernel_descriptor_invalid

    mov rdx, [ \
        rsi + OS_STAGE1_KERNEL_DESCRIPTOR_PAYLOAD_LBA_OFFSET \
    ]
    add rdx, rax
    jc os_stage1_kernel_descriptor_invalid
    dec rdx
    cmp rdx, OS_STAGE1_ATA_LBA28_MAXIMUM
    ja os_stage1_kernel_descriptor_invalid

    mov rdi, rsi
    add rdi, OS_STAGE1_KERNEL_DESCRIPTOR_HEADER_SIZE_BYTES
    mov rcx, OS_STAGE1_KERNEL_DESCRIPTOR_RESERVED_SIZE_BYTES
    xor eax, eax
    repe scasb
    jne os_stage1_kernel_descriptor_invalid

    ; CRC32 字段在计算描述符自身时按零处理，计算后恢复原始磁盘值。
    mov r8d, [ \
        rsi + OS_STAGE1_KERNEL_DESCRIPTOR_HEADER_CRC32_OFFSET \
    ]
    mov dword [ \
        rsi + OS_STAGE1_KERNEL_DESCRIPTOR_HEADER_CRC32_OFFSET \
    ], 0
    mov rcx, OS_STAGE1_KERNEL_DESCRIPTOR_SECTOR_SIZE_BYTES
    call os_stage1_calculate_crc32
    mov dword [ \
        OS_STAGE1_KERNEL_DESCRIPTOR_ADDRESS \
        + OS_STAGE1_KERNEL_DESCRIPTOR_HEADER_CRC32_OFFSET \
    ], r8d
    cmp eax, r8d
    jne os_stage1_kernel_descriptor_invalid

    stc
    ret

os_stage1_kernel_descriptor_invalid:
    mov al, OS_STAGE1_RESULT_KERNEL_HEADER_INVALID
    clc
    ret

os_stage1_read_kernel_payload:
    mov r8, [ \
        OS_STAGE1_KERNEL_DESCRIPTOR_ADDRESS \
        + OS_STAGE1_KERNEL_DESCRIPTOR_SECTOR_COUNT_OFFSET \
    ]
    mov r9, [ \
        OS_STAGE1_KERNEL_DESCRIPTOR_ADDRESS \
        + OS_STAGE1_KERNEL_DESCRIPTOR_PAYLOAD_LBA_OFFSET \
    ]
    mov rdi, OS_STAGE1_KERNEL_STAGING_ADDRESS

os_stage1_read_kernel_payload_sector:
    mov eax, r9d
    call os_stage1_read_ata_sector
    jnc os_stage1_read_kernel_payload_failed
    inc r9
    dec r8
    jnz os_stage1_read_kernel_payload_sector
    stc
    ret

os_stage1_read_kernel_payload_failed:
    clc
    ret

os_stage1_validate_kernel_payload:
    mov rsi, OS_STAGE1_KERNEL_STAGING_ADDRESS
    mov rcx, [ \
        OS_STAGE1_KERNEL_DESCRIPTOR_ADDRESS \
        + OS_STAGE1_KERNEL_DESCRIPTOR_FILE_SIZE_OFFSET \
    ]
    call os_stage1_calculate_crc32
    cmp eax, [ \
        OS_STAGE1_KERNEL_DESCRIPTOR_ADDRESS \
        + OS_STAGE1_KERNEL_DESCRIPTOR_PAYLOAD_CRC32_OFFSET \
    ]
    jne os_stage1_kernel_payload_invalid

    ; 精确文件长度之后的扇区填充必须为零，避免隐藏未校验的数据。
    mov rax, [ \
        OS_STAGE1_KERNEL_DESCRIPTOR_ADDRESS \
        + OS_STAGE1_KERNEL_DESCRIPTOR_SECTOR_COUNT_OFFSET \
    ]
    shl rax, OS_STAGE1_KERNEL_DESCRIPTOR_SECTOR_SIZE_SHIFT
    mov rcx, rax
    sub rcx, [ \
        OS_STAGE1_KERNEL_DESCRIPTOR_ADDRESS \
        + OS_STAGE1_KERNEL_DESCRIPTOR_FILE_SIZE_OFFSET \
    ]
    jz os_stage1_kernel_payload_valid
    mov rdi, OS_STAGE1_KERNEL_STAGING_ADDRESS
    add rdi, [ \
        OS_STAGE1_KERNEL_DESCRIPTOR_ADDRESS \
        + OS_STAGE1_KERNEL_DESCRIPTOR_FILE_SIZE_OFFSET \
    ]
    xor eax, eax
    repe scasb
    jne os_stage1_kernel_payload_invalid

os_stage1_kernel_payload_valid:
    stc
    ret

os_stage1_kernel_payload_invalid:
    mov al, OS_STAGE1_RESULT_KERNEL_CHECKSUM_INVALID
    clc
    ret

os_stage1_calculate_crc32:
    mov eax, OS_STAGE1_CRC32_INITIAL_VALUE

os_stage1_calculate_crc32_byte:
    test rcx, rcx
    jz os_stage1_calculate_crc32_complete
    xor al, [rsi]
    inc rsi
    mov edx, OS_STAGE1_CRC32_BITS_PER_BYTE

os_stage1_calculate_crc32_bit:
    shr eax, 1
    jnc os_stage1_calculate_crc32_next_bit
    xor eax, OS_STAGE1_CRC32_POLYNOMIAL

os_stage1_calculate_crc32_next_bit:
    dec edx
    jnz os_stage1_calculate_crc32_bit
    dec rcx
    jmp os_stage1_calculate_crc32_byte

os_stage1_calculate_crc32_complete:
    not eax
    ret

os_stage1_validate_kernel_elf:
    ; 第一遍只验证完整 ELF 与段间关系，不在失败前写入目标内存。
    mov r13, OS_STAGE1_KERNEL_STAGING_ADDRESS
    mov r14, [ \
        OS_STAGE1_KERNEL_DESCRIPTOR_ADDRESS \
        + OS_STAGE1_KERNEL_DESCRIPTOR_FILE_SIZE_OFFSET \
    ]
    cmp r14, OS_STAGE1_ELF_HEADER_SIZE_BYTES
    jb os_stage1_kernel_elf_invalid
    cmp dword [r13], OS_STAGE1_ELF_MAGIC
    jne os_stage1_kernel_elf_invalid
    cmp byte [ \
        r13 + OS_STAGE1_ELF_IDENT_CLASS_OFFSET \
    ], OS_STAGE1_ELF_CLASS_64
    jne os_stage1_kernel_elf_invalid
    cmp byte [ \
        r13 + OS_STAGE1_ELF_IDENT_DATA_OFFSET \
    ], OS_STAGE1_ELF_DATA_LITTLE_ENDIAN
    jne os_stage1_kernel_elf_invalid
    cmp byte [ \
        r13 + OS_STAGE1_ELF_IDENT_VERSION_OFFSET \
    ], OS_STAGE1_ELF_VERSION_CURRENT
    jne os_stage1_kernel_elf_invalid
    cmp word [ \
        r13 + OS_STAGE1_ELF_TYPE_OFFSET \
    ], OS_STAGE1_ELF_TYPE_EXECUTABLE
    jne os_stage1_kernel_elf_invalid
    cmp word [ \
        r13 + OS_STAGE1_ELF_MACHINE_OFFSET \
    ], OS_STAGE1_ELF_MACHINE_X86_64
    jne os_stage1_kernel_elf_invalid
    cmp dword [ \
        r13 + OS_STAGE1_ELF_VERSION_OFFSET \
    ], OS_STAGE1_ELF_VERSION_CURRENT
    jne os_stage1_kernel_elf_invalid
    cmp qword [ \
        r13 + OS_STAGE1_ELF_ENTRY_OFFSET \
    ], OS_STAGE1_ELF_EXPECTED_ENTRY_ADDRESS
    jne os_stage1_kernel_elf_invalid
    cmp word [ \
        r13 + OS_STAGE1_ELF_HEADER_SIZE_OFFSET \
    ], OS_STAGE1_ELF_HEADER_SIZE_BYTES
    jne os_stage1_kernel_elf_invalid
    cmp word [ \
        r13 + OS_STAGE1_ELF_PROGRAM_HEADER_SIZE_OFFSET \
    ], OS_STAGE1_ELF_PROGRAM_HEADER_SIZE_BYTES
    jne os_stage1_kernel_elf_invalid

    mov rax, [r13 + OS_STAGE1_ELF_PROGRAM_HEADER_OFFSET]
    cmp rax, OS_STAGE1_ELF_HEADER_SIZE_BYTES
    jb os_stage1_kernel_elf_invalid
    mov [ \
        OS_STAGE1_LOADER_SCRATCH_PROGRAM_HEADER_OFFSET \
    ], rax
    movzx rax, word [ \
        r13 + OS_STAGE1_ELF_PROGRAM_HEADER_COUNT_OFFSET \
    ]
    test rax, rax
    jz os_stage1_kernel_elf_invalid
    cmp rax, OS_STAGE1_ELF_MAXIMUM_PROGRAM_HEADER_COUNT
    ja os_stage1_kernel_elf_invalid
    mov [ \
        OS_STAGE1_LOADER_SCRATCH_PROGRAM_HEADER_COUNT \
    ], rax
    imul rax, OS_STAGE1_ELF_PROGRAM_HEADER_SIZE_BYTES
    add rax, [ \
        OS_STAGE1_LOADER_SCRATCH_PROGRAM_HEADER_OFFSET \
    ]
    jc os_stage1_kernel_elf_invalid
    cmp rax, r14
    ja os_stage1_kernel_elf_invalid

    mov qword [ \
        OS_STAGE1_LOADER_SCRATCH_LOAD_SEGMENT_COUNT \
    ], 0
    mov qword [ \
        OS_STAGE1_LOADER_SCRATCH_ENTRY_COVERED \
    ], 0
    mov qword [ \
        OS_STAGE1_LOADER_SCRATCH_CURRENT_INDEX \
    ], 0

os_stage1_validate_kernel_program_header:
    mov rax, [ \
        OS_STAGE1_LOADER_SCRATCH_CURRENT_INDEX \
    ]
    cmp rax, [ \
        OS_STAGE1_LOADER_SCRATCH_PROGRAM_HEADER_COUNT \
    ]
    jae os_stage1_validate_kernel_program_headers_complete
    imul rax, OS_STAGE1_ELF_PROGRAM_HEADER_SIZE_BYTES
    add rax, [ \
        OS_STAGE1_LOADER_SCRATCH_PROGRAM_HEADER_OFFSET \
    ]
    add rax, r13
    mov rbx, rax
    cmp dword [ \
        rbx + OS_STAGE1_ELF_PROGRAM_TYPE_OFFSET \
    ], OS_STAGE1_ELF_PROGRAM_TYPE_LOAD
    jne os_stage1_validate_kernel_program_header_next

    call os_stage1_validate_kernel_load_segment
    jnc os_stage1_kernel_elf_invalid
    inc qword [ \
        OS_STAGE1_LOADER_SCRATCH_LOAD_SEGMENT_COUNT \
    ]
    call os_stage1_validate_kernel_segment_separation
    jnc os_stage1_kernel_elf_invalid

    mov eax, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_FLAGS_OFFSET \
    ]
    test eax, OS_STAGE1_ELF_PROGRAM_FLAG_EXECUTE
    jz os_stage1_validate_kernel_program_header_next
    mov rax, OS_STAGE1_ELF_EXPECTED_ENTRY_ADDRESS
    cmp rax, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_VIRTUAL_ADDRESS_OFFSET \
    ]
    jb os_stage1_validate_kernel_program_header_next
    mov rdx, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_VIRTUAL_ADDRESS_OFFSET \
    ]
    add rdx, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_MEMORY_SIZE_OFFSET \
    ]
    cmp rax, rdx
    jae os_stage1_validate_kernel_program_header_next
    mov qword [ \
        OS_STAGE1_LOADER_SCRATCH_ENTRY_COVERED \
    ], 1

os_stage1_validate_kernel_program_header_next:
    inc qword [ \
        OS_STAGE1_LOADER_SCRATCH_CURRENT_INDEX \
    ]
    jmp os_stage1_validate_kernel_program_header

os_stage1_validate_kernel_program_headers_complete:
    cmp qword [ \
        OS_STAGE1_LOADER_SCRATCH_LOAD_SEGMENT_COUNT \
    ], 0
    je os_stage1_kernel_elf_invalid
    cmp qword [ \
        OS_STAGE1_LOADER_SCRATCH_ENTRY_COVERED \
    ], 1
    jne os_stage1_kernel_elf_invalid
    stc
    ret

os_stage1_validate_kernel_load_segment:
    mov eax, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_FLAGS_OFFSET \
    ]
    mov edx, eax
    and edx, ~OS_STAGE1_ELF_PROGRAM_FLAG_KNOWN_MASK
    jnz os_stage1_kernel_load_segment_invalid
    test eax, OS_STAGE1_ELF_PROGRAM_FLAG_READ
    jz os_stage1_kernel_load_segment_invalid
    mov edx, eax
    and edx, OS_STAGE1_ELF_PROGRAM_FLAG_WRITE \
        | OS_STAGE1_ELF_PROGRAM_FLAG_EXECUTE
    cmp edx, OS_STAGE1_ELF_PROGRAM_FLAG_WRITE \
        | OS_STAGE1_ELF_PROGRAM_FLAG_EXECUTE
    je os_stage1_kernel_load_segment_invalid

    mov rax, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_FILE_SIZE_OFFSET \
    ]
    mov rdx, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_MEMORY_SIZE_OFFSET \
    ]
    cmp rax, rdx
    ja os_stage1_kernel_load_segment_invalid
    test rdx, rdx
    jz os_stage1_kernel_load_segment_invalid

    mov rax, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_FILE_OFFSET \
    ]
    cmp rax, r14
    ja os_stage1_kernel_load_segment_invalid
    mov rdx, r14
    sub rdx, rax
    cmp [ \
        rbx + OS_STAGE1_ELF_PROGRAM_FILE_SIZE_OFFSET \
    ], rdx
    ja os_stage1_kernel_load_segment_invalid

    cmp qword [ \
        rbx + OS_STAGE1_ELF_PROGRAM_ALIGNMENT_OFFSET \
    ], OS_STAGE1_ELF_SEGMENT_ALIGNMENT_BYTES
    jne os_stage1_kernel_load_segment_invalid
    mov rax, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_FILE_OFFSET \
    ]
    and rax, OS_STAGE1_ELF_SEGMENT_ALIGNMENT_MASK
    mov rdx, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_VIRTUAL_ADDRESS_OFFSET \
    ]
    and rdx, OS_STAGE1_ELF_SEGMENT_ALIGNMENT_MASK
    cmp rax, rdx
    jne os_stage1_kernel_load_segment_invalid

    mov rax, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_VIRTUAL_ADDRESS_OFFSET \
    ]
    cmp rax, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_PHYSICAL_ADDRESS_OFFSET \
    ]
    jne os_stage1_kernel_load_segment_invalid
    cmp rax, OS_STAGE1_ELF_MINIMUM_LOAD_ADDRESS
    jb os_stage1_kernel_load_segment_invalid
    mov rdx, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_MEMORY_SIZE_OFFSET \
    ]
    cmp rdx, OS_STAGE1_ELF_MAXIMUM_LOAD_END_ADDRESS
    ja os_stage1_kernel_load_segment_invalid
    mov rcx, OS_STAGE1_ELF_MAXIMUM_LOAD_END_ADDRESS
    sub rcx, rdx
    cmp rax, rcx
    ja os_stage1_kernel_load_segment_invalid

    stc
    ret

os_stage1_kernel_load_segment_invalid:
    clc
    ret

os_stage1_validate_kernel_segment_separation:
    ; 恒等装载令虚拟区间与物理区间相同，只需比较物理区间即可覆盖两者。
    xor r8, r8

os_stage1_validate_kernel_previous_segment:
    cmp r8, [ \
        OS_STAGE1_LOADER_SCRATCH_CURRENT_INDEX \
    ]
    jae os_stage1_kernel_segment_separation_valid
    mov rax, r8
    imul rax, OS_STAGE1_ELF_PROGRAM_HEADER_SIZE_BYTES
    add rax, [ \
        OS_STAGE1_LOADER_SCRATCH_PROGRAM_HEADER_OFFSET \
    ]
    add rax, r13
    mov r9, rax
    cmp dword [ \
        r9 + OS_STAGE1_ELF_PROGRAM_TYPE_OFFSET \
    ], OS_STAGE1_ELF_PROGRAM_TYPE_LOAD
    jne os_stage1_validate_kernel_previous_segment_next

    mov rax, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_PHYSICAL_ADDRESS_OFFSET \
    ]
    mov rdx, rax
    add rdx, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_MEMORY_SIZE_OFFSET \
    ]
    mov r10, [ \
        r9 + OS_STAGE1_ELF_PROGRAM_PHYSICAL_ADDRESS_OFFSET \
    ]
    mov r11, r10
    add r11, [ \
        r9 + OS_STAGE1_ELF_PROGRAM_MEMORY_SIZE_OFFSET \
    ]
    cmp rax, r11
    jae os_stage1_validate_kernel_previous_segment_next
    cmp r10, rdx
    jb os_stage1_kernel_segment_separation_invalid

os_stage1_validate_kernel_previous_segment_next:
    inc r8
    jmp os_stage1_validate_kernel_previous_segment

os_stage1_kernel_segment_separation_valid:
    stc
    ret

os_stage1_kernel_segment_separation_invalid:
    clc
    ret

os_stage1_kernel_elf_invalid:
    mov al, OS_STAGE1_RESULT_KERNEL_ELF_INVALID
    clc
    ret

os_stage1_copy_kernel_segments:
    ; 第二遍复制文件内容，并把 p_memsz 超出 p_filesz 的 BSS 尾部清零。
    mov qword [ \
        OS_STAGE1_LOADER_SCRATCH_CURRENT_INDEX \
    ], 0

os_stage1_copy_kernel_program_header:
    mov rax, [ \
        OS_STAGE1_LOADER_SCRATCH_CURRENT_INDEX \
    ]
    cmp rax, [ \
        OS_STAGE1_LOADER_SCRATCH_PROGRAM_HEADER_COUNT \
    ]
    jae os_stage1_copy_kernel_segments_complete
    imul rax, OS_STAGE1_ELF_PROGRAM_HEADER_SIZE_BYTES
    add rax, [ \
        OS_STAGE1_LOADER_SCRATCH_PROGRAM_HEADER_OFFSET \
    ]
    add rax, OS_STAGE1_KERNEL_STAGING_ADDRESS
    mov rbx, rax
    cmp dword [ \
        rbx + OS_STAGE1_ELF_PROGRAM_TYPE_OFFSET \
    ], OS_STAGE1_ELF_PROGRAM_TYPE_LOAD
    jne os_stage1_copy_kernel_program_header_next

    mov rsi, OS_STAGE1_KERNEL_STAGING_ADDRESS
    add rsi, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_FILE_OFFSET \
    ]
    mov rdi, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_PHYSICAL_ADDRESS_OFFSET \
    ]
    mov rcx, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_FILE_SIZE_OFFSET \
    ]
    rep movsb
    mov rcx, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_MEMORY_SIZE_OFFSET \
    ]
    sub rcx, [ \
        rbx + OS_STAGE1_ELF_PROGRAM_FILE_SIZE_OFFSET \
    ]
    xor eax, eax
    rep stosb

os_stage1_copy_kernel_program_header_next:
    inc qword [ \
        OS_STAGE1_LOADER_SCRATCH_CURRENT_INDEX \
    ]
    jmp os_stage1_copy_kernel_program_header

os_stage1_copy_kernel_segments_complete:
    ret

os_stage1_build_boot_info:
    mov rdi, OS_STAGE1_KERNEL_BOOT_INFO_ADDRESS
    mov rax, OS_STAGE1_BOOT_INFO_MAGIC
    mov [ \
        rdi + OS_STAGE1_BOOT_INFO_MAGIC_OFFSET \
    ], rax
    mov rax, OS_STAGE1_BOOT_INFO_VERSION
    mov [ \
        rdi + OS_STAGE1_BOOT_INFO_VERSION_OFFSET \
    ], rax
    mov rax, OS_STAGE1_BOOT_INFO_SIZE_BYTES
    mov [ \
        rdi + OS_STAGE1_BOOT_INFO_SIZE_OFFSET \
    ], rax
    mov qword [ \
        rdi + OS_STAGE1_BOOT_INFO_KERNEL_FILE_ADDRESS_OFFSET \
    ], OS_STAGE1_KERNEL_STAGING_ADDRESS
    mov rax, [ \
        OS_STAGE1_KERNEL_DESCRIPTOR_ADDRESS \
        + OS_STAGE1_KERNEL_DESCRIPTOR_FILE_SIZE_OFFSET \
    ]
    mov [ \
        rdi + OS_STAGE1_BOOT_INFO_KERNEL_FILE_SIZE_OFFSET \
    ], rax
    mov rax, [ \
        OS_STAGE1_KERNEL_STAGING_ADDRESS + OS_STAGE1_ELF_ENTRY_OFFSET \
    ]
    mov [ \
        rdi + OS_STAGE1_BOOT_INFO_KERNEL_ENTRY_OFFSET \
    ], rax
    mov rax, [ \
        OS_STAGE1_LOADER_SCRATCH_LOAD_SEGMENT_COUNT \
    ]
    mov [ \
        rdi + OS_STAGE1_BOOT_INFO_LOAD_SEGMENT_COUNT_OFFSET \
    ], rax
    mov qword [ \
        rdi + OS_STAGE1_BOOT_INFO_PAGE_TABLE_ROOT_OFFSET \
    ], OS_STAGE1_PML4_ADDRESS
    mov qword [ \
        rdi + OS_STAGE1_BOOT_INFO_IDENTITY_MAP_SIZE_OFFSET \
    ], OS_STAGE1_IDENTITY_MAPPED_SIZE_BYTES
    mov qword [ \
        rdi + OS_STAGE1_BOOT_INFO_KERNEL_STACK_TOP_OFFSET \
    ], OS_STAGE1_KERNEL_STACK_TOP
    mov qword [ \
        rdi + OS_STAGE1_BOOT_INFO_PHYSICAL_MEMORY_MAP_ADDRESS_OFFSET \
    ], OS_STAGE1_PHYSICAL_MEMORY_MAP_ADDRESS
    mov rax, [ \
        OS_STAGE1_MEMORY_MAP_METADATA_ADDRESS \
        + OS_STAGE1_MEMORY_MAP_ENTRY_COUNT_OFFSET \
    ]
    mov [ \
        rdi + OS_STAGE1_BOOT_INFO_PHYSICAL_MEMORY_MAP_ENTRY_COUNT_OFFSET \
    ], rax
    mov qword [ \
        rdi + OS_STAGE1_BOOT_INFO_PHYSICAL_MEMORY_MAP_ENTRY_SIZE_OFFSET \
    ], OS_STAGE1_PHYSICAL_MEMORY_MAP_ENTRY_SIZE_BYTES
    ret

os_stage1_read_ata_sector:
    push rbx
    push rcx
    push rdx
    mov ebx, eax

    mov dx, OS_STAGE1_ATA_DRIVE_HEAD_PORT
    shr eax, OS_STAGE1_ATA_LBA_HEAD_SHIFT
    and al, OS_STAGE1_ATA_DRIVE_HEAD_LBA_NIBBLE_MASK
    or al, OS_STAGE1_ATA_PRIMARY_MASTER_LBA_BASE
    out dx, al

    call os_stage1_ata_delay_400ns
    call os_stage1_wait_for_ata_not_busy
    jnc os_stage1_read_ata_sector_failed

    mov dx, OS_STAGE1_ATA_SECTOR_COUNT_PORT
    mov al, OS_STAGE1_ATA_SINGLE_SECTOR_COUNT
    out dx, al
    mov dx, OS_STAGE1_ATA_LBA_LOW_PORT
    mov al, bl
    out dx, al
    mov dx, OS_STAGE1_ATA_LBA_MID_PORT
    mov al, bh
    out dx, al
    shr ebx, OS_STAGE1_ATA_LBA_HIGH_SHIFT
    mov dx, OS_STAGE1_ATA_LBA_HIGH_PORT
    mov al, bl
    out dx, al
    mov dx, OS_STAGE1_ATA_COMMAND_STATUS_PORT
    mov al, OS_STAGE1_ATA_READ_SECTORS_COMMAND
    out dx, al

    call os_stage1_wait_for_ata_data
    jnc os_stage1_read_ata_sector_failed
    mov dx, OS_STAGE1_ATA_DATA_PORT
    mov ecx, OS_STAGE1_ATA_WORDS_PER_SECTOR
    rep insw
    pop rdx
    pop rcx
    pop rbx
    stc
    ret

os_stage1_read_ata_sector_failed:
    pop rdx
    pop rcx
    pop rbx
    clc
    ret

os_stage1_ata_delay_400ns:
    mov dx, OS_STAGE1_ATA_ALTERNATE_STATUS_PORT
    in al, dx
    in al, dx
    in al, dx
    in al, dx
    ret

os_stage1_wait_for_ata_not_busy:
    mov ecx, OS_STAGE1_ATA_STATUS_POLL_LIMIT
    mov dx, OS_STAGE1_ATA_COMMAND_STATUS_PORT

os_stage1_wait_for_ata_not_busy_poll:
%ifdef OS_STAGE1_TEST_FORCE_KERNEL_ATA_TIMEOUT
    ; 测试镜像保留完整轮询预算，验证 Stage 1 的有界超时路径。
    mov al, OS_STAGE1_ATA_STATUS_BUSY_BIT
%elifdef OS_STAGE1_TEST_FORCE_KERNEL_ATA_ERROR
    ; 测试镜像返回真实 ATA ERR 位形状，验证设备错误分类。
    mov al, OS_STAGE1_ATA_STATUS_ERROR_BIT
%else
    in al, dx
%endif
    test al, OS_STAGE1_ATA_STATUS_BUSY_BIT
    jnz os_stage1_wait_for_ata_not_busy_next
    test al, OS_STAGE1_ATA_ERROR_STATUS_MASK
    jnz os_stage1_ata_device_error
    stc
    ret

os_stage1_wait_for_ata_not_busy_next:
    loop os_stage1_wait_for_ata_not_busy_poll
    mov al, OS_STAGE1_RESULT_ATA_TIMEOUT
    clc
    ret

os_stage1_wait_for_ata_data:
    mov ecx, OS_STAGE1_ATA_STATUS_POLL_LIMIT
    mov dx, OS_STAGE1_ATA_COMMAND_STATUS_PORT

os_stage1_wait_for_ata_data_poll:
%ifdef OS_STAGE1_TEST_FORCE_KERNEL_ATA_TIMEOUT
    mov al, OS_STAGE1_ATA_STATUS_BUSY_BIT
%elifdef OS_STAGE1_TEST_FORCE_KERNEL_ATA_ERROR
    mov al, OS_STAGE1_ATA_STATUS_ERROR_BIT
%else
    in al, dx
%endif
    test al, OS_STAGE1_ATA_STATUS_BUSY_BIT
    jnz os_stage1_wait_for_ata_data_next
    test al, OS_STAGE1_ATA_ERROR_STATUS_MASK
    jnz os_stage1_ata_device_error
    test al, OS_STAGE1_ATA_STATUS_DATA_REQUEST_BIT
    jnz os_stage1_ata_data_ready

os_stage1_wait_for_ata_data_next:
    loop os_stage1_wait_for_ata_data_poll
    mov al, OS_STAGE1_RESULT_ATA_TIMEOUT
    clc
    ret

os_stage1_ata_device_error:
    mov al, OS_STAGE1_RESULT_ATA_DEVICE_ERROR
    clc
    ret

os_stage1_ata_data_ready:
    stc
    ret

os_stage1_kernel_header_valid_message:
    db "[OS][STAGE1] KERNEL_HEADER_VALID", 0x0D, 0x0A, 0x00

os_stage1_kernel_payload_valid_message:
    db "[OS][STAGE1] KERNEL_PAYLOAD_VALID", 0x0D, 0x0A, 0x00

os_stage1_kernel_elf_valid_message:
    db "[OS][STAGE1] KERNEL_ELF_VALID", 0x0D, 0x0A, 0x00

os_stage1_kernel_segments_loaded_message:
    db "[OS][STAGE1] KERNEL_SEGMENTS_LOADED", 0x0D, 0x0A, 0x00

os_stage1_boot_info_ready_message:
    db "[OS][STAGE1] BOOT_INFO_READY", 0x0D, 0x0A, 0x00
