bits 16

OS_FIRMWARE_ENTRY_RUNTIME_OFFSET equ 0xF000
OS_FIRMWARE_STACK_TOP_ADDRESS equ 0x7000
OS_FIRMWARE_STAGE1_DESCRIPTOR_BUFFER_ADDRESS equ 0x0500
OS_FIRMWARE_WORD_SIZE_BYTES equ 0x0002

OS_FIRMWARE_VGA_TEXT_SEGMENT equ 0xB800
OS_FIRMWARE_VGA_FONT_SEGMENT equ 0xA000
OS_FIRMWARE_VGA_STATE_SEGMENT equ 0x2000
OS_FIRMWARE_VGA_STATE_MAGIC equ 0x4756534F
OS_FIRMWARE_VGA_STATE_VERSION equ 0x00000003
OS_FIRMWARE_VGA_STATE_MAGIC_OFFSET equ 0x0000
OS_FIRMWARE_VGA_STATE_VERSION_OFFSET equ 0x0004
OS_FIRMWARE_VGA_STATE_TRACE_LENGTH_OFFSET equ 0x0008
OS_FIRMWARE_VGA_STATE_TRACE_OVERFLOW_OFFSET equ 0x000C
OS_FIRMWARE_VGA_STATE_CURSOR_ROW_OFFSET equ 0x0010
OS_FIRMWARE_VGA_STATE_CURSOR_COLUMN_OFFSET equ 0x0012
OS_FIRMWARE_VGA_STATE_ATTRIBUTE_OFFSET equ 0x0014
OS_FIRMWARE_VGA_STATE_TRACE_OFFSET equ 0x0020
OS_FIRMWARE_VGA_STATE_TRACE_CAPACITY equ 0x0007FFE0
OS_FIRMWARE_VGA_COLUMN_COUNT equ 80
OS_FIRMWARE_VGA_ROW_COUNT equ 25
OS_FIRMWARE_VGA_CELL_COUNT equ 2000
OS_FIRMWARE_VGA_ROW_SIZE_BYTES equ 160
OS_FIRMWARE_VGA_LAST_ROW_OFFSET equ 3840
OS_FIRMWARE_VGA_DEFAULT_ATTRIBUTE equ 0x07
OS_FIRMWARE_VGA_BLANK_CELL equ 0x0720
OS_FIRMWARE_VGA_CARRIAGE_RETURN equ 0x0D
OS_FIRMWARE_VGA_LINE_FEED equ 0x0A
OS_FIRMWARE_VGA_BACKSPACE equ 0x08
OS_FIRMWARE_VGA_TAB equ 0x09
OS_FIRMWARE_VGA_TAB_WIDTH equ 8
OS_FIRMWARE_VGA_FIRST_PRINTABLE equ 0x20
OS_FIRMWARE_VGA_LAST_PRINTABLE equ 0x7E
OS_FIRMWARE_VGA_MISC_OUTPUT_PORT equ 0x03C2
OS_FIRMWARE_VGA_SEQUENCER_INDEX_PORT equ 0x03C4
OS_FIRMWARE_VGA_GRAPHICS_INDEX_PORT equ 0x03CE
OS_FIRMWARE_VGA_ATTRIBUTE_INDEX_PORT equ 0x03C0
OS_FIRMWARE_VGA_INPUT_STATUS_PORT equ 0x03DA
OS_FIRMWARE_VGA_CRTC_INDEX_PORT equ 0x03D4
OS_FIRMWARE_VGA_CRTC_DATA_PORT equ 0x03D5
OS_FIRMWARE_VGA_DAC_MASK_PORT equ 0x03C6
OS_FIRMWARE_VGA_DAC_WRITE_INDEX_PORT equ 0x03C8
OS_FIRMWARE_VGA_DAC_DATA_PORT equ 0x03C9
OS_FIRMWARE_VGA_CRTC_CURSOR_HIGH_INDEX equ 0x0E
OS_FIRMWARE_VGA_CRTC_CURSOR_LOW_INDEX equ 0x0F
OS_FIRMWARE_VGA_SEQUENCER_REGISTER_COUNT equ 5
OS_FIRMWARE_VGA_CRTC_REGISTER_COUNT equ 25
OS_FIRMWARE_VGA_GRAPHICS_REGISTER_COUNT equ 9
OS_FIRMWARE_VGA_ATTRIBUTE_REGISTER_COUNT equ 21
OS_FIRMWARE_VGA_PALETTE_COLOR_COUNT equ 16
OS_FIRMWARE_VGA_FONT_FIRST_CHARACTER equ 0x20
OS_FIRMWARE_VGA_FONT_CHARACTER_COUNT equ 96
OS_FIRMWARE_VGA_FONT_SOURCE_ROWS equ 8
OS_FIRMWARE_VGA_FONT_SLOT_BYTES equ 32
OS_FIRMWARE_VGA_FONT_RUNTIME_OFFSET equ 0xE000
OS_FIRMWARE_VGA_FONT_DESTINATION_OFFSET equ \
    OS_FIRMWARE_VGA_FONT_FIRST_CHARACTER * OS_FIRMWARE_VGA_FONT_SLOT_BYTES

OS_FIRMWARE_PIT_COMMAND_PORT equ 0x0043
OS_FIRMWARE_PIT_CHANNEL_ZERO_PORT equ 0x0040
OS_FIRMWARE_PIT_CHANNEL_ZERO_MODE_TWO equ 0x34
OS_FIRMWARE_PIT_DIVISOR equ 0x04A9
OS_FIRMWARE_PIT_DIVISOR_LOW equ (OS_FIRMWARE_PIT_DIVISOR & 0x00FF)
OS_FIRMWARE_PIT_DIVISOR_HIGH equ (OS_FIRMWARE_PIT_DIVISOR >> 8)

