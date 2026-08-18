bits 16
org 0

%include "kernel_loader.inc"
%include "vga_console.inc"

OS_STAGE1_GDT_CODE_SELECTOR equ 0x0008
OS_STAGE1_GDT_DATA_SELECTOR equ 0x0010
OS_STAGE1_GDT_LONG_CODE_SELECTOR equ 0x0018
OS_STAGE1_A20_CONTROL_PORT equ 0x0092
OS_STAGE1_A20_ENABLE_BIT equ 0x02
OS_STAGE1_A20_RESET_DISABLE_MASK equ 0xFE
OS_STAGE1_A20_LOW_SEGMENT equ 0x0000
OS_STAGE1_A20_LOW_OFFSET equ 0x0000
OS_STAGE1_A20_HIGH_SEGMENT equ 0xFFFF
OS_STAGE1_A20_HIGH_OFFSET equ 0x0010
OS_STAGE1_A20_LOW_TEST_PATTERN equ 0x5A
OS_STAGE1_A20_HIGH_TEST_PATTERN equ 0xA5
OS_STAGE1_LOAD_PHYSICAL_BASE equ 0x8000
OS_STAGE1_CR0_PROTECTED_MODE_BIT equ 0x00000001
OS_STAGE1_PROTECTED_STACK_TOP equ 0x00007000
OS_STAGE1_GDT_NULL_DESCRIPTOR equ 0x0000000000000000
OS_STAGE1_GDT_CODE_DESCRIPTOR equ 0x00CF9A000000FFFF
OS_STAGE1_GDT_DATA_DESCRIPTOR equ 0x00CF92000000FFFF
OS_STAGE1_GDT_LONG_CODE_DESCRIPTOR equ 0x00AF9A000000FFFF
OS_STAGE1_PML4_ADDRESS equ 0x00010000
OS_STAGE1_PDPT_ADDRESS equ 0x00011000
OS_STAGE1_PD_ADDRESS equ 0x00012000
OS_STAGE1_PAGE_TABLE_TOTAL_DWORD_COUNT equ 0x00000C00
OS_STAGE1_PAGE_TABLE_PRESENT_WRITABLE_FLAGS equ 0x00000003
OS_STAGE1_PAGE_TABLE_LARGE_PAGE_FLAGS equ 0x00000083
OS_STAGE1_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES equ 0x00200000
OS_STAGE1_PAGE_DIRECTORY_ENTRY_SIZE_BYTES equ 0x00000008
OS_STAGE1_PAGE_DIRECTORY_ENTRY_COUNT equ 0x00000020
OS_STAGE1_IDENTITY_MAPPED_SIZE_BYTES equ 0x04000000
OS_STAGE1_PAGE_TABLE_ENTRY_HIGH_DWORD_OFFSET equ 0x00000004
OS_STAGE1_PAGE_TABLE_EMPTY_HIGH_DWORD equ 0x00000000
OS_STAGE1_GDT_LIMIT_INCLUSIVE_ADJUSTMENT equ 0x0001
OS_STAGE1_CR4_PAE_BIT equ 0x00000020
OS_STAGE1_IA32_EFER_MSR equ 0xC0000080
OS_STAGE1_IA32_EFER_LME_BIT equ 0x00000100
OS_STAGE1_IA32_EFER_LMA_BIT equ 0x00000400
OS_STAGE1_CR0_PAGING_BIT equ 0x80000000
OS_STAGE1_KERNEL_STACK_TOP equ 0x03FFF000

os_stage1_entry:
    cli
    cld

    push cs
    pop ds

    call os_stage1_enable_a20
    call os_stage1_validate_a20
    jnc os_stage1_report_a20_invalid
    mov si, os_stage1_a20_ready_message
    call os_stage1_write_string

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

    call os_stage1_activate_pae
    jnc os_stage1_report_pae_invalid
    mov esi, OS_STAGE1_LOAD_PHYSICAL_BASE + os_stage1_pae_ready_message
    call os_stage1_protected_write_string

    call os_stage1_activate_long_mode_enable
    jnc os_stage1_report_long_mode_enable_invalid
    mov esi, OS_STAGE1_LOAD_PHYSICAL_BASE + os_stage1_long_mode_enable_ready_message
    call os_stage1_protected_write_string

    call os_stage1_activate_paging
    jnc os_stage1_report_paging_invalid
    mov esi, OS_STAGE1_LOAD_PHYSICAL_BASE + os_stage1_paging_enabled_message
    call os_stage1_protected_write_string

    jmp OS_STAGE1_GDT_LONG_CODE_SELECTOR:(OS_STAGE1_LOAD_PHYSICAL_BASE + os_stage1_long_mode_entry)

