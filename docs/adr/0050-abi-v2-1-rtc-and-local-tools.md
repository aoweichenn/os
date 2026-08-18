# ADR 0050：ABI v2.1 兼容扩展、CMOS UTC 与本地工具集

状态：已接受
日期：2026-08-18

## 背景

ABI v2.0.0 冻结了 1..69，但 v2.2 的受控输入模式和真实 date 无法由既有调用
表达。把 PIT 单调纳秒当日期会产生错误语义；把宿主时间注入用户程序又违反
QEMU 只模拟硬件的边界。新增文本工具还必须避免 libc、动态分配和无界递归。

## 决策

1. ABI 主版本保持 2，minor 升为 1；只在尾部增加 SetTerminalInputMode=70 与
   GetRealtime=71，既有编号、结构和错误区间不变。
2. GetRealtime 读取 QEMU PC CMOS `0x70/0x71`：有限等待 UIP、读取两份相同
   快照、按 status B 解码 BCD/binary 与 12/24 小时，再验证 Gregorian 日历并
   返回 UTC 字段和 Unix 秒。
3. RTC 是可回拨墙钟，绝不进入 Sleep、futex 或 DeadlineQueue；这些接口继续只
   使用 PIT monotonic clock。
4. rootfs 安装 43 个不同 inode 的工具路径。新增 grep/find/sort/tail/df/du/
   hexdump/clear/date/env 与 err 继续复用 multi-call ELF，但每次 exec/fd 语义独立。
5. grep/sort/tail 的行上限分别显式为 256 字节，sort 最多 64 行；find/du/df 用
   128×513 字节迭代路径栈，不用递归。df 在 rootfs v3 阶段报告固定 256 MiB
   区域与可达文件 allocated bytes；v2.3 改盘面几何时必须同步替换统计来源。

## 后果

- date 输出真实 `UTC YYYY-MM-DDTHH:MM:SSZ`，RTC 失败明确返回设备错误。
- ABI v2.0 程序保持二进制兼容；读取 `/proc/version` 的新程序可观察 v2.1.0。
- 工具对超长行、过多路径或遍历容量耗尽明确失败，不会申请无界宿主/来宾内存。
- 当前没有时区数据库、闰秒表、RTC 设置、locale/regex 或完整 POSIX 工具选项。