OS_FIRMWARE_ATA_DATA_PORT equ 0x01F0
OS_FIRMWARE_ATA_SECTOR_COUNT_PORT equ 0x01F2
OS_FIRMWARE_ATA_LBA_LOW_PORT equ 0x01F3
OS_FIRMWARE_ATA_LBA_MID_PORT equ 0x01F4
OS_FIRMWARE_ATA_LBA_HIGH_PORT equ 0x01F5
OS_FIRMWARE_ATA_DRIVE_HEAD_PORT equ 0x01F6
OS_FIRMWARE_ATA_COMMAND_STATUS_PORT equ 0x01F7
OS_FIRMWARE_ATA_ALTERNATE_STATUS_PORT equ 0x03F6
OS_FIRMWARE_ATA_PRIMARY_MASTER_LBA_BASE equ 0xE0
OS_FIRMWARE_ATA_DRIVE_HEAD_LBA_NIBBLE_MASK equ 0x0F
OS_FIRMWARE_ATA_LBA_HEAD_SHIFT equ 24
OS_FIRMWARE_ATA_LBA_HIGH_SHIFT equ 16
OS_FIRMWARE_ATA_READ_SECTORS_COMMAND equ 0x20
OS_FIRMWARE_ATA_SINGLE_SECTOR_COUNT equ 0x01
OS_FIRMWARE_ATA_WORDS_PER_SECTOR equ 0x0100
OS_FIRMWARE_ATA_STATUS_ERROR_BIT equ 0x01
OS_FIRMWARE_ATA_STATUS_DATA_REQUEST_BIT equ 0x08
OS_FIRMWARE_ATA_STATUS_DEVICE_FAULT_BIT equ 0x20
OS_FIRMWARE_ATA_STATUS_BUSY_BIT equ 0x80
OS_FIRMWARE_ATA_ERROR_STATUS_MASK equ \
    OS_FIRMWARE_ATA_STATUS_ERROR_BIT \
    | OS_FIRMWARE_ATA_STATUS_DEVICE_FAULT_BIT
OS_FIRMWARE_ATA_STATUS_POLL_LIMIT equ 0xFFFF
OS_FIRMWARE_ATA_LBA28_MAXIMUM equ 0x0FFFFFFF
OS_FIRMWARE_DISK_SECTOR_COUNT equ 0x0800

OS_FIRMWARE_STAGE1_MAGIC_WORD_0 equ 0x534F
OS_FIRMWARE_STAGE1_MAGIC_WORD_1 equ 0x5453
OS_FIRMWARE_STAGE1_MAGIC_WORD_2 equ 0x4741
OS_FIRMWARE_STAGE1_MAGIC_WORD_3 equ 0x3145
OS_FIRMWARE_STAGE1_VERSION equ 0x0001
OS_FIRMWARE_STAGE1_HEADER_SIZE_BYTES equ 0x001C
OS_FIRMWARE_STAGE1_DESCRIPTOR_WORD_COUNT equ 0x0100
OS_FIRMWARE_STAGE1_DESCRIPTOR_LBA equ 0x00000000
OS_FIRMWARE_STAGE1_MINIMUM_PAYLOAD_LBA equ 0x00000001
OS_FIRMWARE_STAGE1_MAXIMUM_PAYLOAD_SECTORS equ 0x0040
OS_FIRMWARE_STAGE1_FLAGS_NONE equ 0x0000
OS_FIRMWARE_STAGE1_MINIMUM_LOAD_ADDRESS equ 0x00008000
OS_FIRMWARE_STAGE1_MAXIMUM_LOAD_END_ADDRESS equ 0x0009FC00
OS_FIRMWARE_STAGE1_VGA_STATE_BEGIN_ADDRESS equ 0x00020000
OS_FIRMWARE_STAGE1_VGA_STATE_END_ADDRESS equ 0x000A0000
OS_FIRMWARE_STAGE1_SECTOR_SIZE_SHIFT equ 0x0009
OS_FIRMWARE_STAGE1_WORD_COUNT_SHIFT equ 0x0008
OS_FIRMWARE_STAGE1_REAL_MODE_PARAGRAPH_SHIFT equ 0x0004

OS_FIRMWARE_STAGE1_MAGIC_OFFSET equ 0x0000
OS_FIRMWARE_STAGE1_VERSION_OFFSET equ 0x0008
OS_FIRMWARE_STAGE1_HEADER_SIZE_OFFSET equ 0x000A
OS_FIRMWARE_STAGE1_LOAD_SEGMENT_OFFSET equ 0x000C
OS_FIRMWARE_STAGE1_ENTRY_OFFSET equ 0x000E
OS_FIRMWARE_STAGE1_PAYLOAD_SECTOR_COUNT_OFFSET equ 0x0010
OS_FIRMWARE_STAGE1_FLAGS_OFFSET equ 0x0012
OS_FIRMWARE_STAGE1_PAYLOAD_LBA_OFFSET equ 0x0014
OS_FIRMWARE_STAGE1_PAYLOAD_CHECKSUM_OFFSET equ 0x0018

OS_FIRMWARE_STAGE1_MAGIC_ADDRESS equ \
    OS_FIRMWARE_STAGE1_DESCRIPTOR_BUFFER_ADDRESS \
    + OS_FIRMWARE_STAGE1_MAGIC_OFFSET
OS_FIRMWARE_STAGE1_MAGIC_WORD_1_ADDRESS equ \
    OS_FIRMWARE_STAGE1_MAGIC_ADDRESS + OS_FIRMWARE_WORD_SIZE_BYTES
OS_FIRMWARE_STAGE1_MAGIC_WORD_2_ADDRESS equ \
    OS_FIRMWARE_STAGE1_MAGIC_WORD_1_ADDRESS + OS_FIRMWARE_WORD_SIZE_BYTES
OS_FIRMWARE_STAGE1_MAGIC_WORD_3_ADDRESS equ \
    OS_FIRMWARE_STAGE1_MAGIC_WORD_2_ADDRESS + OS_FIRMWARE_WORD_SIZE_BYTES
OS_FIRMWARE_STAGE1_VERSION_ADDRESS equ \
    OS_FIRMWARE_STAGE1_DESCRIPTOR_BUFFER_ADDRESS \
    + OS_FIRMWARE_STAGE1_VERSION_OFFSET
OS_FIRMWARE_STAGE1_HEADER_SIZE_ADDRESS equ \
    OS_FIRMWARE_STAGE1_DESCRIPTOR_BUFFER_ADDRESS \
    + OS_FIRMWARE_STAGE1_HEADER_SIZE_OFFSET
OS_FIRMWARE_STAGE1_LOAD_SEGMENT_ADDRESS equ \
    OS_FIRMWARE_STAGE1_DESCRIPTOR_BUFFER_ADDRESS \
    + OS_FIRMWARE_STAGE1_LOAD_SEGMENT_OFFSET
OS_FIRMWARE_STAGE1_ENTRY_ADDRESS equ \
    OS_FIRMWARE_STAGE1_DESCRIPTOR_BUFFER_ADDRESS \
    + OS_FIRMWARE_STAGE1_ENTRY_OFFSET