os_stage1_protected_halt:
    hlt
    jmp os_stage1_protected_halt

os_stage1_report_page_tables_invalid:
    mov esi, OS_STAGE1_LOAD_PHYSICAL_BASE + os_stage1_page_tables_invalid_message
    call os_stage1_protected_write_string
    jmp os_stage1_protected_halt

os_stage1_report_pae_invalid:
    mov esi, OS_STAGE1_LOAD_PHYSICAL_BASE + os_stage1_pae_invalid_message
    call os_stage1_protected_write_string
    jmp os_stage1_protected_halt

os_stage1_report_long_mode_enable_invalid:
    mov esi, OS_STAGE1_LOAD_PHYSICAL_BASE + os_stage1_long_mode_enable_invalid_message
    call os_stage1_protected_write_string
    jmp os_stage1_protected_halt

os_stage1_report_paging_invalid:
    mov esi, OS_STAGE1_LOAD_PHYSICAL_BASE + os_stage1_paging_invalid_message
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
    push eax
    push ebx
    push ecx
    push edx
    push esi
    push edi
    mov bl, al
    cmp dword [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_MAGIC_OFFSET], \
        OS_STAGE1_VGA_STATE_MAGIC
    jne os_stage1_protected_write_byte_failed
    cmp dword [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_VERSION_OFFSET], \
        OS_STAGE1_VGA_STATE_VERSION
    jne os_stage1_protected_write_byte_failed
    mov eax, [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_TRACE_LENGTH_OFFSET]
    cmp eax, OS_STAGE1_VGA_STATE_TRACE_CAPACITY
    jae os_stage1_protected_write_byte_overflow
    mov [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_TRACE_OFFSET + eax], bl
    inc eax
    mov [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_TRACE_LENGTH_OFFSET], eax

    cmp bl, OS_STAGE1_VGA_CARRIAGE_RETURN
    je os_stage1_protected_write_byte_carriage_return
    cmp bl, OS_STAGE1_VGA_LINE_FEED
    je os_stage1_protected_write_byte_line_feed
    cmp bl, OS_STAGE1_VGA_FIRST_PRINTABLE
    jb os_stage1_protected_write_byte_render_complete
    cmp bl, OS_STAGE1_VGA_LAST_PRINTABLE
    ja os_stage1_protected_write_byte_render_complete
    call os_stage1_protected_put_vga_character
    jmp os_stage1_protected_write_byte_render_complete

os_stage1_protected_write_byte_carriage_return:
    mov word [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_COLUMN_OFFSET], 0
    jmp os_stage1_protected_write_byte_render_complete

os_stage1_protected_write_byte_line_feed:
    call os_stage1_protected_advance_vga_line

os_stage1_protected_write_byte_render_complete:
    call os_stage1_protected_update_vga_cursor
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop eax
    stc
    ret

os_stage1_protected_write_byte_overflow:
    mov dword [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_TRACE_OVERFLOW_OFFSET], 1

os_stage1_protected_write_byte_failed:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop eax
    clc
    ret

os_stage1_protected_put_vga_character:
    movzx eax, word [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_ROW_OFFSET]
    imul eax, OS_STAGE1_VGA_COLUMN_COUNT
    movzx edx, word [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_COLUMN_OFFSET]
    add eax, edx
    shl eax, 1
    mov edi, eax
    mov ah, [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_ATTRIBUTE_OFFSET]
    mov al, bl
    mov [OS_STAGE1_VGA_TEXT_ADDRESS + edi], ax
    mov ax, [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_COLUMN_OFFSET]
    inc ax
    cmp ax, OS_STAGE1_VGA_COLUMN_COUNT
    jae os_stage1_protected_put_vga_character_wrap
    mov [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_COLUMN_OFFSET], ax
    ret

os_stage1_protected_put_vga_character_wrap:
    call os_stage1_protected_advance_vga_line
    ret

