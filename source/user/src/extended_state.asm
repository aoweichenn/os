bits 64
default rel

section .text

global OsUserInstallExtendedStatePattern
global OsUserValidateExtendedStatePattern

OS_USER_EXTENDED_STATE_INVALID_PROCESS_ID equ 0
OS_USER_EXTENDED_STATE_PATTERN_INDEX_MASK equ 3
OS_USER_EXTENDED_STATE_MXCSR_SIZE_BYTES equ 4
OS_USER_EXTENDED_STATE_X87_CONTROL_SIZE_BYTES equ 2
OS_USER_EXTENDED_STATE_X87_VALUE_SIZE_BYTES equ 8
OS_USER_EXTENDED_STATE_VALID_RESULT equ 1
OS_USER_EXTENDED_STATE_INVALID_RESULT equ 0
OS_USER_EXTENDED_STATE_VERIFY_STORAGE_SIZE_BYTES equ 32
OS_USER_EXTENDED_STATE_SECOND_QWORD_OFFSET equ 8
OS_USER_EXTENDED_STATE_SECOND_XMM_OFFSET equ 16
OS_USER_EXTENDED_STATE_X87_CONTROL_STORAGE_OFFSET equ 4
OS_USER_EXTENDED_STATE_X87_VALUE_STORAGE_OFFSET equ 8
OS_USER_EXTENDED_STATE_XMM0_LOW_BASE equ 0x1111111111111111
OS_USER_EXTENDED_STATE_XMM0_HIGH_BASE equ 0x2222222222222222
OS_USER_EXTENDED_STATE_XMM15_LOW_BASE equ 0x5151515151515151
OS_USER_EXTENDED_STATE_XMM15_HIGH_BASE equ 0x6262626262626262

; 每个进程获得不同的 XMM、MXCSR、x87 控制字和 x87 ST0 模式。
; 这些状态不会被 -mno-sse2 的 C++ 代码隐式改写，因而可以精确观察调度保存。
OsUserInstallExtendedStatePattern:
    cmp rdi, OS_USER_EXTENDED_STATE_INVALID_PROCESS_ID
    je .invalid

    ; XMM 模式直接由完整 PID 推导，因此不会受早期四进程验收上限约束。
    sub rsp, OS_USER_EXTENDED_STATE_VERIFY_STORAGE_SIZE_BYTES
    mov rax, OS_USER_EXTENDED_STATE_XMM0_LOW_BASE
    xor rax, rdi
    mov [rsp], rax
    mov rax, OS_USER_EXTENDED_STATE_XMM0_HIGH_BASE
    add rax, rdi
    mov [rsp + OS_USER_EXTENDED_STATE_SECOND_QWORD_OFFSET], rax
    mov rax, OS_USER_EXTENDED_STATE_XMM15_LOW_BASE
    xor rax, rdi
    mov [rsp + OS_USER_EXTENDED_STATE_SECOND_XMM_OFFSET], rax
    mov rax, OS_USER_EXTENDED_STATE_XMM15_HIGH_BASE
    add rax, rdi
    mov [rsp + OS_USER_EXTENDED_STATE_SECOND_XMM_OFFSET + \
        OS_USER_EXTENDED_STATE_SECOND_QWORD_OFFSET], rax
    movdqu xmm0, [rsp]
    movdqu xmm15, [rsp + OS_USER_EXTENDED_STATE_SECOND_XMM_OFFSET]
    add rsp, OS_USER_EXTENDED_STATE_VERIFY_STORAGE_SIZE_BYTES

    mov rcx, rdi
    dec rcx
    and rcx, OS_USER_EXTENDED_STATE_PATTERN_INDEX_MASK
    mov rax, rcx
    imul rax, OS_USER_EXTENDED_STATE_MXCSR_SIZE_BYTES
    lea rdx, [rel os_user_extended_state_mxcsr_patterns]
    ldmxcsr [rdx + rax]

    fninit
    mov rax, rcx
    imul rax, OS_USER_EXTENDED_STATE_X87_CONTROL_SIZE_BYTES
    lea rdx, [rel os_user_extended_state_x87_control_patterns]
    fldcw [rdx + rax]
    mov rax, rcx
    imul rax, OS_USER_EXTENDED_STATE_X87_VALUE_SIZE_BYTES
    lea rdx, [rel os_user_extended_state_x87_value_patterns]
    fld qword [rdx + rax]

    mov eax, OS_USER_EXTENDED_STATE_VALID_RESULT
    ret

