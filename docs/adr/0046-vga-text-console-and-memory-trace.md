# ADR 0046：VGA 前台、内存系统日志与宿主导出

状态：已接受
日期：2026-08-18

## 背景

历史系统从 Firmware 到 Ring 3 都把 COM1 当作输出设备，QEMU runner 通过
`-serial stdio` 捕获里程碑。v2.1 首先把输出迁到 VGA；随后交互需求进一步
要求详细诊断日志与用户终端分流，避免启动统计和运行期事件冲掉 Shell 内容。
无图形 CI 仍需要可重复、可排序且不会因 25 行滚屏丢失的系统证据。

项目使用自研 ROM，不能假定第三方 VGA BIOS 已设置文本模式或装载字形。

## 决策

1. QEMU 使用显式 `-device VGA`，串口设为 `none`。
2. ROM 自行设置 80×25 彩色文本模式、EGA 兼容 16 色 DAC 调色板，把 Public
   Domain 的 8×8 Basic Latin 字形扩展成 8×16 后装入 VGA 字符平面。字节固定来源为
   [`dhepper/font8x8` 提交 `8e279d2`](https://github.com/dhepper/font8x8/tree/8e279d2d864e79128e96188a6b9526cfa3fbfef9)。
3. `0xB8000` 保存当前 25 行；换行到末行时由项目代码上移并清空末行。
4. `0x20000..0x9FFFF` 保存固定布局的共享光标、输出模式和只追加日志。
   Firmware 初始化一次，Stage 1 与 Kernel 只接管，不重新清空。
5. 共享头版本 3 在偏移 `0x15` 增加 `output_mode`：0 表示启动输出，1 表示
   用户终端。布局仍保持 32 字节，日志仍从偏移 `0x20` 开始。
6. 启动模式下诊断字节同时进入内存日志和 VGA，早期失败因此可见。Kernel
   准备进入用户环境时清屏并提交终端模式；此后普通诊断只写内存，TTY 的
   stdout/stderr 才写屏幕。panic 始终尽力同时写两处，即使日志已经溢出也
   不能阻止屏幕报告。
7. QEMU runner 通过 QMP 读取物理日志区；目标系统不知道 QMP 的存在。
   自动测试输出带宿主观察时间，交互式 `qemu-display` 默认持续写入
   `build/developer/qemu-display.log`。
8. 日志区写满必须置位 overflow；普通诊断和用户日志调用失败，不能把截断
   记录判为成功。紧急屏幕输出不受该标志阻止。
9. 每条 QEMU 成功和失败路径都执行 `screendump`，P6 PPM 必须包含至少
   512 个非黑像素；内存日志不能替代可见扫描输出的证据。

## 原因

直接解析截图会受到字形、缩放和滚屏影响。只追加内存日志保存完整启动历史，
VGA 则只承担启动反馈、交互文本和紧急错误；自动化同时检查两条路径。该内存
协议属于项目启动契约，不要求目标机存在 QEMU 调试设备。

`0x20000..0x9FFFF` 位于既有低 1 MiB 平台保留范围，不会进入普通页帧分配。
共享头版本为 3；512 KiB 区域扣除 32 字节头后全部用于追加记录，覆盖完整
functional 与 capacity 验收输出。模式提交只改变后续诊断的可见性，不删除
已经追加的字节。
Stage 1 生产负载固定在 `0x8000..0xFFFF`，Kernel ELF 暂存区位于 54 MiB，
因此三者不重叠。

## 后果

- VGA 寄存器表、DAC 调色板、字体来源、共享状态布局、输出路由和 QEMU
  捕获器成为启动契约的一部分。
- Firmware 和 Stage 1 必须分别提供实模式、保护模式和长模式显存写入路径。
- Kernel 页表必须保留 VGA legacy aperture 的 supervisor identity mapping，并
  对 `0xA0000..0xBFFFF` 使用 cache-disable 权限。
- 内存日志不是持久文件系统；复位后丢失。需要保留时由宿主工具在会话期间
  导出，v2.3 以前不让 panic 依赖 VFS、页缓存或磁盘锁。
- 历史 v0.1/v0.2 release note 继续记录当时的 COM1 实现，不回写历史事实。