OS_FIRMWARE_STAGE1_PAYLOAD_SECTOR_COUNT_ADDRESS equ \
    OS_FIRMWARE_STAGE1_DESCRIPTOR_BUFFER_ADDRESS \
    + OS_FIRMWARE_STAGE1_PAYLOAD_SECTOR_COUNT_OFFSET
OS_FIRMWARE_STAGE1_FLAGS_ADDRESS equ \
    OS_FIRMWARE_STAGE1_DESCRIPTOR_BUFFER_ADDRESS \
    + OS_FIRMWARE_STAGE1_FLAGS_OFFSET
OS_FIRMWARE_STAGE1_PAYLOAD_LBA_ADDRESS equ \
    OS_FIRMWARE_STAGE1_DESCRIPTOR_BUFFER_ADDRESS \
    + OS_FIRMWARE_STAGE1_PAYLOAD_LBA_OFFSET
OS_FIRMWARE_STAGE1_PAYLOAD_CHECKSUM_ADDRESS equ \
    OS_FIRMWARE_STAGE1_DESCRIPTOR_BUFFER_ADDRESS \
    + OS_FIRMWARE_STAGE1_PAYLOAD_CHECKSUM_OFFSET

OS_FIRMWARE_RESULT_ATA_TIMEOUT equ 0x01
OS_FIRMWARE_RESULT_ATA_DEVICE_ERROR equ 0x02
OS_FIRMWARE_RESULT_STAGE1_HEADER_INVALID equ 0x03
OS_FIRMWARE_RESULT_STAGE1_CHECKSUM_INVALID equ 0x04
OS_FIRMWARE_RESULT_CONSOLE_FAILURE equ 0x05

OS_FIRMWARE_RESET_VECTOR_OFFSET equ 0xFFF0
OS_FIRMWARE_NEAR_JUMP_INSTRUCTION_SIZE equ 0x0003
OS_FIRMWARE_NEAR_JUMP_OPCODE equ 0xE9
OS_FIRMWARE_RESET_JUMP_DISPLACEMENT equ \
    OS_FIRMWARE_ENTRY_RUNTIME_OFFSET \
    - OS_FIRMWARE_RESET_VECTOR_OFFSET \
    - OS_FIRMWARE_NEAR_JUMP_INSTRUCTION_SIZE

section .text.entry progbits alloc exec nowrite align=1

global os_firmware_entry

os_firmware_entry:
    cli
    cld

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, OS_FIRMWARE_STACK_TOP_ADDRESS
    xor bp, bp

    call os_firmware_initialize_vga_console
    jnc os_firmware_halt
    call os_firmware_initialize_pit

    mov si, OS_FIRMWARE_ENTRY_RUNTIME_OFFSET \
        + (os_firmware_reset_message - $$)
    call os_firmware_write_string
    jnc os_firmware_halt

    call os_firmware_validate_vga_console
    jnc os_firmware_halt

    mov si, OS_FIRMWARE_ENTRY_RUNTIME_OFFSET \
        + (os_firmware_vga_ready_message - $$)
    call os_firmware_write_string
    jnc os_firmware_halt

    mov si, OS_FIRMWARE_ENTRY_RUNTIME_OFFSET \
        + (os_firmware_clock_ready_message - $$)
    call os_firmware_write_string
    jnc os_firmware_halt

    call os_firmware_load_stage1
    jnc os_firmware_report_load_failure

    mov si, OS_FIRMWARE_ENTRY_RUNTIME_OFFSET \
        + (os_firmware_stage1_loaded_message - $$)
    call os_firmware_write_string
    jnc os_firmware_halt

    mov bx, [OS_FIRMWARE_STAGE1_LOAD_SEGMENT_ADDRESS]
    mov dx, [OS_FIRMWARE_STAGE1_ENTRY_ADDRESS]
    push bx
    push dx
    retf

os_firmware_report_load_failure:
    cmp al, OS_FIRMWARE_RESULT_ATA_TIMEOUT
    je os_firmware_report_ata_timeout
    cmp al, OS_FIRMWARE_RESULT_ATA_DEVICE_ERROR
    je os_firmware_report_ata_error
    cmp al, OS_FIRMWARE_RESULT_STAGE1_HEADER_INVALID
    je os_firmware_report_stage1_header_invalid
    cmp al, OS_FIRMWARE_RESULT_STAGE1_CHECKSUM_INVALID
    je os_firmware_report_stage1_checksum_invalid
    jmp os_firmware_halt

os_firmware_report_ata_timeout:
    mov si, OS_FIRMWARE_ENTRY_RUNTIME_OFFSET \
        + (os_firmware_ata_timeout_message - $$)
    call os_firmware_write_string
    jmp os_firmware_halt

os_firmware_report_ata_error:
    mov si, OS_FIRMWARE_ENTRY_RUNTIME_OFFSET \
        + (os_firmware_ata_error_message - $$)
    call os_firmware_write_string
    jmp os_firmware_halt

os_firmware_report_stage1_header_invalid:
    mov si, OS_FIRMWARE_ENTRY_RUNTIME_OFFSET \
        + (os_firmware_stage1_header_invalid_message - $$)
    call os_firmware_write_string
    jmp os_firmware_halt

os_firmware_report_stage1_checksum_invalid:
    mov si, OS_FIRMWARE_ENTRY_RUNTIME_OFFSET \
        + (os_firmware_stage1_checksum_invalid_message - $$)
    call os_firmware_write_string
    jmp os_firmware_halt

os_firmware_load_stage1:
    xor ax, ax
    mov es, ax
    mov di, OS_FIRMWARE_STAGE1_DESCRIPTOR_BUFFER_ADDRESS
    mov eax, OS_FIRMWARE_STAGE1_DESCRIPTOR_LBA
    call os_firmware_read_ata_sector
    jnc os_firmware_load_stage1_failed

    call os_firmware_validate_stage1_descriptor
    jnc os_firmware_load_stage1_failed

    mov si, OS_FIRMWARE_ENTRY_RUNTIME_OFFSET \
        + (os_firmware_stage1_header_valid_message - $$)
    call os_firmware_write_string
    jnc os_firmware_load_stage1_console_failed

    mov ax, [OS_FIRMWARE_STAGE1_LOAD_SEGMENT_ADDRESS]
    mov es, ax
    xor di, di
    mov ebx, [OS_FIRMWARE_STAGE1_PAYLOAD_LBA_ADDRESS]
    mov si, [OS_FIRMWARE_STAGE1_PAYLOAD_SECTOR_COUNT_ADDRESS]

