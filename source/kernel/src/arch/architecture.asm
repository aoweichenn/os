bits 64
default rel

section .text

global OsKernelLoadGdtAndTss
global OsKernelLoadIdt
global OsKernelReadGdtr
global OsKernelReadIdtr
global OsKernelReadCodeSegment
global OsKernelReadStackSegment
global OsKernelReadTaskRegister
global OsKernelExceptionDispatch
global OsKernelHardwareInterruptDispatch
global OsKernelSystemCallEntry
global OsKernelSystemCallDispatch
global OsKernelNativeSystemCallEntry
global OsKernelEnterScheduledProcess
global OsKernelReturnFromUserMode
global OsKernelInitializeFxState
global OsKernelSaveFxState
global OsKernelRestoreFxState

extern OsKernelDispatchException
extern OsKernelDispatchHardwareInterrupt
extern OsKernelDispatchSystemCall
extern OsKernelPrepareUserReturn
extern OsKernelSelectUserReturn

OS_KERNEL_ARCHITECTURE_KERNEL_CODE_SELECTOR equ 0x08
OS_KERNEL_ARCHITECTURE_KERNEL_DATA_SELECTOR equ 0x10
OS_KERNEL_ARCHITECTURE_USER_DATA_SELECTOR equ 0x1B
OS_KERNEL_ARCHITECTURE_USER_CODE_SELECTOR equ 0x23
OS_KERNEL_ARCHITECTURE_LEGACY_SYSTEM_CALL_VECTOR equ 0x80
OS_KERNEL_ARCHITECTURE_NATIVE_SYSTEM_CALL_VECTOR equ 0x81
OS_KERNEL_ARCHITECTURE_CPU_LOCAL_ENTRY_STACK_OFFSET equ 16
OS_KERNEL_ARCHITECTURE_CPU_LOCAL_USER_STACK_OFFSET equ 24
OS_KERNEL_ARCHITECTURE_GS_NOT_SWAPPED equ 0
OS_KERNEL_ARCHITECTURE_GS_SWAPPED equ 1
OS_KERNEL_ARCHITECTURE_INTERRUPT_RETURN equ 1
OS_KERNEL_ARCHITECTURE_SYSTEM_RETURN equ 2

OsKernelLoadGdtAndTss:
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

OsKernelLoadIdt:
    lidt [rdi]
    ret

OsKernelReadGdtr:
    sgdt [rdi]
    ret

OsKernelReadIdtr:
    sidt [rdi]
    ret

OsKernelReadCodeSegment:
    xor eax, eax
    mov ax, cs
    ret

OsKernelReadStackSegment:
    xor eax, eax
    mov ax, ss
    ret

OsKernelReadTaskRegister:
    xor eax, eax
    str ax
    ret

; 所有入口先形成相同的 vector/error_code 布局，再保存 System V
; AMD64 的通用寄存器。这样 C++ 分发器不需要猜测异常是否由 CPU 压入错误码。
OsKernelExceptionDispatch:
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
    call OsKernelDispatchException
    mov rsp, rax

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
    jmp OsKernelExceptionDispatch
%endmacro

%macro OS_KERNEL_EXCEPTION_WITH_ERROR_CODE 1
global os_kernel_exception_vector_%1
os_kernel_exception_vector_%1:
    push qword %1
    jmp OsKernelExceptionDispatch
%endmacro

OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 0
OS_KERNEL_EXCEPTION_WITHOUT_ERROR_CODE 1
global os_kernel_exception_vector_2
os_kernel_exception_vector_2:
    cli
    mov qword [rel os_kernel_nmi_observed], 1
.halt:
    hlt
    jmp .halt
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

; 8259A 已重映射到向量 32..47。硬件 IRQ 不压入错误码，因此所有入口都补零，
; 并使用与异常相同的寄存器帧布局，但交给独立的硬件中断分发器。
OsKernelHardwareInterruptDispatch:
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
    call OsKernelDispatchHardwareInterrupt
    mov rsp, rax

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

%macro OS_KERNEL_HARDWARE_INTERRUPT 1
global os_kernel_hardware_interrupt_vector_%1
os_kernel_hardware_interrupt_vector_%1:
    push qword 0
    push qword %1
    jmp OsKernelHardwareInterruptDispatch
%endmacro

OS_KERNEL_HARDWARE_INTERRUPT 32
OS_KERNEL_HARDWARE_INTERRUPT 33
OS_KERNEL_HARDWARE_INTERRUPT 34
OS_KERNEL_HARDWARE_INTERRUPT 35
OS_KERNEL_HARDWARE_INTERRUPT 36
OS_KERNEL_HARDWARE_INTERRUPT 37
OS_KERNEL_HARDWARE_INTERRUPT 38
OS_KERNEL_HARDWARE_INTERRUPT 39
OS_KERNEL_HARDWARE_INTERRUPT 40
OS_KERNEL_HARDWARE_INTERRUPT 41
OS_KERNEL_HARDWARE_INTERRUPT 42
OS_KERNEL_HARDWARE_INTERRUPT 43
OS_KERNEL_HARDWARE_INTERRUPT 44
OS_KERNEL_HARDWARE_INTERRUPT 45
OS_KERNEL_HARDWARE_INTERRUPT 46
OS_KERNEL_HARDWARE_INTERRUPT 47