.invalid:
    mov eax, OS_USER_EXTENDED_STATE_INVALID_RESULT
    ret

; 校验只把寄存器内容复制到调用者栈，不改变被观察的 XMM、MXCSR 或 x87 栈。
OsUserValidateExtendedStatePattern:
    cmp rdi, OS_USER_EXTENDED_STATE_INVALID_PROCESS_ID
    je .invalid

    mov r8, rdi
    sub rsp, OS_USER_EXTENDED_STATE_VERIFY_STORAGE_SIZE_BYTES
    movdqu [rsp], xmm0
    movdqu [rsp + OS_USER_EXTENDED_STATE_SECOND_XMM_OFFSET], xmm15

    mov rax, OS_USER_EXTENDED_STATE_XMM0_LOW_BASE
    xor rax, r8
    cmp [rsp], rax
    jne .mismatch
    mov rax, OS_USER_EXTENDED_STATE_XMM0_HIGH_BASE
    add rax, r8
    cmp [rsp + OS_USER_EXTENDED_STATE_SECOND_QWORD_OFFSET], rax
    jne .mismatch
    mov rax, OS_USER_EXTENDED_STATE_XMM15_LOW_BASE
    xor rax, r8
    cmp [rsp + OS_USER_EXTENDED_STATE_SECOND_XMM_OFFSET], rax
    jne .mismatch
    mov rax, OS_USER_EXTENDED_STATE_XMM15_HIGH_BASE
    add rax, r8
    cmp [rsp + OS_USER_EXTENDED_STATE_SECOND_XMM_OFFSET + \
        OS_USER_EXTENDED_STATE_SECOND_QWORD_OFFSET], rax
    jne .mismatch

    stmxcsr [rsp]
    mov rcx, r8
    dec rcx
    and rcx, OS_USER_EXTENDED_STATE_PATTERN_INDEX_MASK
    mov rax, rcx
    imul rax, OS_USER_EXTENDED_STATE_MXCSR_SIZE_BYTES
    lea rdx, [rel os_user_extended_state_mxcsr_patterns]
    mov edi, [rdx + rax]
    cmp [rsp], edi
    jne .mismatch

    fnstcw [rsp + OS_USER_EXTENDED_STATE_X87_CONTROL_STORAGE_OFFSET]
    mov rax, rcx
    imul rax, OS_USER_EXTENDED_STATE_X87_CONTROL_SIZE_BYTES
    lea rdx, [rel os_user_extended_state_x87_control_patterns]
    mov di, [rdx + rax]
    cmp [rsp + OS_USER_EXTENDED_STATE_X87_CONTROL_STORAGE_OFFSET], di
    jne .mismatch

    fst qword [rsp + OS_USER_EXTENDED_STATE_X87_VALUE_STORAGE_OFFSET]
    mov rax, rcx
    imul rax, OS_USER_EXTENDED_STATE_X87_VALUE_SIZE_BYTES
    lea rdx, [rel os_user_extended_state_x87_value_patterns]
    mov rcx, [rdx + rax]
    cmp [rsp + OS_USER_EXTENDED_STATE_X87_VALUE_STORAGE_OFFSET], rcx
    jne .mismatch

    add rsp, OS_USER_EXTENDED_STATE_VERIFY_STORAGE_SIZE_BYTES
    mov eax, OS_USER_EXTENDED_STATE_VALID_RESULT
    ret

.mismatch:
    add rsp, OS_USER_EXTENDED_STATE_VERIFY_STORAGE_SIZE_BYTES
.invalid:
    mov eax, OS_USER_EXTENDED_STATE_INVALID_RESULT
    ret

section .rodata
align 16

os_user_extended_state_mxcsr_patterns:
    dd 0x00001F80, 0x00003F80, 0x00005F80, 0x00007F80

os_user_extended_state_x87_control_patterns:
    dw 0x037F, 0x077F, 0x0B7F, 0x0F7F

os_user_extended_state_x87_value_patterns:
    dq 0x3FF0000000000000
    dq 0x4000000000000000
    dq 0x4008000000000000
    dq 0x4010000000000000

section .note.GNU-stack noalloc noexec nowrite progbits