os_firmware_load_stage1_sector:
    mov eax, ebx
    call os_firmware_read_ata_sector
    jnc os_firmware_load_stage1_failed
    inc ebx
    dec si
    jnz os_firmware_load_stage1_sector

    mov ax, es
    mov dx, ax
    xor ax, ax
    mov ds, ax
    mov cx, [OS_FIRMWARE_STAGE1_PAYLOAD_SECTOR_COUNT_ADDRESS]
    shl cx, OS_FIRMWARE_STAGE1_WORD_COUNT_SHIFT
    mov ax, dx
    mov ds, ax
    xor si, si
    xor dx, dx

os_firmware_load_stage1_checksum_word:
    lodsw
    add dx, ax
    loop os_firmware_load_stage1_checksum_word

    xor ax, ax
    mov ds, ax
    cmp dx, [OS_FIRMWARE_STAGE1_PAYLOAD_CHECKSUM_ADDRESS]
    jne os_firmware_load_stage1_checksum_failed

    stc
    ret

os_firmware_load_stage1_checksum_failed:
    mov al, OS_FIRMWARE_RESULT_STAGE1_CHECKSUM_INVALID
    clc
    ret

os_firmware_load_stage1_console_failed:
    mov al, OS_FIRMWARE_RESULT_CONSOLE_FAILURE

os_firmware_load_stage1_failed:
    clc
    ret

os_firmware_validate_stage1_descriptor:
    mov si, OS_FIRMWARE_STAGE1_DESCRIPTOR_BUFFER_ADDRESS
    mov cx, OS_FIRMWARE_STAGE1_DESCRIPTOR_WORD_COUNT
    xor dx, dx

os_firmware_validate_stage1_descriptor_checksum:
    lodsw
    add dx, ax
    loop os_firmware_validate_stage1_descriptor_checksum
    test dx, dx
    jnz os_firmware_validate_stage1_descriptor_failed

    cmp word [OS_FIRMWARE_STAGE1_MAGIC_ADDRESS], OS_FIRMWARE_STAGE1_MAGIC_WORD_0
    jne os_firmware_validate_stage1_descriptor_failed
    cmp word [OS_FIRMWARE_STAGE1_MAGIC_WORD_1_ADDRESS], \
        OS_FIRMWARE_STAGE1_MAGIC_WORD_1
    jne os_firmware_validate_stage1_descriptor_failed
    cmp word [OS_FIRMWARE_STAGE1_MAGIC_WORD_2_ADDRESS], \
        OS_FIRMWARE_STAGE1_MAGIC_WORD_2
    jne os_firmware_validate_stage1_descriptor_failed
    cmp word [OS_FIRMWARE_STAGE1_MAGIC_WORD_3_ADDRESS], \
        OS_FIRMWARE_STAGE1_MAGIC_WORD_3
    jne os_firmware_validate_stage1_descriptor_failed

    cmp word [OS_FIRMWARE_STAGE1_VERSION_ADDRESS], OS_FIRMWARE_STAGE1_VERSION
    jne os_firmware_validate_stage1_descriptor_failed
    cmp word [OS_FIRMWARE_STAGE1_HEADER_SIZE_ADDRESS], OS_FIRMWARE_STAGE1_HEADER_SIZE_BYTES
    jne os_firmware_validate_stage1_descriptor_failed
    cmp word [OS_FIRMWARE_STAGE1_FLAGS_ADDRESS], OS_FIRMWARE_STAGE1_FLAGS_NONE
    jne os_firmware_validate_stage1_descriptor_failed

    movzx ecx, word [OS_FIRMWARE_STAGE1_PAYLOAD_SECTOR_COUNT_ADDRESS]
    test ecx, ecx
    jz os_firmware_validate_stage1_descriptor_failed
    cmp ecx, OS_FIRMWARE_STAGE1_MAXIMUM_PAYLOAD_SECTORS
    ja os_firmware_validate_stage1_descriptor_failed

    mov eax, [OS_FIRMWARE_STAGE1_PAYLOAD_LBA_ADDRESS]
    cmp eax, OS_FIRMWARE_STAGE1_MINIMUM_PAYLOAD_LBA
    jb os_firmware_validate_stage1_descriptor_failed
    cmp eax, OS_FIRMWARE_ATA_LBA28_MAXIMUM
    ja os_firmware_validate_stage1_descriptor_failed
    mov edx, eax
    add edx, ecx
    jc os_firmware_validate_stage1_descriptor_failed
    cmp edx, OS_FIRMWARE_DISK_SECTOR_COUNT
    ja os_firmware_validate_stage1_descriptor_failed

    movzx eax, word [OS_FIRMWARE_STAGE1_LOAD_SEGMENT_ADDRESS]
    shl eax, OS_FIRMWARE_STAGE1_REAL_MODE_PARAGRAPH_SHIFT
    cmp eax, OS_FIRMWARE_STAGE1_MINIMUM_LOAD_ADDRESS
    jb os_firmware_validate_stage1_descriptor_failed
    mov edx, ecx
    shl edx, OS_FIRMWARE_STAGE1_SECTOR_SIZE_SHIFT
    add edx, eax
    jc os_firmware_validate_stage1_descriptor_failed
    cmp edx, OS_FIRMWARE_STAGE1_MAXIMUM_LOAD_END_ADDRESS
    ja os_firmware_validate_stage1_descriptor_failed
    cmp eax, OS_FIRMWARE_STAGE1_VGA_STATE_END_ADDRESS
    jae os_firmware_validate_stage1_load_does_not_overlap_vga_state
    cmp edx, OS_FIRMWARE_STAGE1_VGA_STATE_BEGIN_ADDRESS
    jbe os_firmware_validate_stage1_load_does_not_overlap_vga_state
    jmp os_firmware_validate_stage1_descriptor_failed

os_firmware_validate_stage1_load_does_not_overlap_vga_state:

    movzx eax, word [OS_FIRMWARE_STAGE1_ENTRY_ADDRESS]
    mov edx, ecx
    shl edx, OS_FIRMWARE_STAGE1_SECTOR_SIZE_SHIFT
    cmp eax, edx
    jae os_firmware_validate_stage1_descriptor_failed

    stc
    ret

os_firmware_validate_stage1_descriptor_failed:
    mov al, OS_FIRMWARE_RESULT_STAGE1_HEADER_INVALID
    clc
    ret

