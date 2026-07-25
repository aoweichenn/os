bits 64
default rel

section .text

OS_USER_SYSTEM_CALL_VECTOR equ 0x80

global osUserInvokeSystemCall

; C++ 包装器按 System V ABI 传入 number、argument0..argument3。
; 这里把它们变换为项目系统调用 ABI 的 RAX、RDI、RSI、RDX、R10，再由
; DPL3 IDT gate 进入内核。
osUserInvokeSystemCall:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    mov r10, r8
    int OS_USER_SYSTEM_CALL_VECTOR
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