os_stage1_protected_advance_vga_line:
    mov word [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_COLUMN_OFFSET], 0
    mov ax, [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_ROW_OFFSET]
    inc ax
    cmp ax, OS_STAGE1_VGA_ROW_COUNT
    jae os_stage1_protected_scroll_vga
    mov [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_ROW_OFFSET], ax
    ret

os_stage1_protected_scroll_vga:
    mov esi, OS_STAGE1_VGA_TEXT_ADDRESS + OS_STAGE1_VGA_ROW_SIZE_BYTES
    mov edi, OS_STAGE1_VGA_TEXT_ADDRESS
    mov ecx, (OS_STAGE1_VGA_CELL_COUNT - OS_STAGE1_VGA_COLUMN_COUNT) / 2
    rep movsd
    mov edi, OS_STAGE1_VGA_TEXT_ADDRESS + OS_STAGE1_VGA_LAST_ROW_OFFSET
    mov ax, OS_STAGE1_VGA_BLANK_CELL
    mov ecx, OS_STAGE1_VGA_COLUMN_COUNT
    rep stosw
    mov word [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_ROW_OFFSET], \
        OS_STAGE1_VGA_ROW_COUNT - 1
    ret

os_stage1_protected_update_vga_cursor:
    movzx eax, word [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_ROW_OFFSET]
    imul eax, OS_STAGE1_VGA_COLUMN_COUNT
    movzx ebx, word [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_COLUMN_OFFSET]
    add eax, ebx
    mov ebx, eax
    mov edx, OS_STAGE1_VGA_CRTC_INDEX_PORT
    mov al, OS_STAGE1_VGA_CRTC_CURSOR_HIGH_INDEX
    out dx, al
    mov edx, OS_STAGE1_VGA_CRTC_DATA_PORT
    mov al, bh
    out dx, al
    mov edx, OS_STAGE1_VGA_CRTC_INDEX_PORT
    mov al, OS_STAGE1_VGA_CRTC_CURSOR_LOW_INDEX
    out dx, al
    mov edx, OS_STAGE1_VGA_CRTC_DATA_PORT
    mov al, bl
    out dx, al
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
    mov edi, OS_STAGE1_PD_ADDRESS
    mov eax, OS_STAGE1_PAGE_TABLE_LARGE_PAGE_FLAGS
    mov ecx, OS_STAGE1_PAGE_DIRECTORY_ENTRY_COUNT

os_stage1_build_page_directory_entry:
    mov dword [edi], eax
    mov dword [ \
        edi + OS_STAGE1_PAGE_TABLE_ENTRY_HIGH_DWORD_OFFSET \
    ], OS_STAGE1_PAGE_TABLE_EMPTY_HIGH_DWORD
    add eax, OS_STAGE1_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES
    add edi, OS_STAGE1_PAGE_DIRECTORY_ENTRY_SIZE_BYTES
    loop os_stage1_build_page_directory_entry
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
    mov edi, OS_STAGE1_PD_ADDRESS
    mov eax, OS_STAGE1_PAGE_TABLE_LARGE_PAGE_FLAGS
    mov ecx, OS_STAGE1_PAGE_DIRECTORY_ENTRY_COUNT

os_stage1_validate_page_directory_entry:
    cmp dword [edi], eax
    jne os_stage1_page_tables_invalid
    cmp dword [ \
        edi + OS_STAGE1_PAGE_TABLE_ENTRY_HIGH_DWORD_OFFSET \
    ], OS_STAGE1_PAGE_TABLE_EMPTY_HIGH_DWORD
    jne os_stage1_page_tables_invalid
    add eax, OS_STAGE1_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES
    add edi, OS_STAGE1_PAGE_DIRECTORY_ENTRY_SIZE_BYTES
    loop os_stage1_validate_page_directory_entry
    stc
    ret

os_stage1_page_tables_invalid:
    clc
    ret

os_stage1_activate_pae:
    mov eax, cr4
    or eax, OS_STAGE1_CR4_PAE_BIT
    mov cr4, eax
    mov eax, OS_STAGE1_PML4_ADDRESS
    mov cr3, eax

    mov eax, cr4
    test eax, OS_STAGE1_CR4_PAE_BIT
    jz os_stage1_pae_invalid
    mov eax, cr3
    cmp eax, OS_STAGE1_PML4_ADDRESS
    jne os_stage1_pae_invalid
    stc
    ret