os_firmware_read_ata_sector:
    push ebx
    mov ebx, eax

    mov dx, OS_FIRMWARE_ATA_DRIVE_HEAD_PORT
    shr eax, OS_FIRMWARE_ATA_LBA_HEAD_SHIFT
    and al, OS_FIRMWARE_ATA_DRIVE_HEAD_LBA_NIBBLE_MASK
    or al, OS_FIRMWARE_ATA_PRIMARY_MASTER_LBA_BASE
    out dx, al

    call os_firmware_ata_delay_400ns
    call os_firmware_wait_for_ata_not_busy
    jnc os_firmware_read_ata_sector_failed

    mov dx, OS_FIRMWARE_ATA_SECTOR_COUNT_PORT
    mov al, OS_FIRMWARE_ATA_SINGLE_SECTOR_COUNT
    out dx, al

    mov dx, OS_FIRMWARE_ATA_LBA_LOW_PORT
    mov al, bl
    out dx, al

    mov dx, OS_FIRMWARE_ATA_LBA_MID_PORT
    mov al, bh
    out dx, al

    shr ebx, OS_FIRMWARE_ATA_LBA_HIGH_SHIFT
    mov dx, OS_FIRMWARE_ATA_LBA_HIGH_PORT
    mov al, bl
    out dx, al

    mov dx, OS_FIRMWARE_ATA_COMMAND_STATUS_PORT
    mov al, OS_FIRMWARE_ATA_READ_SECTORS_COMMAND
    out dx, al

    call os_firmware_wait_for_ata_data
    jnc os_firmware_read_ata_sector_failed

    mov dx, OS_FIRMWARE_ATA_DATA_PORT
    mov cx, OS_FIRMWARE_ATA_WORDS_PER_SECTOR
    rep insw
    pop ebx
    stc
    ret

os_firmware_read_ata_sector_failed:
    pop ebx
    clc
    ret

os_firmware_ata_delay_400ns:
    mov dx, OS_FIRMWARE_ATA_ALTERNATE_STATUS_PORT
    in al, dx
    in al, dx
    in al, dx
    in al, dx
    ret

os_firmware_wait_for_ata_not_busy:
    mov cx, OS_FIRMWARE_ATA_STATUS_POLL_LIMIT
    mov dx, OS_FIRMWARE_ATA_COMMAND_STATUS_PORT

os_firmware_wait_for_ata_not_busy_poll:
%ifdef OS_FIRMWARE_TEST_FORCE_IDE_BUSY_TIMEOUT
    ; 失败镜像在命令寄存器写入前就覆盖设备永久忙分支。
    mov al, OS_FIRMWARE_ATA_STATUS_BUSY_BIT
%elifdef OS_FIRMWARE_TEST_FORCE_IDE_DEVICE_ERROR
    ; 失败镜像保留真实 ERR 位形状，验证预命令状态检查。
    mov al, OS_FIRMWARE_ATA_STATUS_ERROR_BIT
%else
    in al, dx
%endif
    test al, OS_FIRMWARE_ATA_STATUS_BUSY_BIT
    jnz os_firmware_wait_for_ata_not_busy_next
    test al, OS_FIRMWARE_ATA_ERROR_STATUS_MASK
    jnz os_firmware_wait_for_ata_not_busy_device_error
    stc
    ret

os_firmware_wait_for_ata_not_busy_next:
    loop os_firmware_wait_for_ata_not_busy_poll
    mov al, OS_FIRMWARE_RESULT_ATA_TIMEOUT
    clc
    ret

os_firmware_wait_for_ata_not_busy_device_error:
    mov al, OS_FIRMWARE_RESULT_ATA_DEVICE_ERROR
    clc
    ret

os_firmware_wait_for_ata_data:
    mov cx, OS_FIRMWARE_ATA_STATUS_POLL_LIMIT
    mov dx, OS_FIRMWARE_ATA_COMMAND_STATUS_PORT

os_firmware_wait_for_ata_data_poll:
%ifdef OS_FIRMWARE_TEST_FORCE_IDE_BUSY_TIMEOUT
    ; 保留完整轮询次数，稳定覆盖设备永久忙的有界失败路径。
    mov al, OS_FIRMWARE_ATA_STATUS_BUSY_BIT
%elifdef OS_FIRMWARE_TEST_FORCE_IDE_DEVICE_ERROR
    ; 返回真实 ATA ERR 位形状，不绕过固件的错误状态分支。
    mov al, OS_FIRMWARE_ATA_STATUS_ERROR_BIT
%else
    in al, dx
%endif
    test al, OS_FIRMWARE_ATA_STATUS_BUSY_BIT
    jnz os_firmware_wait_for_ata_data_next
    test al, OS_FIRMWARE_ATA_ERROR_STATUS_MASK
    jnz os_firmware_wait_for_ata_data_device_error
    test al, OS_FIRMWARE_ATA_STATUS_DATA_REQUEST_BIT
    jnz os_firmware_wait_for_ata_data_ready

os_firmware_wait_for_ata_data_next:
    loop os_firmware_wait_for_ata_data_poll
    mov al, OS_FIRMWARE_RESULT_ATA_TIMEOUT
    clc
    ret

os_firmware_wait_for_ata_data_device_error:
    mov al, OS_FIRMWARE_RESULT_ATA_DEVICE_ERROR
    clc
    ret

os_firmware_wait_for_ata_data_ready:
    stc
    ret

