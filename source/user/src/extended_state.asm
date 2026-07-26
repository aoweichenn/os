bits 64
default rel

section .text

global OsUserInstallExtendedStatePattern
global OsUserValidateExtendedStatePattern

OS_USER_EXTENDED_STATE_FIRST_PROCESS_ID equ 1
OS_USER_EXTENDED_STATE_LAST_PROCESS_ID equ 4
OS_USER_EXTENDED_STATE_XMM_PATTERN_SIZE_BYTES equ 16
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

; 每个进程获得不同的 XMM、MXCSR、x87 控制字和 x87 ST0 模式。
; 这些状态不会被 -mno-sse2 的 C++ 代码隐式改写，因而可以精确观察调度保存。
OsUserInstallExtendedStatePattern:
    mov rax, rdi
    sub rax, OS_USER_EXTENDED_STATE_FIRST_PROCESS_ID
    cmp rax, OS_USER_EXTENDED_STATE_LAST_PROCESS_ID - \
             OS_USER_EXTENDED_STATE_FIRST_PROCESS_ID
    ja .invalid

    mov rcx, rax
    imul rax, OS_USER_EXTENDED_STATE_XMM_PATTERN_SIZE_BYTES
    lea rdx, [rel os_user_extended_state_xmm0_patterns]
    movdqu xmm0, [rdx + rax]
    lea rdx, [rel os_user_extended_state_xmm15_patterns]
    movdqu xmm15, [rdx + rax]

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
    mov rax, rdi
    sub rax, OS_USER_EXTENDED_STATE_FIRST_PROCESS_ID
    cmp rax, OS_USER_EXTENDED_STATE_LAST_PROCESS_ID - \
             OS_USER_EXTENDED_STATE_FIRST_PROCESS_ID
    ja .invalid

    mov r8, rax
    sub rsp, OS_USER_EXTENDED_STATE_VERIFY_STORAGE_SIZE_BYTES
    movdqu [rsp], xmm0
    movdqu [rsp + OS_USER_EXTENDED_STATE_SECOND_XMM_OFFSET], xmm15

    imul rax, OS_USER_EXTENDED_STATE_XMM_PATTERN_SIZE_BYTES
    lea rdx, [rel os_user_extended_state_xmm0_patterns]
    mov rcx, [rdx + rax]
    cmp [rsp], rcx
    jne .mismatch
    mov rcx, [rdx + rax + OS_USER_EXTENDED_STATE_SECOND_QWORD_OFFSET]
    cmp [rsp + OS_USER_EXTENDED_STATE_SECOND_QWORD_OFFSET], rcx
    jne .mismatch
    lea rdx, [rel os_user_extended_state_xmm15_patterns]
    mov rcx, [rdx + rax]
    cmp [rsp + OS_USER_EXTENDED_STATE_SECOND_XMM_OFFSET], rcx
    jne .mismatch
    mov rcx, [rdx + rax + OS_USER_EXTENDED_STATE_SECOND_QWORD_OFFSET]
    cmp [rsp + OS_USER_EXTENDED_STATE_SECOND_XMM_OFFSET + \
        OS_USER_EXTENDED_STATE_SECOND_QWORD_OFFSET], rcx
    jne .mismatch

    stmxcsr [rsp]
    mov rax, r8
    imul rax, OS_USER_EXTENDED_STATE_MXCSR_SIZE_BYTES
    lea rdx, [rel os_user_extended_state_mxcsr_patterns]
    mov ecx, [rdx + rax]
    cmp [rsp], ecx
    jne .mismatch

    fnstcw [rsp + OS_USER_EXTENDED_STATE_X87_CONTROL_STORAGE_OFFSET]
    mov rax, r8
    imul rax, OS_USER_EXTENDED_STATE_X87_CONTROL_SIZE_BYTES
    lea rdx, [rel os_user_extended_state_x87_control_patterns]
    mov cx, [rdx + rax]
    cmp [rsp + OS_USER_EXTENDED_STATE_X87_CONTROL_STORAGE_OFFSET], cx
    jne .mismatch

    fst qword [rsp + OS_USER_EXTENDED_STATE_X87_VALUE_STORAGE_OFFSET]
    mov rax, r8
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

os_user_extended_state_xmm0_patterns:
    dq 0x1111111111111111, 0x1212121212121212
    dq 0x2121212121212121, 0x2222222222222222
    dq 0x3131313131313131, 0x3232323232323232
    dq 0x4141414141414141, 0x4242424242424242

os_user_extended_state_xmm15_patterns:
    dq 0x5151515151515151, 0x5252525252525252
    dq 0x6161616161616161, 0x6262626262626262
    dq 0x7171717171717171, 0x7272727272727272
    dq 0x8181818181818181, 0x8282828282828282

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