os_stage1_pae_invalid:
    clc
    ret

os_stage1_activate_long_mode_enable:
    mov ecx, OS_STAGE1_IA32_EFER_MSR
    rdmsr
    or eax, OS_STAGE1_IA32_EFER_LME_BIT
    wrmsr

    rdmsr
    test eax, OS_STAGE1_IA32_EFER_LME_BIT
    jz os_stage1_long_mode_enable_invalid
    stc
    ret

os_stage1_long_mode_enable_invalid:
    clc
    ret

os_stage1_activate_paging:
    mov eax, cr0
    or eax, OS_STAGE1_CR0_PAGING_BIT
    mov cr0, eax

    mov eax, cr0
    test eax, OS_STAGE1_CR0_PAGING_BIT
    jz os_stage1_paging_invalid
    mov ecx, OS_STAGE1_IA32_EFER_MSR
    rdmsr
    test eax, OS_STAGE1_IA32_EFER_LMA_BIT
    jz os_stage1_paging_invalid
    stc
    ret

os_stage1_paging_invalid:
    clc
    ret

[bits 64]
default abs
os_stage1_long_mode_entry:
    mov ax, OS_STAGE1_GDT_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, OS_STAGE1_PROTECTED_STACK_TOP
    mov rsi, OS_STAGE1_LOAD_PHYSICAL_BASE + os_stage1_long_mode_message
    call os_stage1_long_write_string

    call os_stage1_discover_physical_memory
    jnc os_stage1_report_memory_map_invalid
    call os_stage1_validate_kernel_staging_memory
    jnc os_stage1_report_memory_map_invalid
    mov rsi, OS_STAGE1_LOAD_PHYSICAL_BASE \
        + os_stage1_memory_map_ready_message
    call os_stage1_long_write_string

    call os_stage1_load_kernel
    jnc os_stage1_report_kernel_load_failure
    mov rsi, OS_STAGE1_LOAD_PHYSICAL_BASE \
        + os_stage1_kernel_transfer_message
    call os_stage1_long_write_string

    mov rsp, OS_STAGE1_KERNEL_STACK_TOP
    mov rdi, OS_STAGE1_KERNEL_BOOT_INFO_ADDRESS
    mov rax, [ \
        rdi + OS_STAGE1_BOOT_INFO_KERNEL_ENTRY_OFFSET \
    ]
    call rax

    mov rsi, OS_STAGE1_LOAD_PHYSICAL_BASE \
        + os_stage1_kernel_returned_message
    call os_stage1_long_write_string

os_stage1_long_halt:
    hlt
    jmp os_stage1_long_halt

os_stage1_report_kernel_load_failure:
    cmp al, OS_STAGE1_RESULT_ATA_TIMEOUT
    je os_stage1_report_kernel_ata_timeout
    cmp al, OS_STAGE1_RESULT_ATA_DEVICE_ERROR
    je os_stage1_report_kernel_ata_error
    cmp al, OS_STAGE1_RESULT_KERNEL_HEADER_INVALID
    je os_stage1_report_kernel_header_invalid
    cmp al, OS_STAGE1_RESULT_KERNEL_CHECKSUM_INVALID
    je os_stage1_report_kernel_checksum_invalid
    cmp al, OS_STAGE1_RESULT_KERNEL_ELF_INVALID
    je os_stage1_report_kernel_elf_invalid
    jmp os_stage1_long_halt

os_stage1_report_memory_map_invalid:
    mov rsi, OS_STAGE1_LOAD_PHYSICAL_BASE \
        + os_stage1_memory_map_invalid_message
    call os_stage1_long_write_string
    jmp os_stage1_long_halt

os_stage1_report_kernel_ata_timeout:
    mov rsi, OS_STAGE1_LOAD_PHYSICAL_BASE \
        + os_stage1_kernel_ata_timeout_message
    call os_stage1_long_write_string
    jmp os_stage1_long_halt

os_stage1_report_kernel_ata_error:
    mov rsi, OS_STAGE1_LOAD_PHYSICAL_BASE \
        + os_stage1_kernel_ata_error_message
    call os_stage1_long_write_string
    jmp os_stage1_long_halt

