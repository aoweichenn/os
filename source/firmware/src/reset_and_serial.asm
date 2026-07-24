bits 16

OS_FIRMWARE_ENTRY_RUNTIME_OFFSET equ 0xF000
OS_FIRMWARE_STACK_TOP_ADDRESS equ 0x7000
OS_FIRMWARE_STAGE1_DESCRIPTOR_BUFFER_ADDRESS equ 0x0500
OS_FIRMWARE_WORD_SIZE_BYTES equ 0x0002

OS_FIRMWARE_COM1_BASE_PORT equ 0x03F8
OS_FIRMWARE_COM1_INTERRUPT_ENABLE_OFFSET equ 0x0001
OS_FIRMWARE_COM1_FIFO_CONTROL_OFFSET equ 0x0002
OS_FIRMWARE_COM1_LINE_CONTROL_OFFSET equ 0x0003
OS_FIRMWARE_COM1_MODEM_CONTROL_OFFSET equ 0x0004
OS_FIRMWARE_COM1_LINE_STATUS_OFFSET equ 0x0005
OS_FIRMWARE_COM1_DIVISOR_LOW_OFFSET equ 0x0000
OS_FIRMWARE_COM1_DIVISOR_HIGH_OFFSET equ 0x0001

OS_FIRMWARE_COM1_DISABLE_INTERRUPTS equ 0x00
OS_FIRMWARE_COM1_ENABLE_DLAB equ 0x80
OS_FIRMWARE_COM1_BAUD_DIVISOR_LOW equ 0x01
OS_FIRMWARE_COM1_BAUD_DIVISOR_HIGH equ 0x00
OS_FIRMWARE_COM1_LINE_8N1 equ 0x03
OS_FIRMWARE_COM1_FIFO_CONFIGURATION equ 0xC7
OS_FIRMWARE_COM1_MODEM_CONFIGURATION equ 0x0B
OS_FIRMWARE_COM1_TRANSMITTER_EMPTY_BIT equ 0x20
OS_FIRMWARE_COM1_READY_POLL_LIMIT equ 0xFFFF

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
OS_FIRMWARE_RESULT_SERIAL_FAILURE equ 0x05

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

    call os_firmware_initialize_com1

    mov si, OS_FIRMWARE_ENTRY_RUNTIME_OFFSET \
        + (os_firmware_reset_message - $$)
    call os_firmware_write_string
    jnc os_firmware_halt

%ifdef OS_FIRMWARE_TEST_FORCE_SERIAL_FAILURE
    ; 仅在失败镜像中改变第二条日志的就绪观测，第一条复位证据仍走真实串口。
    inc bp
%endif

    mov si, OS_FIRMWARE_ENTRY_RUNTIME_OFFSET \
        + (os_firmware_serial_ready_message - $$)
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
    jnc os_firmware_load_stage1_serial_failed

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

os_firmware_load_stage1_serial_failed:
    mov al, OS_FIRMWARE_RESULT_SERIAL_FAILURE

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

os_firmware_initialize_com1:
    mov dx, OS_FIRMWARE_COM1_BASE_PORT \
        + OS_FIRMWARE_COM1_INTERRUPT_ENABLE_OFFSET
    mov al, OS_FIRMWARE_COM1_DISABLE_INTERRUPTS
    out dx, al

    mov dx, OS_FIRMWARE_COM1_BASE_PORT \
        + OS_FIRMWARE_COM1_LINE_CONTROL_OFFSET
    mov al, OS_FIRMWARE_COM1_ENABLE_DLAB
    out dx, al

    mov dx, OS_FIRMWARE_COM1_BASE_PORT \
        + OS_FIRMWARE_COM1_DIVISOR_LOW_OFFSET
    mov al, OS_FIRMWARE_COM1_BAUD_DIVISOR_LOW
    out dx, al

    mov dx, OS_FIRMWARE_COM1_BASE_PORT \
        + OS_FIRMWARE_COM1_DIVISOR_HIGH_OFFSET
    mov al, OS_FIRMWARE_COM1_BAUD_DIVISOR_HIGH
    out dx, al

    mov dx, OS_FIRMWARE_COM1_BASE_PORT \
        + OS_FIRMWARE_COM1_LINE_CONTROL_OFFSET
    mov al, OS_FIRMWARE_COM1_LINE_8N1
    out dx, al

    mov dx, OS_FIRMWARE_COM1_BASE_PORT \
        + OS_FIRMWARE_COM1_FIFO_CONTROL_OFFSET
    mov al, OS_FIRMWARE_COM1_FIFO_CONFIGURATION
    out dx, al

    mov dx, OS_FIRMWARE_COM1_BASE_PORT \
        + OS_FIRMWARE_COM1_MODEM_CONTROL_OFFSET
    mov al, OS_FIRMWARE_COM1_MODEM_CONFIGURATION
    out dx, al
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
    call os_firmware_wait_for_transmitter
    jnc os_firmware_write_byte_failed

    pop ax
    mov dx, OS_FIRMWARE_COM1_BASE_PORT
    out dx, al
    stc
    ret

os_firmware_write_byte_failed:
    pop ax
    clc
    ret

os_firmware_wait_for_transmitter:
    mov cx, OS_FIRMWARE_COM1_READY_POLL_LIMIT
    mov dx, OS_FIRMWARE_COM1_BASE_PORT \
        + OS_FIRMWARE_COM1_LINE_STATUS_OFFSET

os_firmware_wait_for_transmitter_poll:
    test bp, bp
    jne os_firmware_wait_for_transmitter_next

    in al, dx
    test al, OS_FIRMWARE_COM1_TRANSMITTER_EMPTY_BIT
    jnz os_firmware_wait_for_transmitter_ready

os_firmware_wait_for_transmitter_next:
    loop os_firmware_wait_for_transmitter_poll
    clc
    ret

os_firmware_wait_for_transmitter_ready:
    stc
    ret

os_firmware_halt:
    cli
    hlt
    jmp os_firmware_halt

os_firmware_reset_message:
    db "[OS][FIRMWARE] RESET", 0x0D, 0x0A, 0x00

os_firmware_serial_ready_message:
    db "[OS][FIRMWARE] SERIAL_READY", 0x0D, 0x0A, 0x00

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

section .reset_vector progbits alloc exec nowrite align=1

; CPU 复位时 CS 隐藏基址为 0xFFFF0000，16 位近跳转保留该基址并进入 ROM 入口。
db OS_FIRMWARE_NEAR_JUMP_OPCODE
dw OS_FIRMWARE_RESET_JUMP_DISPLACEMENT
