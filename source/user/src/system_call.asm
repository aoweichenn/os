bits 64
default rel

section .text

OS_USER_SYSTEM_CALL_VECTOR equ 0x80

global OsUserInvokeSystemCall
global OsUserInvokeLegacySystemCall
global OsUserInvokeSystemCallWithDirectionFlag

; C++ 包装器按 System V ABI 传入 number、argument0..argument3。
; 这里把它们变换为项目系统调用 ABI 的 RAX、RDI、RSI、RDX、R10。
; 默认入口使用 SYSCALL；兼容入口只服务双入口等价性与长期 ABI 回归。
OsUserInvokeSystemCall:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    mov r10, r8
    syscall
    ret

OsUserInvokeLegacySystemCall:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    mov r10, r8
    int OS_USER_SYSTEM_CALL_VECTOR
    ret

; DF 是合法用户状态但不属于本项目的 SYSRET 快速集合。入口 CLD 后分发，
; 返回选择器必须改走 IRETQ 恢复 DF；包装器随后立即清除，避免污染 C++ ABI。
OsUserInvokeSystemCallWithDirectionFlag:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    mov r10, r8
    std
    syscall
    cld
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