os_stage1_report_kernel_header_invalid:
    mov rsi, OS_STAGE1_LOAD_PHYSICAL_BASE \
        + os_stage1_kernel_header_invalid_message
    call os_stage1_long_write_string
    jmp os_stage1_long_halt

os_stage1_report_kernel_checksum_invalid:
    mov rsi, OS_STAGE1_LOAD_PHYSICAL_BASE \
        + os_stage1_kernel_checksum_invalid_message
    call os_stage1_long_write_string
    jmp os_stage1_long_halt

os_stage1_report_kernel_elf_invalid:
    mov rsi, OS_STAGE1_LOAD_PHYSICAL_BASE \
        + os_stage1_kernel_elf_invalid_message
    call os_stage1_long_write_string
    jmp os_stage1_long_halt

os_stage1_long_write_string:
    lodsb
    test al, al
    jz os_stage1_long_write_complete
    call os_stage1_long_write_byte
    jnc os_stage1_long_write_complete
    jmp os_stage1_long_write_string

os_stage1_long_write_complete:
    ret

os_stage1_long_write_byte:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    mov bl, al
    cmp dword [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_MAGIC_OFFSET], \
        OS_STAGE1_VGA_STATE_MAGIC
    jne os_stage1_long_write_byte_failed
    cmp dword [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_VERSION_OFFSET], \
        OS_STAGE1_VGA_STATE_VERSION
    jne os_stage1_long_write_byte_failed
    mov eax, [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_TRACE_LENGTH_OFFSET]
    cmp eax, OS_STAGE1_VGA_STATE_TRACE_CAPACITY
    jae os_stage1_long_write_byte_overflow
    mov [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_TRACE_OFFSET + rax], bl
    inc eax
    mov [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_TRACE_LENGTH_OFFSET], eax

    cmp bl, OS_STAGE1_VGA_CARRIAGE_RETURN
    je os_stage1_long_write_byte_carriage_return
    cmp bl, OS_STAGE1_VGA_LINE_FEED
    je os_stage1_long_write_byte_line_feed
    cmp bl, OS_STAGE1_VGA_FIRST_PRINTABLE
    jb os_stage1_long_write_byte_render_complete
    cmp bl, OS_STAGE1_VGA_LAST_PRINTABLE
    ja os_stage1_long_write_byte_render_complete
    call os_stage1_long_put_vga_character
    jmp os_stage1_long_write_byte_render_complete

os_stage1_long_write_byte_carriage_return:
    mov word [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_COLUMN_OFFSET], 0
    jmp os_stage1_long_write_byte_render_complete

os_stage1_long_write_byte_line_feed:
    call os_stage1_long_advance_vga_line

os_stage1_long_write_byte_render_complete:
    call os_stage1_long_update_vga_cursor
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    stc
    ret

os_stage1_long_write_byte_overflow:
    mov dword [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_TRACE_OVERFLOW_OFFSET], 1

os_stage1_long_write_byte_failed:
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    clc
    ret

os_stage1_long_put_vga_character:
    movzx eax, word [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_ROW_OFFSET]
    imul eax, OS_STAGE1_VGA_COLUMN_COUNT
    movzx edx, word [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_COLUMN_OFFSET]
    add eax, edx
    shl eax, 1
    mov edi, eax
    mov ah, [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_ATTRIBUTE_OFFSET]
    mov al, bl
    mov [OS_STAGE1_VGA_TEXT_ADDRESS + rdi], ax
    mov ax, [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_COLUMN_OFFSET]
    inc ax
    cmp ax, OS_STAGE1_VGA_COLUMN_COUNT
    jae os_stage1_long_put_vga_character_wrap
    mov [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_COLUMN_OFFSET], ax
    ret

os_stage1_long_put_vga_character_wrap:
    call os_stage1_long_advance_vga_line
    ret

os_stage1_long_advance_vga_line:
    mov word [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_COLUMN_OFFSET], 0
    mov ax, [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_ROW_OFFSET]
    inc ax
    cmp ax, OS_STAGE1_VGA_ROW_COUNT
    jae os_stage1_long_scroll_vga
    mov [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_ROW_OFFSET], ax
    ret

