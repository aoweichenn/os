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
OS_STAGE1_PROTECTED_STACK_TOP equ 0x00007000
OS_STAGE1_GDT_NULL_DESCRIPTOR equ 0x0000000000000000
OS_STAGE1_GDT_CODE_DESCRIPTOR equ 0x00CF9A000000FFFF
OS_STAGE1_GDT_DATA_DESCRIPTOR equ 0x00CF92000000FFFF
OS_STAGE1_PML4_ADDRESS equ 0x00010000
OS_STAGE1_PDPT_ADDRESS equ 0x00011000
OS_STAGE1_PD_ADDRESS equ 0x00012000
OS_STAGE1_PAGE_TABLE_TOTAL_DWORD_COUNT equ 0x00000C00
OS_STAGE1_PAGE_TABLE_PRESENT_WRITABLE_FLAGS equ 0x00000003
OS_STAGE1_PAGE_TABLE_LARGE_PAGE_FLAGS equ 0x00000083
OS_STAGE1_PAGE_TABLE_ENTRY_HIGH_DWORD_OFFSET equ 0x00000004
OS_STAGE1_PAGE_TABLE_EMPTY_HIGH_DWORD equ 0x00000000
OS_STAGE1_GDT_LIMIT_INCLUSIVE_ADJUSTMENT equ 0x0001

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
    jmp OS_STAGE1_GDT_CODE_SELECTOR:(OS_STAGE1_LOAD_PHYSICAL_BASE + os_stage1_protected_entry)

[bits 32]
os_stage1_protected_entry:
    mov ax, OS_STAGE1_GDT_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, OS_STAGE1_PROTECTED_STACK_TOP
    mov esi, OS_STAGE1_LOAD_PHYSICAL_BASE + os_stage1_protected_message
    call os_stage1_protected_write_string

    call os_stage1_build_page_tables
    call os_stage1_validate_page_tables
    jnc os_stage1_report_page_tables_invalid
    mov esi, OS_STAGE1_LOAD_PHYSICAL_BASE + os_stage1_page_tables_ready_message
    call os_stage1_protected_write_string

os_stage1_protected_halt:
    hlt
    jmp os_stage1_protected_halt

os_stage1_report_page_tables_invalid:
    mov esi, OS_STAGE1_LOAD_PHYSICAL_BASE + os_stage1_page_tables_invalid_message
    call os_stage1_protected_write_string
    jmp os_stage1_protected_halt

os_stage1_protected_write_string:
    lodsb
    test al, al
    jz os_stage1_protected_write_complete
    call os_stage1_protected_write_byte
    jnc os_stage1_protected_write_complete
    jmp os_stage1_protected_write_string

os_stage1_protected_write_complete:
    ret

os_stage1_protected_write_byte:
    mov bl, al
    mov ecx, OS_STAGE1_COM1_READY_POLL_LIMIT
    mov edx, OS_STAGE1_COM1_BASE_PORT + OS_STAGE1_COM1_LINE_STATUS_OFFSET

os_stage1_protected_write_byte_poll:
    in al, dx
    test al, OS_STAGE1_COM1_TRANSMITTER_EMPTY_BIT
    jnz os_stage1_protected_write_byte_ready
    loop os_stage1_protected_write_byte_poll
    clc
    ret

os_stage1_protected_write_byte_ready:
    mov edx, OS_STAGE1_COM1_BASE_PORT
    mov al, bl
    out dx, al
    stc
    ret

os_stage1_build_page_tables:
    mov edi, OS_STAGE1_PML4_ADDRESS
    xor eax, eax
    mov ecx, OS_STAGE1_PAGE_TABLE_TOTAL_DWORD_COUNT
    rep stosd

    mov dword [OS_STAGE1_PML4_ADDRESS], \
        OS_STAGE1_PDPT_ADDRESS | OS_STAGE1_PAGE_TABLE_PRESENT_WRITABLE_FLAGS
    mov dword [OS_STAGE1_PDPT_ADDRESS], \
        OS_STAGE1_PD_ADDRESS | OS_STAGE1_PAGE_TABLE_PRESENT_WRITABLE_FLAGS
    mov dword [OS_STAGE1_PD_ADDRESS], \
        OS_STAGE1_PAGE_TABLE_LARGE_PAGE_FLAGS
    ret

os_stage1_validate_page_tables:
    cmp dword [OS_STAGE1_PML4_ADDRESS], \
        OS_STAGE1_PDPT_ADDRESS | OS_STAGE1_PAGE_TABLE_PRESENT_WRITABLE_FLAGS
    jne os_stage1_page_tables_invalid
    cmp dword [OS_STAGE1_PML4_ADDRESS \
        + OS_STAGE1_PAGE_TABLE_ENTRY_HIGH_DWORD_OFFSET], \
        OS_STAGE1_PAGE_TABLE_EMPTY_HIGH_DWORD
    jne os_stage1_page_tables_invalid
    cmp dword [OS_STAGE1_PDPT_ADDRESS], \
        OS_STAGE1_PD_ADDRESS | OS_STAGE1_PAGE_TABLE_PRESENT_WRITABLE_FLAGS
    jne os_stage1_page_tables_invalid
    cmp dword [OS_STAGE1_PDPT_ADDRESS \
        + OS_STAGE1_PAGE_TABLE_ENTRY_HIGH_DWORD_OFFSET], \
        OS_STAGE1_PAGE_TABLE_EMPTY_HIGH_DWORD
    jne os_stage1_page_tables_invalid
    cmp dword [OS_STAGE1_PD_ADDRESS], \
        OS_STAGE1_PAGE_TABLE_LARGE_PAGE_FLAGS
    jne os_stage1_page_tables_invalid
    cmp dword [OS_STAGE1_PD_ADDRESS \
        + OS_STAGE1_PAGE_TABLE_ENTRY_HIGH_DWORD_OFFSET], \
        OS_STAGE1_PAGE_TABLE_EMPTY_HIGH_DWORD
    jne os_stage1_page_tables_invalid
    stc
    ret

os_stage1_page_tables_invalid:
    clc
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
    dq OS_STAGE1_GDT_NULL_DESCRIPTOR
    dq OS_STAGE1_GDT_CODE_DESCRIPTOR
    dq OS_STAGE1_GDT_DATA_DESCRIPTOR

os_stage1_gdt_descriptor:
    dw os_stage1_gdt_descriptor - os_stage1_gdt \
        - OS_STAGE1_GDT_LIMIT_INCLUSIVE_ADJUSTMENT
    dd OS_STAGE1_LOAD_PHYSICAL_BASE + os_stage1_gdt

os_stage1_gdt_ready_message:
    db "[OS][STAGE1] GDT_READY", 0x0D, 0x0A, 0x00

os_stage1_protected_message:
    db "[OS][STAGE1] PROTECTED_MODE", 0x0D, 0x0A, 0x00

os_stage1_page_tables_ready_message:
    db "[OS][STAGE1] PAGE_TABLES_READY", 0x0D, 0x0A, 0x00

os_stage1_page_tables_invalid_message:
    db "[OS][STAGE1] PAGE_TABLES_INVALID", 0x0D, 0x0A, 0x00