os_firmware_initialize_vga_console:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov ax, OS_FIRMWARE_VGA_STATE_SEGMENT
    mov es, ax
    xor ax, ax
    xor di, di
    mov cx, OS_FIRMWARE_VGA_STATE_TRACE_OFFSET / 2
    rep stosw
    mov dword [es:OS_FIRMWARE_VGA_STATE_VERSION_OFFSET], OS_FIRMWARE_VGA_STATE_VERSION
    mov byte [es:OS_FIRMWARE_VGA_STATE_ATTRIBUTE_OFFSET], OS_FIRMWARE_VGA_DEFAULT_ATTRIBUTE

    mov si, OS_FIRMWARE_ENTRY_RUNTIME_OFFSET \
        + (os_firmware_vga_misc_register - $$)
    mov dx, OS_FIRMWARE_VGA_MISC_OUTPUT_PORT
    cs lodsb
    out dx, al

    mov si, OS_FIRMWARE_ENTRY_RUNTIME_OFFSET \
        + (os_firmware_vga_sequencer_registers - $$)
    mov dx, OS_FIRMWARE_VGA_SEQUENCER_INDEX_PORT
    xor bx, bx
    mov cx, OS_FIRMWARE_VGA_SEQUENCER_REGISTER_COUNT
    call os_firmware_program_vga_indexed_registers

    mov si, OS_FIRMWARE_ENTRY_RUNTIME_OFFSET \
        + (os_firmware_vga_crtc_registers - $$)
    mov dx, OS_FIRMWARE_VGA_CRTC_INDEX_PORT
    mov al, 0x03
    out dx, al
    inc dx
    in al, dx
    or al, 0x80
    out dx, al
    dec dx
    mov al, 0x11
    out dx, al
    inc dx
    in al, dx
    and al, 0x7F
    out dx, al
    dec dx
    xor bx, bx
    mov cx, OS_FIRMWARE_VGA_CRTC_REGISTER_COUNT
    call os_firmware_program_vga_indexed_registers

    mov si, OS_FIRMWARE_ENTRY_RUNTIME_OFFSET \
        + (os_firmware_vga_graphics_registers - $$)
    mov dx, OS_FIRMWARE_VGA_GRAPHICS_INDEX_PORT
    xor bx, bx
    mov cx, OS_FIRMWARE_VGA_GRAPHICS_REGISTER_COUNT
    call os_firmware_program_vga_indexed_registers

    mov si, OS_FIRMWARE_ENTRY_RUNTIME_OFFSET \
        + (os_firmware_vga_attribute_registers - $$)
    xor bx, bx
    mov cx, OS_FIRMWARE_VGA_ATTRIBUTE_REGISTER_COUNT

os_firmware_initialize_vga_attribute_register:
    mov dx, OS_FIRMWARE_VGA_INPUT_STATUS_PORT
    in al, dx
    mov dx, OS_FIRMWARE_VGA_ATTRIBUTE_INDEX_PORT
    mov al, bl
    out dx, al
    cs lodsb
    out dx, al
    inc bl
    loop os_firmware_initialize_vga_attribute_register
    mov dx, OS_FIRMWARE_VGA_INPUT_STATUS_PORT
    in al, dx
    mov dx, OS_FIRMWARE_VGA_ATTRIBUTE_INDEX_PORT
    mov al, 0x20
    out dx, al

    call os_firmware_program_vga_palette
    call os_firmware_load_vga_font

    mov ax, OS_FIRMWARE_VGA_TEXT_SEGMENT
    mov es, ax
    xor di, di
    mov ax, OS_FIRMWARE_VGA_BLANK_CELL
    mov cx, OS_FIRMWARE_VGA_CELL_COUNT
    rep stosw
    mov ax, OS_FIRMWARE_VGA_STATE_SEGMENT
    mov es, ax
    ; magic 最后提交，宿主看到它时 VGA 模式、字形和其余头字段都已经可用。
    mov dword [es:OS_FIRMWARE_VGA_STATE_MAGIC_OFFSET], OS_FIRMWARE_VGA_STATE_MAGIC
    call os_firmware_update_vga_cursor

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

os_firmware_program_vga_indexed_registers:
    mov al, bl
    out dx, al
    inc dx
    cs lodsb
    out dx, al
    dec dx
    inc bl
    loop os_firmware_program_vga_indexed_registers
    ret

os_firmware_load_vga_font:
    mov dx, OS_FIRMWARE_VGA_SEQUENCER_INDEX_PORT
    mov al, 0x02
    mov ah, 0x04
    call os_firmware_write_vga_indexed_register
    mov al, 0x04
    mov ah, 0x07
    call os_firmware_write_vga_indexed_register
    mov dx, OS_FIRMWARE_VGA_GRAPHICS_INDEX_PORT
    mov al, 0x04
    mov ah, 0x02
    call os_firmware_write_vga_indexed_register
    mov al, 0x05
    mov ah, 0x00
    call os_firmware_write_vga_indexed_register
    mov al, 0x06
    mov ah, 0x00
    call os_firmware_write_vga_indexed_register

    mov ax, OS_FIRMWARE_VGA_FONT_SEGMENT
    mov es, ax
    mov di, OS_FIRMWARE_VGA_FONT_DESTINATION_OFFSET
    mov si, OS_FIRMWARE_VGA_FONT_RUNTIME_OFFSET
    mov cx, OS_FIRMWARE_VGA_FONT_CHARACTER_COUNT

os_firmware_load_vga_font_character:
    mov bx, OS_FIRMWARE_VGA_FONT_SOURCE_ROWS

os_firmware_load_vga_font_row:
    cs lodsb
    stosb
    stosb
    dec bx
    jnz os_firmware_load_vga_font_row
    add di, OS_FIRMWARE_VGA_FONT_SLOT_BYTES \
        - OS_FIRMWARE_VGA_FONT_SOURCE_ROWS * 2
    loop os_firmware_load_vga_font_character

    mov dx, OS_FIRMWARE_VGA_SEQUENCER_INDEX_PORT
    mov al, 0x02
    mov ah, 0x03
    call os_firmware_write_vga_indexed_register
    mov al, 0x04
    mov ah, 0x03
    call os_firmware_write_vga_indexed_register
    mov dx, OS_FIRMWARE_VGA_GRAPHICS_INDEX_PORT
    mov al, 0x04
    mov ah, 0x00
    call os_firmware_write_vga_indexed_register
    mov al, 0x05
    mov ah, 0x10
    call os_firmware_write_vga_indexed_register
    mov al, 0x06
    mov ah, 0x0E
    call os_firmware_write_vga_indexed_register
    ret

os_firmware_write_vga_indexed_register:
    out dx, al
    inc dx
    mov al, ah
    out dx, al
    dec dx
    ret

os_firmware_program_vga_palette:
    mov dx, OS_FIRMWARE_VGA_DAC_MASK_PORT
    mov al, 0xFF
    out dx, al
    mov si, OS_FIRMWARE_ENTRY_RUNTIME_OFFSET \
        + (os_firmware_vga_palette - $$)
    mov cx, OS_FIRMWARE_VGA_PALETTE_COLOR_COUNT

os_firmware_program_vga_palette_color:
    mov dx, OS_FIRMWARE_VGA_DAC_WRITE_INDEX_PORT
    cs lodsb
    out dx, al
    mov dx, OS_FIRMWARE_VGA_DAC_DATA_PORT
    cs lodsb
    out dx, al
    cs lodsb
    out dx, al
    cs lodsb
    out dx, al
    loop os_firmware_program_vga_palette_color
    ret