os_stage1_long_scroll_vga:
    mov rsi, OS_STAGE1_VGA_TEXT_ADDRESS + OS_STAGE1_VGA_ROW_SIZE_BYTES
    mov rdi, OS_STAGE1_VGA_TEXT_ADDRESS
    mov ecx, (OS_STAGE1_VGA_CELL_COUNT - OS_STAGE1_VGA_COLUMN_COUNT) / 4
    rep movsq
    mov rdi, OS_STAGE1_VGA_TEXT_ADDRESS + OS_STAGE1_VGA_LAST_ROW_OFFSET
    mov ax, OS_STAGE1_VGA_BLANK_CELL
    mov ecx, OS_STAGE1_VGA_COLUMN_COUNT
    rep stosw
    mov word [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_ROW_OFFSET], \
        OS_STAGE1_VGA_ROW_COUNT - 1
    ret

os_stage1_long_update_vga_cursor:
    movzx eax, word [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_ROW_OFFSET]
    imul eax, OS_STAGE1_VGA_COLUMN_COUNT
    movzx ebx, word [OS_STAGE1_VGA_STATE_ADDRESS + OS_STAGE1_VGA_STATE_CURSOR_COLUMN_OFFSET]
    add eax, ebx
    mov ebx, eax
    mov edx, OS_STAGE1_VGA_CRTC_INDEX_PORT
    mov al, OS_STAGE1_VGA_CRTC_CURSOR_HIGH_INDEX
    out dx, al
    mov edx, OS_STAGE1_VGA_CRTC_DATA_PORT
    mov al, bh
    out dx, al
    mov edx, OS_STAGE1_VGA_CRTC_INDEX_PORT
    mov al, OS_STAGE1_VGA_CRTC_CURSOR_LOW_INDEX
    out dx, al
    mov edx, OS_STAGE1_VGA_CRTC_DATA_PORT
    mov al, bl
    out dx, al
    ret

[bits 16]

os_stage1_report_a20_invalid:
    mov si, os_stage1_a20_invalid_message
    call os_stage1_write_string
    jmp os_stage1_halt

os_stage1_enable_a20:
    in al, OS_STAGE1_A20_CONTROL_PORT
    or al, OS_STAGE1_A20_ENABLE_BIT
    and al, OS_STAGE1_A20_RESET_DISABLE_MASK
    out OS_STAGE1_A20_CONTROL_PORT, al
    ret

os_stage1_validate_a20:
    push ds
    push fs
    push si
    push ax
    push bx

    mov ax, OS_STAGE1_A20_LOW_SEGMENT
    mov ds, ax
    mov si, OS_STAGE1_A20_LOW_OFFSET
    mov bl, [ds:si]

    mov ax, OS_STAGE1_A20_HIGH_SEGMENT
    mov fs, ax
    mov si, OS_STAGE1_A20_HIGH_OFFSET
    mov bh, [fs:si]

    mov ax, OS_STAGE1_A20_LOW_SEGMENT
    mov ds, ax
    mov si, OS_STAGE1_A20_LOW_OFFSET
    mov byte [ds:si], OS_STAGE1_A20_LOW_TEST_PATTERN
    mov si, OS_STAGE1_A20_HIGH_OFFSET
    mov byte [fs:si], OS_STAGE1_A20_HIGH_TEST_PATTERN

    mov si, OS_STAGE1_A20_LOW_OFFSET
    mov al, [ds:si]
    mov si, OS_STAGE1_A20_HIGH_OFFSET
    mov ah, [fs:si]
    cmp al, ah
    je os_stage1_a20_invalid
    stc
    jmp os_stage1_restore_a20_test_bytes

os_stage1_a20_invalid:
    clc

