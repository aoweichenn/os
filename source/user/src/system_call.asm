bits 64
default rel

OS_USER_SYSTEM_CALL_VECTOR equ 0x80

global OsUserInvokeSystemCall
global OsUserInvokeLegacySystemCall
global OsUserInvokeSystemCallWithDirectionFlag
global OsUserSignalReturnRestorer

; C++ 包装器按 System V ABI 传入 number、argument0..argument3。
; 这里把它们变换为项目系统调用 ABI 的 RAX、RDI、RSI、RDX、R10。
; 默认入口使用 SYSCALL；兼容入口只服务双入口等价性与长期 ABI 回归。
section .text.os_user_system_call_required

OsUserInvokeSystemCall:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    mov r10, r8
    syscall
    ret

section .text

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

OS_USER_SIGNAL_RETURN_SYSTEM_CALL_NUMBER equ 61

; handler 通过 RET 到达这里时，RSP 正好指向内核构造的 SignalFrame。
; SignalReturn 成功后直接恢复被中断现场，因此这条路径绝不会正常返回。
OsUserSignalReturnRestorer:
    mov rdi, rsp
    mov rax, OS_USER_SIGNAL_RETURN_SYSTEM_CALL_NUMBER
    syscall
    ud2

section .note.GNU-stack noalloc noexec nowrite progbits
