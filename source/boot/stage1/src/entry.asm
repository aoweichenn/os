bits 16
org 0

OS_STAGE1_COM1_BASE_PORT equ 0x03F8
OS_STAGE1_COM1_LINE_STATUS_OFFSET equ 0x0005
OS_STAGE1_COM1_TRANSMITTER_EMPTY_BIT equ 0x20
OS_STAGE1_COM1_READY_POLL_LIMIT equ 0xFFFF

os_stage1_entry:
    cli
    cld

    push cs
    pop ds

    mov si, os_stage1_entered_message
    call os_stage1_write_string

os_stage1_halt:
    hlt
    jmp os_stage1_halt

os_stage1_write_string:
    lodsb
    test al, al
    jz os_stage1_write_string_complete

    call os_stage1_write_byte
    jnc os_stage1_write_string_complete
    jmp os_stage1_write_string

os_stage1_write_string_complete:
    ret

os_stage1_write_byte:
    push ax
    mov cx, OS_STAGE1_COM1_READY_POLL_LIMIT
    mov dx, OS_STAGE1_COM1_BASE_PORT \
        + OS_STAGE1_COM1_LINE_STATUS_OFFSET

os_stage1_write_byte_poll:
    in al, dx
    test al, OS_STAGE1_COM1_TRANSMITTER_EMPTY_BIT
    jnz os_stage1_write_byte_ready
    loop os_stage1_write_byte_poll

    pop ax
    clc
    ret

os_stage1_write_byte_ready:
    pop ax
    mov dx, OS_STAGE1_COM1_BASE_PORT
    out dx, al
    stc
    ret

os_stage1_entered_message:
    db "[OS][STAGE1] ENTERED", 0x0D, 0x0A, 0x00
