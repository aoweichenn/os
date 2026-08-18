# ADR 0048：有界 Shell 控制序列与 append 重定向

状态：已接受
日期：2026-08-18

## 背景

原 Shell 一次只能执行一条管线，输出重定向只有截断模式。直接为每条控制命令
复制一个完整 `ShellExecutionPlan` 会让用户栈随命令数线性增长；只在打开文件
时把 offset 放到文件尾，又不能保证后续独立描述符追加时不覆盖已有内容。

## 决策

1. 顶层序列只保存最多 8 个源文本 span 和 `Always/OnSuccess/OnFailure` 条件；
   子命令仍复用现有最多 16 stage 的执行计划。
2. 整行先逐 span 预解析，全部成功后才执行；执行时重新解析当前 span，避免
   同时保存多个大型计划，也避免后段语法错误留下前段副作用。
3. 512 字节命令存储使 argument offset/length 可用显式 `uint16_t`；静态断言
   锁定存储不超过 `UINT16_MAX`，计划继续小于 4 KiB。
4. ABI 新增 append open flag。Kernel 把它保存为 FileDescription status flag；
   每次普通文件 write 在操作锁内 stat 当前大小并更新共享 OpenFile offset。
5. 当前单 BSP、内核不可抢占契约保证独立 FileDescription 的 stat/write 之间
   不发生调度；未来 SMP 必须把该原子边界下沉到 vnode 或文件系统后端锁。
6. `2>`/`2>>` 直接把目标 fd 设为标准错误 2；`/bin/err` 用于整机组合验证。

## 后果

- `;`、`&&`、`||` 与引号、反斜杠、单管线 `|` 具有明确优先边界。
- append 仅允许 writable regular file；对目录和字符设备请求会失败。
- rootfs 工具路径由 32 增至 33；v1.18 发布记录继续保留当时 32 个工具的历史。
- 本 ADR 不引入环境展开、glob、raw terminal 或历史记录，它们继续属于 v2.2。
