bits 64
default rel

section .text

global osKernelLoadGdtAndTss
global osKernelLoadIdt
global osKernelReadGdtr
global osKernelReadIdtr
global osKernelReadCodeSegment
global osKernelReadStackSegment
global osKernelReadTaskRegister
global osKernelExceptionDispatch

extern osKernelDispatchException

osKernelLoadGdtAndTss:
    lgdt [rdi]
    push rsi
    lea rax, [rel .code_segment_reloaded]
    push rax
    retfq

.code_segment_reloaded:
    mov ax, dx
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov ax, cx
    ltr ax
    ret

osKernelLoadIdt:
    lidt [rdi]
    ret

osKernelReadGdtr:
    sgdt [rdi]
    ret

osKernelReadIdtr:
    sidt [rdi]
    ret

osKernelReadCodeSegment:
    xor eax, eax
    mov ax, cs
    ret

osKernelReadStackSegment:
    xor eax, eax
    mov ax, ss
    ret

osKernelReadTaskRegister:
    xor eax, eax
    str ax
    ret

; 所有入口先形成相同的 vector/error_code 布局，再保存 System V
; AMD64 的通用寄存器。这样 C++ 分发器不需要猜测异常是否由 CPU 压入错误码。
osKernelExceptionDispatch:
    cld
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    and rsp, -16
    sub rsp, 16
    mov [rsp], rdi
    call osKernelDispatchException
    mov rsp, [rsp]

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

%macro OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 1
global os_kernel_exception_vector_%1
os_kernel_exception_vector_%1:
    push qword 0
    push qword %1
    jmp osKernelExceptionDispatch
%endmacro

%macro OS_KERNEL_EXCEPTION_WITH_ERROR_CODE 1
global os_kernel_exception_vector_%1
os_kernel_exception_vector_%1:
    push qword %1
    jmp osKernelExceptionDispatch
%endmacro

OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 0
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 1
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 2
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 3
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 4
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 5
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 6
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 7
OS_KERNEL_EXCEPTION_WITH_ERROR_CODE 8
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 9
OS_KERNEL_EXCEPTION_WITH_ERROR_CODE 10
OS_KERNEL_EXCEPTION_WITH_ERROR_CODE 11
OS_KERNEL_EXCEPTION_WITH_ERROR_CODE 12
OS_KERNEL_EXCEPTION_WITH_ERROR_CODE 13
OS_KERNEL_EXCEPTION_WITH_ERROR_CODE 14
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 15
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 16
OS_KERNEL_EXCEPTION_WITH_ERROR_CODE 17
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 18
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 19
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 20
OS_KERNEL_EXCEPTION_WITH_ERROR_CODE 21
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 22
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 23
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 24
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 25
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 26
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 27
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 28
OS_KERNEL_EXCEPTION_WITH_ERROR_CODE 29
OS_KERNEL_EXCEPTION_WITH_ERROR_CODE 30
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 31

section .rodata
align 16

global osKernelExceptionStubTable
osKernelExceptionStubTable:
%assign OS_KERNEL_EXCEPTION_TABLE_VECTOR 0
%rep 32
    dq os_kernel_exception_vector_%+OS_KERNEL_EXCEPTION_TABLE_VECTOR
%assign OS_KERNEL_EXCEPTION_TABLE_VECTOR OS_KERNEL_EXCEPTION_TABLE_VECTOR + 1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
