bits 16
org 0

OS_STAGE1_COM1_BASE_PORT equ 0x03F8
OS_STAGE1_COM1_LINE_STATUS_OFFSET equ 0x0005
OS_STAGE1_COM1_TRANSMITTER_EMPTY_BIT equ 0x20
OS_STAGE1_COM1_READY_POLL_LIMIT equ 0xFFFF
OS_STAGE1_GDT_CODE_SELECTOR equ 0x0008
OS_STAGE1_GDT_DATA_SELECTOR equ 0x0010
OS_STAGE1_LOAD_PHYSICAL_BASE equ 0x8000
OS_STAGE1_CR0_PROTECTED_MODE_BIT equ 0x00000001
OS_STAGE1_PROTECTED_STACK_TOP equ 0x00009000

os_stage1_entry:
    cli
    cld

    push cs
    pop ds

    mov si, os_stage1_entered_message
    call os_stage1_write_string

    lgdt [os_stage1_gdt_descriptor]
    mov si, os_stage1_gdt_ready_message
    call os_stage1_write_string

    mov eax, cr0
    or eax, OS_STAGE1_CR0_PROTECTED_MODE_BIT
    mov cr0, eax
    jmp OS_STAGE1_GDT_CODE_SELECTOR:os_stage1_protected_entry

[bits 32]
os_stage1_protected_entry:
    mov ax, OS_STAGE1_GDT_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, OS_STAGE1_PROTECTED_STACK_TOP
    mov esi, os_stage1_protected_message
    call os_stage1_protected_write_string

os_stage1_protected_halt:
    hlt
    jmp os_stage1_protected_halt

os_stage1_protected_write_string:
    lodsb
    test al, al
    jz os_stage1_protected_write_complete
    call os_stage1_protected_write_byte
    jmp os_stage1_protected_write_string

os_stage1_protected_write_complete:
    ret

os_stage1_protected_write_byte:
    mov bl, al
    mov edx, OS_STAGE1_COM1_BASE_PORT + OS_STAGE1_COM1_LINE_STATUS_OFFSET
    in al, dx
    test al, OS_STAGE1_COM1_TRANSMITTER_EMPTY_BIT
    jz os_stage1_protected_write_byte
    mov edx, OS_STAGE1_COM1_BASE_PORT
    mov al, bl
    out dx, al
    ret

[bits 16]


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

os_stage1_gdt:
    dq 0x0000000000000000
    dq 0x00CF9A008000FFFF
    dq 0x00CF92008000FFFF

os_stage1_gdt_descriptor:
    dw os_stage1_gdt_descriptor - os_stage1_gdt - 1
    dd OS_STAGE1_LOAD_PHYSICAL_BASE + os_stage1_gdt

os_stage1_gdt_ready_message:
    db "[OS][STAGE1] GDT_READY", 0x0D, 0x0A, 0x00

os_stage1_protected_message:
    db "[OS][STAGE1] PROTECTED_MODE", 0x0D, 0x0A, 0x00