os_firmware_validate_vga_console:
%ifdef OS_FIRMWARE_TEST_FORCE_VGA_FAILURE
    clc
    ret
%endif
    push ax
    push bx
    push di
    push es
    mov ax, OS_FIRMWARE_VGA_TEXT_SEGMENT
    mov es, ax
    mov di, OS_FIRMWARE_VGA_CELL_COUNT * 2 - 2
    mov bx, [es:di]
    mov ax, 0x0F56
    mov [es:di], ax
    cmp [es:di], ax
    mov [es:di], bx
    pop es
    pop di
    pop bx
    pop ax
    jne os_firmware_validate_vga_console_failed
    stc
    ret

os_firmware_validate_vga_console_failed:
    clc
    ret

os_firmware_initialize_pit:
    mov al, OS_FIRMWARE_PIT_CHANNEL_ZERO_MODE_TWO
    out OS_FIRMWARE_PIT_COMMAND_PORT, al
    mov al, OS_FIRMWARE_PIT_DIVISOR_LOW
    out OS_FIRMWARE_PIT_CHANNEL_ZERO_PORT, al
    mov al, OS_FIRMWARE_PIT_DIVISOR_HIGH
    out OS_FIRMWARE_PIT_CHANNEL_ZERO_PORT, al
    ret

os_firmware_write_string:
    cs lodsb
    test al, al
    jz os_firmware_write_string_complete

    call os_firmware_write_byte
    jnc os_firmware_write_string_failed
    jmp os_firmware_write_string

os_firmware_write_string_complete:
    stc
    ret

os_firmware_write_string_failed:
    clc
    ret

os_firmware_write_byte:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    push fs
    mov bl, al
    mov ax, OS_FIRMWARE_VGA_STATE_SEGMENT
    mov fs, ax
    cmp dword [fs:OS_FIRMWARE_VGA_STATE_MAGIC_OFFSET], OS_FIRMWARE_VGA_STATE_MAGIC
    jne os_firmware_write_byte_failed
    cmp dword [fs:OS_FIRMWARE_VGA_STATE_VERSION_OFFSET], OS_FIRMWARE_VGA_STATE_VERSION
    jne os_firmware_write_byte_failed
    mov eax, [fs:OS_FIRMWARE_VGA_STATE_TRACE_LENGTH_OFFSET]
    cmp eax, OS_FIRMWARE_VGA_STATE_TRACE_CAPACITY
    jae os_firmware_write_byte_overflow
    mov di, ax
    add di, OS_FIRMWARE_VGA_STATE_TRACE_OFFSET
    mov [fs:di], bl
    inc eax
    mov [fs:OS_FIRMWARE_VGA_STATE_TRACE_LENGTH_OFFSET], eax

    cmp bl, OS_FIRMWARE_VGA_CARRIAGE_RETURN
    je os_firmware_write_byte_carriage_return
    cmp bl, OS_FIRMWARE_VGA_LINE_FEED
    je os_firmware_write_byte_line_feed
    cmp bl, OS_FIRMWARE_VGA_BACKSPACE
    je os_firmware_write_byte_backspace
    cmp bl, OS_FIRMWARE_VGA_TAB
    je os_firmware_write_byte_tab
    cmp bl, OS_FIRMWARE_VGA_FIRST_PRINTABLE
    jb os_firmware_write_byte_render_complete
    cmp bl, OS_FIRMWARE_VGA_LAST_PRINTABLE
    ja os_firmware_write_byte_render_complete
    call os_firmware_put_vga_character
    jmp os_firmware_write_byte_render_complete

os_firmware_write_byte_carriage_return:
    mov word [fs:OS_FIRMWARE_VGA_STATE_CURSOR_COLUMN_OFFSET], 0
    jmp os_firmware_write_byte_render_complete

os_firmware_write_byte_line_feed:
    call os_firmware_advance_vga_line
    jmp os_firmware_write_byte_render_complete

os_firmware_write_byte_backspace:
    mov ax, [fs:OS_FIRMWARE_VGA_STATE_CURSOR_COLUMN_OFFSET]
    test ax, ax
    jz os_firmware_write_byte_render_complete
    dec ax
    mov [fs:OS_FIRMWARE_VGA_STATE_CURSOR_COLUMN_OFFSET], ax
    push bx
    mov bl, 0x20
    call os_firmware_put_vga_character
    pop bx
    dec word [fs:OS_FIRMWARE_VGA_STATE_CURSOR_COLUMN_OFFSET]
    jmp os_firmware_write_byte_render_complete

os_firmware_write_byte_tab:
    push bx
    mov bl, 0x20

os_firmware_write_byte_tab_next:
    call os_firmware_put_vga_character
    mov ax, [fs:OS_FIRMWARE_VGA_STATE_CURSOR_COLUMN_OFFSET]
    xor dx, dx
    mov cx, OS_FIRMWARE_VGA_TAB_WIDTH
    div cx
    test dx, dx
    jnz os_firmware_write_byte_tab_next
    pop bx

os_firmware_write_byte_render_complete:
    call os_firmware_update_vga_cursor
    pop fs
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

os_firmware_write_byte_overflow:
    mov dword [fs:OS_FIRMWARE_VGA_STATE_TRACE_OVERFLOW_OFFSET], 1

os_firmware_write_byte_failed:
    pop fs
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

os_firmware_put_vga_character:
    mov ax, OS_FIRMWARE_VGA_TEXT_SEGMENT
    mov es, ax
    mov ax, [fs:OS_FIRMWARE_VGA_STATE_CURSOR_ROW_OFFSET]
    mov dx, OS_FIRMWARE_VGA_COLUMN_COUNT
    mul dx
    add ax, [fs:OS_FIRMWARE_VGA_STATE_CURSOR_COLUMN_OFFSET]
    shl ax, 1
    mov di, ax
    mov al, bl
    mov ah, [fs:OS_FIRMWARE_VGA_STATE_ATTRIBUTE_OFFSET]
    mov [es:di], ax
    mov ax, [fs:OS_FIRMWARE_VGA_STATE_CURSOR_COLUMN_OFFSET]
    inc ax
    cmp ax, OS_FIRMWARE_VGA_COLUMN_COUNT
    jae os_firmware_put_vga_character_wrap
    mov [fs:OS_FIRMWARE_VGA_STATE_CURSOR_COLUMN_OFFSET], ax
    ret