; INT 0x80 由硬件切换到 TSS.RSP0；SYSCALL 则先 SWAPGS，再从 CpuLocal
; 取得同一 Thread 的可信栈。两条路径最终都构造完全相同的 UserContext。
OsKernelSystemCallEntry:
    push qword 0
    push qword OS_KERNEL_ARCHITECTURE_LEGACY_SYSTEM_CALL_VECTOR
    jmp OsKernelSystemCallDispatch

OsKernelSystemCallDispatch:
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

    mov r12, OS_KERNEL_ARCHITECTURE_GS_NOT_SWAPPED
    jmp os_kernel_system_call_dispatch_context

OsKernelNativeSystemCallEntry:
    swapgs
    mov [gs:OS_KERNEL_ARCHITECTURE_CPU_LOCAL_USER_STACK_OFFSET], rsp
    mov rsp, [gs:OS_KERNEL_ARCHITECTURE_CPU_LOCAL_ENTRY_STACK_OFFSET]

    push qword OS_KERNEL_ARCHITECTURE_USER_DATA_SELECTOR
    push qword [gs:OS_KERNEL_ARCHITECTURE_CPU_LOCAL_USER_STACK_OFFSET]
    push r11
    push qword OS_KERNEL_ARCHITECTURE_USER_CODE_SELECTOR
    push rcx
    push qword 0
    push qword OS_KERNEL_ARCHITECTURE_NATIVE_SYSTEM_CALL_VECTOR

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

    mov r12, OS_KERNEL_ARCHITECTURE_GS_SWAPPED

os_kernel_system_call_dispatch_context:
    mov rdi, rsp
    mov ax, OS_KERNEL_ARCHITECTURE_KERNEL_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    and rsp, -16
    sti
    call OsKernelDispatchSystemCall
    cli

    mov rbx, rax
    mov rdi, rbx
    call OsKernelPrepareUserReturn
    mov rbx, rax
    mov rdi, rbx
    call OsKernelSelectUserReturn
    mov r13, rax

    mov ax, OS_KERNEL_ARCHITECTURE_USER_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    cmp r12, OS_KERNEL_ARCHITECTURE_GS_SWAPPED
    jne os_kernel_system_call_gs_base_ready
    swapgs

os_kernel_system_call_gs_base_ready:
    cmp r13, OS_KERNEL_ARCHITECTURE_SYSTEM_RETURN
    je os_kernel_system_call_system_return
    cmp r13, OS_KERNEL_ARCHITECTURE_INTERRUPT_RETURN
    jne os_kernel_system_call_invalid_return

    mov rsp, rbx

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

os_kernel_system_call_system_return:
    mov rsp, rbx

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
    mov rcx, [rsp]
    mov r11, [rsp + 16]
    mov rsp, [rsp + 24]
    o64 sysret

os_kernel_system_call_invalid_return:
    ud2

; 调度启动前保存内核调用链，再直接恢复首个 Thread 预构造的 176 字节用户现场。
; 最后一个 Thread 结束后，退出路径恢复这里的栈并返回 ExecuteProcesses。
OsKernelEnterScheduledProcess:
    pushfq
    cli
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [rel os_kernel_saved_user_mode_kernel_stack], rsp
    mov rsp, rdi

    mov ax, OS_KERNEL_ARCHITECTURE_USER_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov gs, ax

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

OsKernelReturnFromUserMode:
    cli
    test rdi, rdi
    jz .gs_base_ready
    swapgs

.gs_base_ready:
    mov rsp, [rel os_kernel_saved_user_mode_kernel_stack]
    mov qword [rel os_kernel_saved_user_mode_kernel_stack], 0

    mov ax, OS_KERNEL_ARCHITECTURE_KERNEL_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov gs, ax
    mov ss, ax

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    popfq
    ret

; FXSAVE64/FXRSTOR64 的内存操作数必须按 16 字节对齐。C++ 的 FxSaveArea
; 固定这一布局；初始模板明确复位 x87 并采用体系结构默认 MXCSR。
OsKernelInitializeFxState:
    fninit
    ldmxcsr [rel os_kernel_default_mxcsr]
    fxsave64 [rdi]
    ret

OsKernelSaveFxState:
    fxsave64 [rdi]
    ret

OsKernelRestoreFxState:
    fxrstor64 [rdi]
    ret

section .rodata
align 16

os_kernel_default_mxcsr:
    dd 0x00001F80

global os_kernel_exception_stub_table
os_kernel_exception_stub_table:
%assign OS_KERNEL_EXCEPTION_TABLE_VECTOR 0
%rep 32
    dq os_kernel_exception_vector_%+OS_KERNEL_EXCEPTION_TABLE_VECTOR
%assign OS_KERNEL_EXCEPTION_TABLE_VECTOR OS_KERNEL_EXCEPTION_TABLE_VECTOR + 1
%endrep

global os_kernel_hardware_interrupt_stub_table
os_kernel_hardware_interrupt_stub_table:
%assign OS_KERNEL_HARDWARE_INTERRUPT_TABLE_VECTOR 32
%rep 16
    dq os_kernel_hardware_interrupt_vector_%+OS_KERNEL_HARDWARE_INTERRUPT_TABLE_VECTOR
%assign OS_KERNEL_HARDWARE_INTERRUPT_TABLE_VECTOR \
    OS_KERNEL_HARDWARE_INTERRUPT_TABLE_VECTOR + 1
%endrep

section .bss
align 8

global os_kernel_nmi_observed
os_kernel_nmi_observed:
    resq 1

os_kernel_saved_user_mode_kernel_stack:
    resq 1

section .note.GNU-stack noalloc noexec nowrite progbits