os_stage1_restore_a20_test_bytes:
    mov si, OS_STAGE1_A20_HIGH_OFFSET
    mov [fs:si], bh
    mov si, OS_STAGE1_A20_LOW_OFFSET
    mov [ds:si], bl
    pop bx
    pop ax
    pop si
    pop fs
    pop ds
    ret

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
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    push fs
    mov bl, al
    mov ax, OS_STAGE1_VGA_STATE_SEGMENT
    mov fs, ax
    cmp dword [fs:OS_STAGE1_VGA_STATE_MAGIC_OFFSET], OS_STAGE1_VGA_STATE_MAGIC
    jne os_stage1_write_byte_failed
    cmp dword [fs:OS_STAGE1_VGA_STATE_VERSION_OFFSET], OS_STAGE1_VGA_STATE_VERSION
    jne os_stage1_write_byte_failed
    mov eax, [fs:OS_STAGE1_VGA_STATE_TRACE_LENGTH_OFFSET]
    cmp eax, OS_STAGE1_VGA_STATE_TRACE_CAPACITY
    jae os_stage1_write_byte_overflow
    mov di, ax
    add di, OS_STAGE1_VGA_STATE_TRACE_OFFSET
    mov [fs:di], bl
    inc eax
    mov [fs:OS_STAGE1_VGA_STATE_TRACE_LENGTH_OFFSET], eax

    cmp bl, OS_STAGE1_VGA_CARRIAGE_RETURN
    je os_stage1_write_byte_carriage_return
    cmp bl, OS_STAGE1_VGA_LINE_FEED
    je os_stage1_write_byte_line_feed
    cmp bl, OS_STAGE1_VGA_FIRST_PRINTABLE
    jb os_stage1_write_byte_render_complete
    cmp bl, OS_STAGE1_VGA_LAST_PRINTABLE
    ja os_stage1_write_byte_render_complete
    call os_stage1_put_vga_character
    jmp os_stage1_write_byte_render_complete

os_stage1_write_byte_carriage_return:
    mov word [fs:OS_STAGE1_VGA_STATE_CURSOR_COLUMN_OFFSET], 0
    jmp os_stage1_write_byte_render_complete

os_stage1_write_byte_line_feed:
    call os_stage1_advance_vga_line

os_stage1_write_byte_render_complete:
    call os_stage1_update_vga_cursor
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

os_stage1_write_byte_overflow:
    mov dword [fs:OS_STAGE1_VGA_STATE_TRACE_OVERFLOW_OFFSET], 1

os_stage1_write_byte_failed:
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

os_stage1_put_vga_character:
    mov ax, OS_STAGE1_VGA_TEXT_SEGMENT
    mov es, ax
    mov ax, [fs:OS_STAGE1_VGA_STATE_CURSOR_ROW_OFFSET]
    mov dx, OS_STAGE1_VGA_COLUMN_COUNT
    mul dx
    add ax, [fs:OS_STAGE1_VGA_STATE_CURSOR_COLUMN_OFFSET]
    shl ax, 1
    mov di, ax
    mov al, bl
    mov ah, [fs:OS_STAGE1_VGA_STATE_ATTRIBUTE_OFFSET]
    mov [es:di], ax
    mov ax, [fs:OS_STAGE1_VGA_STATE_CURSOR_COLUMN_OFFSET]
    inc ax
    cmp ax, OS_STAGE1_VGA_COLUMN_COUNT
    jae os_stage1_put_vga_character_wrap
    mov [fs:OS_STAGE1_VGA_STATE_CURSOR_COLUMN_OFFSET], ax
    ret

os_stage1_put_vga_character_wrap:
    call os_stage1_advance_vga_line
    ret

os_stage1_advance_vga_line:
    mov word [fs:OS_STAGE1_VGA_STATE_CURSOR_COLUMN_OFFSET], 0
    mov ax, [fs:OS_STAGE1_VGA_STATE_CURSOR_ROW_OFFSET]
    inc ax
    cmp ax, OS_STAGE1_VGA_ROW_COUNT
    jae os_stage1_scroll_vga
    mov [fs:OS_STAGE1_VGA_STATE_CURSOR_ROW_OFFSET], ax
    ret

os_stage1_scroll_vga:
    mov ax, OS_STAGE1_VGA_TEXT_SEGMENT
    mov ds, ax
    mov es, ax
    mov si, OS_STAGE1_VGA_ROW_SIZE_BYTES
    xor di, di
    mov cx, OS_STAGE1_VGA_CELL_COUNT - OS_STAGE1_VGA_COLUMN_COUNT
    rep movsw
    mov di, OS_STAGE1_VGA_LAST_ROW_OFFSET
    mov ax, OS_STAGE1_VGA_BLANK_CELL
    mov cx, OS_STAGE1_VGA_COLUMN_COUNT
    rep stosw
    mov word [fs:OS_STAGE1_VGA_STATE_CURSOR_ROW_OFFSET], OS_STAGE1_VGA_ROW_COUNT - 1
    ret

