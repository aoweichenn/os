# User 与 ABI 模块

## 模块职责

`source/abi` 保存内核与用户态共同依赖的稳定数值契约；它不能依赖 Kernel 或
User。`source/user` 保存 Ring 3 可执行程序和最小系统调用包装；它是
freestanding 目标，不拥有标准库、堆、线程局部存储或宿主系统调用。

```text
abi/system_call.hpp
       ↑           ↑
user 包装与程序    kernel 分发与校验
       ↓           ↓
 system_call.asm → IDT 0x80 → system_calls.cpp
```

## 系统调用 ABI

| 寄存器 | 含义 |
| --- | --- |
| `RAX` | 调用编号；返回后保存有符号 64 位结果 |
| `RDI` | 参数 0 |
| `RSI` | 参数 1 |
| `INT 0x80` | 进入内核的指令与门向量 |

| 编号 | 接口 | 参数 | 结果 |
| --- | --- | --- | --- |
| 1 | `WriteLog` | `RDI=地址`，`RSI=字节数` | 已写字节数或负错误码 |
| 2 | `ExitProcess` | `RDI=有符号退出码` | 不返回用户态 |
| 3 | `GetProcessId` | 无 | 当前进程的 64 位 PID |

错误值为 `-1` 非法用户内存、`-2` 未知编号、`-3` 写入过长、`-4` 串口失败。
这些值使用显式 `int64_t`；ABI 不使用与平台宽度相关的 `long`、`size_t` 或
枚举底层默认类型。

## 代码走读

1. `programs/smoke.cpp` 只调用公开包装，不直接依赖内核符号。
2. `src/system_call.cpp` 把类型化枚举转换为稳定编号。
3. `src/system_call.asm` 按 ABI 把 C++ 参数移到系统调用寄存器并执行
   `INT 0x80`。
4. CPU 根据 IDT gate 的 DPL 允许 CPL3 触发该向量，根据 TSS.RSP0 离开
   用户栈，再压入特权帧。
5. 内核汇编公共入口保存寄存器，C++ 分发器验证完整来源和地址。
6. 普通调用通过 `IRETQ` 回 Ring 3；`ExitProcess` 和用户异常终止当前 PCB，
   返回调度器选择的下一进程帧。

## v0.9 调度验收程序

`programs/scheduler_worker.cpp` 同一份 ELF 被创建三次，三个实例都链接到
`0x40000000`，并在同一 BSS 虚拟地址维护 `workerCounter`。每个 worker
先通过 `GetProcessId` 选择 PID2/PID3/PID4 的固定进度标记，再执行三轮
有界计算。若地址空间错误共享，后运行实例会看到其他进程写过的计数并以非零
状态退出；三个实例都输出 `ADDRESS_SPACE_ISOLATED` 才能证明独立物理叶页。

worker 不自行访问 PIT、CR3、TSS 或 PIC。它只制造足够长的纯计算区间，让真实
IRQ0 在 CPL3 打断执行；抢占证据来自内核的 run tick 与 dispatch 统计。每个
worker 恰好执行六次系统调用：PID 查询、三次进度日志、一次隔离日志和退出。
QEMU 协议检查每条固定日志的精确出现次数，避免某个进程重复输出掩盖另一个
进程缺失。

## 依赖与命名

- 公开头位于 `source/user/include/os/user/` 和
  `source/abi/include/os/abi/`，实现位于各自 `src/`。
- 普通 C++ 函数使用大驼峰，例如 `InvokeSystemCall()`、`WriteLog()` 和
  `ExitProcess()`。
- `osUserEntry` 与 `osUserInvokeSystemCall` 是 C/汇编 ABI 符号，因链接契约
  保留既定前缀，不作为普通函数命名的例外扩散到业务代码。
- 所有协议数字和字符串均使用“项目 + 模块 + 功能”的全大写命名常量。
