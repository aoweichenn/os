bits 16

OS_FIRMWARE_ENTRY_RUNTIME_OFFSET equ 0xF000
OS_FIRMWARE_STACK_TOP_ADDRESS equ 0x7000

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
    jnc os_firmware_serial_failure

%ifdef OS_FIRMWARE_TEST_FORCE_SERIAL_FAILURE
    ; 仅在失败路径镜像中屏蔽就绪位，验证轮询有界且错误能够被宿主机观察。
    inc bp
%endif

    mov si, OS_FIRMWARE_ENTRY_RUNTIME_OFFSET \
        + (os_firmware_serial_ready_message - $$)
    call os_firmware_write_string
    jnc os_firmware_serial_failure

    jmp os_firmware_halt

os_firmware_serial_failure:
    jmp os_firmware_halt

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

section .reset_vector progbits alloc exec nowrite align=1

; CPU 复位时 CS 隐藏基址为 0xFFFF0000，16 位近跳转保留该基址并进入 ROM 入口。
db OS_FIRMWARE_NEAR_JUMP_OPCODE
dw OS_FIRMWARE_RESET_JUMP_DISPLACEMENT