os_stage1_update_vga_cursor:
    mov ax, [fs:OS_STAGE1_VGA_STATE_CURSOR_ROW_OFFSET]
    mov bx, OS_STAGE1_VGA_COLUMN_COUNT
    mul bx
    add ax, [fs:OS_STAGE1_VGA_STATE_CURSOR_COLUMN_OFFSET]
    mov bx, ax
    mov dx, OS_STAGE1_VGA_CRTC_INDEX_PORT
    mov al, OS_STAGE1_VGA_CRTC_CURSOR_HIGH_INDEX
    out dx, al
    mov dx, OS_STAGE1_VGA_CRTC_DATA_PORT
    mov al, bh
    out dx, al
    mov dx, OS_STAGE1_VGA_CRTC_INDEX_PORT
    mov al, OS_STAGE1_VGA_CRTC_CURSOR_LOW_INDEX
    out dx, al
    mov dx, OS_STAGE1_VGA_CRTC_DATA_PORT
    mov al, bl
    out dx, al
    ret

os_stage1_entered_message:
    db "[OS][STAGE1] ENTERED", 0x0D, 0x0A, 0x00

os_stage1_a20_ready_message:
    db "[OS][STAGE1] A20_READY", 0x0D, 0x0A, 0x00

os_stage1_a20_invalid_message:
    db "[OS][STAGE1] A20_INVALID", 0x0D, 0x0A, 0x00

os_stage1_gdt:
    dq OS_STAGE1_GDT_NULL_DESCRIPTOR
    dq OS_STAGE1_GDT_CODE_DESCRIPTOR
    dq OS_STAGE1_GDT_DATA_DESCRIPTOR
    dq OS_STAGE1_GDT_LONG_CODE_DESCRIPTOR

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

os_stage1_pae_ready_message:
    db "[OS][STAGE1] PAE_READY", 0x0D, 0x0A, 0x00

os_stage1_pae_invalid_message:
    db "[OS][STAGE1] PAE_INVALID", 0x0D, 0x0A, 0x00

os_stage1_long_mode_enable_ready_message:
    db "[OS][STAGE1] LME_READY", 0x0D, 0x0A, 0x00

os_stage1_long_mode_enable_invalid_message:
    db "[OS][STAGE1] LME_INVALID", 0x0D, 0x0A, 0x00

os_stage1_paging_enabled_message:
    db "[OS][STAGE1] PAGING_ENABLED", 0x0D, 0x0A, 0x00

os_stage1_paging_invalid_message:
    db "[OS][STAGE1] PAGING_INVALID", 0x0D, 0x0A, 0x00

os_stage1_long_mode_message:
    db "[OS][STAGE1] LONG_MODE", 0x0D, 0x0A, 0x00

os_stage1_memory_map_ready_message:
    db "[OS][STAGE1] MEMORY_MAP_READY", 0x0D, 0x0A, 0x00

os_stage1_memory_map_invalid_message:
    db "[OS][STAGE1] MEMORY_MAP_INVALID", 0x0D, 0x0A, 0x00

os_stage1_kernel_transfer_message:
    db "[OS][STAGE1] KERNEL_TRANSFER", 0x0D, 0x0A, 0x00

os_stage1_kernel_returned_message:
    db "[OS][STAGE1] KERNEL_RETURNED", 0x0D, 0x0A, 0x00

os_stage1_kernel_ata_timeout_message:
    db "[OS][STAGE1] KERNEL_ATA_TIMEOUT", 0x0D, 0x0A, 0x00

os_stage1_kernel_ata_error_message:
    db "[OS][STAGE1] KERNEL_ATA_ERROR", 0x0D, 0x0A, 0x00

os_stage1_kernel_header_invalid_message:
    db "[OS][STAGE1] KERNEL_HEADER_INVALID", 0x0D, 0x0A, 0x00

os_stage1_kernel_checksum_invalid_message:
    db "[OS][STAGE1] KERNEL_CHECKSUM_INVALID", 0x0D, 0x0A, 0x00

os_stage1_kernel_elf_invalid_message:
    db "[OS][STAGE1] KERNEL_ELF_INVALID", 0x0D, 0x0A, 0x00

%include "memory_map.asm"
%include "kernel_loader.asm"