os_firmware_put_vga_character_wrap:
    call os_firmware_advance_vga_line
    ret

os_firmware_advance_vga_line:
    mov word [fs:OS_FIRMWARE_VGA_STATE_CURSOR_COLUMN_OFFSET], 0
    mov ax, [fs:OS_FIRMWARE_VGA_STATE_CURSOR_ROW_OFFSET]
    inc ax
    cmp ax, OS_FIRMWARE_VGA_ROW_COUNT
    jae os_firmware_scroll_vga
    mov [fs:OS_FIRMWARE_VGA_STATE_CURSOR_ROW_OFFSET], ax
    ret

os_firmware_scroll_vga:
    mov ax, OS_FIRMWARE_VGA_TEXT_SEGMENT
    mov ds, ax
    mov es, ax
    mov si, OS_FIRMWARE_VGA_ROW_SIZE_BYTES
    xor di, di
    mov cx, (OS_FIRMWARE_VGA_CELL_COUNT - OS_FIRMWARE_VGA_COLUMN_COUNT)
    rep movsw
    mov di, OS_FIRMWARE_VGA_LAST_ROW_OFFSET
    mov ax, OS_FIRMWARE_VGA_BLANK_CELL
    mov cx, OS_FIRMWARE_VGA_COLUMN_COUNT
    rep stosw
    mov word [fs:OS_FIRMWARE_VGA_STATE_CURSOR_ROW_OFFSET], OS_FIRMWARE_VGA_ROW_COUNT - 1
    ret

os_firmware_update_vga_cursor:
    push ax
    push bx
    push dx
    push fs
    mov ax, OS_FIRMWARE_VGA_STATE_SEGMENT
    mov fs, ax
    mov ax, [fs:OS_FIRMWARE_VGA_STATE_CURSOR_ROW_OFFSET]
    mov bx, OS_FIRMWARE_VGA_COLUMN_COUNT
    mul bx
    add ax, [fs:OS_FIRMWARE_VGA_STATE_CURSOR_COLUMN_OFFSET]
    mov bx, ax
    mov dx, OS_FIRMWARE_VGA_CRTC_INDEX_PORT
    mov al, OS_FIRMWARE_VGA_CRTC_CURSOR_HIGH_INDEX
    out dx, al
    mov dx, OS_FIRMWARE_VGA_CRTC_DATA_PORT
    mov al, bh
    out dx, al
    mov dx, OS_FIRMWARE_VGA_CRTC_INDEX_PORT
    mov al, OS_FIRMWARE_VGA_CRTC_CURSOR_LOW_INDEX
    out dx, al
    mov dx, OS_FIRMWARE_VGA_CRTC_DATA_PORT
    mov al, bl
    out dx, al
    pop fs
    pop dx
    pop bx
    pop ax
    ret

os_firmware_halt:
    cli
    hlt
    jmp os_firmware_halt

os_firmware_reset_message:
    db "[OS][FIRMWARE] RESET", 0x0D, 0x0A, 0x00

os_firmware_vga_ready_message:
    db "[OS][FIRMWARE] VGA_READY", 0x0D, 0x0A, 0x00

os_firmware_clock_ready_message:
    db "[OS][FIRMWARE] CLOCK_READY", 0x0D, 0x0A, 0x00

os_firmware_stage1_header_valid_message:
    db "[OS][FIRMWARE] STAGE1_HEADER_VALID", 0x0D, 0x0A, 0x00

os_firmware_stage1_loaded_message:
    db "[OS][FIRMWARE] STAGE1_LOADED", 0x0D, 0x0A, 0x00

os_firmware_ata_timeout_message:
    db "[OS][FIRMWARE] IDE_TIMEOUT", 0x0D, 0x0A, 0x00

os_firmware_ata_error_message:
    db "[OS][FIRMWARE] IDE_ERROR", 0x0D, 0x0A, 0x00

os_firmware_stage1_header_invalid_message:
    db "[OS][FIRMWARE] STAGE1_HEADER_INVALID", 0x0D, 0x0A, 0x00

os_firmware_stage1_checksum_invalid_message:
    db "[OS][FIRMWARE] STAGE1_CHECKSUM_INVALID", 0x0D, 0x0A, 0x00

os_firmware_vga_misc_register:
    db 0x67

os_firmware_vga_sequencer_registers:
    db 0x03, 0x00, 0x03, 0x00, 0x02

os_firmware_vga_crtc_registers:
    db 0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F
    db 0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50
    db 0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3
    db 0xFF

os_firmware_vga_graphics_registers:
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF

os_firmware_vga_attribute_registers:
    db 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07
    db 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F
    db 0x0C, 0x00, 0x0F, 0x08, 0x00

; 属性控制器使用 EGA 兼容 DAC 索引。每项依次为索引、红、绿、蓝，
; DAC 分量是 6 位值；ROM 必须显式初始化，不能依赖外部 BIOS 的遗留调色板。
os_firmware_vga_palette:
    db 0x00, 0x00, 0x00, 0x00
    db 0x01, 0x00, 0x00, 0x2A
    db 0x02, 0x00, 0x2A, 0x00
    db 0x03, 0x00, 0x2A, 0x2A
    db 0x04, 0x2A, 0x00, 0x00
    db 0x05, 0x2A, 0x00, 0x2A
    db 0x14, 0x2A, 0x15, 0x00
    db 0x07, 0x2A, 0x2A, 0x2A
    db 0x38, 0x15, 0x15, 0x15
    db 0x39, 0x15, 0x15, 0x3F
    db 0x3A, 0x15, 0x3F, 0x15
    db 0x3B, 0x15, 0x3F, 0x3F
    db 0x3C, 0x3F, 0x15, 0x15
    db 0x3D, 0x3F, 0x15, 0x3F
    db 0x3E, 0x3F, 0x3F, 0x15
    db 0x3F, 0x3F, 0x3F, 0x3F

section .font progbits alloc noexec nowrite align=1

os_firmware_vga_font:
%include "font8x8_basic.inc"

section .reset_vector progbits alloc exec nowrite align=1

; CPU 复位时 CS 隐藏基址为 0xFFFF0000，16 位近跳转保留该基址并进入 ROM 入口。
db OS_FIRMWARE_NEAR_JUMP_OPCODE
dw OS_FIRMWARE_RESET_JUMP_DISPLACEMENT
