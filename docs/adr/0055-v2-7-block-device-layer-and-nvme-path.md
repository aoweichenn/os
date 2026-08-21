# ADR 0055：v2.7 通用块设备层与自研 NVMe 路径

## 状态

已接受，通用块层、NVMe admin/PRP/MSI-X/reset、双 namespace rootfs/swap 与
ATA 回退已实现；公开发布未开始。

## 问题

v2.6 的 `BlockRequestQueue` 位于设备模块，却把 512 字节扇区、LBA28 和单个
Issued 请求写进参数校验与状态机；块设备基类又定义在文件系统缓存头文件中。
这些边界足以表达 ATA PIO，却不能描述 NVMe namespace 的逻辑块几何、多块传输
和多个未完成命令。若直接在 NVMe 驱动中绕过它们，rootfs、swap 和页缓存会开始
识别具体控制器，后续每增加一种设备都需要修改上层。

v2.6 已形成但没有公开发布。后续工程以 v2.7 继续，不回写 v2.6 的冻结身份，
也不以增加驱动数量为目标。

## 决策

### 通用块设备层

设备模块公开 `BlockDevice`，只表达逻辑块读、写和稳定化。文件系统原有
`FileSystemBlockDevice` 名称暂时保留为类型别名，使 rootfs、journal、swap 和
宿主内存设备不需要在同一提交中整体重命名；新代码不得再在文件系统模块定义
块设备协议。

`BlockDevice` 使用静态 Read/Write/Flush 函数表做类型擦除，具体驱动继承
`BlockDeviceAdapter<DriverType>`。它不使用 C++ virtual、RTTI 或纯虚调用，避免
freestanding Kernel 引入 `__cxa_pure_virtual`；`constinit` ATA 对象只复制编译期
函数表，不产生全局构造运行时。

`BlockDeviceGeometry` 由驱动提供：

- 逻辑块字节数与逻辑块总数；
- 单请求最大逻辑块数；
- 最大未完成请求数；
- 写入与 Flush 能力。

请求同时保存起始 LBA、逻辑块数和缓冲字节数。队列在发布请求前检查整块倍数、
乘法溢出、末 LBA、传输上限和设备能力；失败不得占用 identifier 或槽位。

### 多请求生命周期

FIFO 只规定 Queued 的签发顺序，完成允许乱序。Issued 数不得超过设备声明深度。
完成、设备错误、超时和取消继续竞争唯一终态；超时扫描每次选择已经到期且
deadline 最早的请求，同 deadline 再按 identifier 决定，便于固定种子模型复现。

ATA PIO 声明 512 字节逻辑块、LBA28 容量、单块传输、深度 1、可写且支持 Flush，
因此现有 IRQ14 行为不改变。通用队列不再包含 ATA 常量。

### 自研 NVMe 范围

QEMU 只模拟 PCI/NVMe 硬件。项目自行实现 PCI configuration space 枚举、BAR
解析、MMIO、DMA 可见队列、doorbell、命令 identifier、phase tag、超时与控制器
复位，不使用宿主 NVMe passthrough、virtio、固件驱动或外部启动器。

首个 NVMe 目标对齐 QEMU 实现的 NVMe 1.4 必选子集：一个控制器、一个 namespace、
一个 admin queue pair 和一个 I/O queue pair。先用有界轮询完成 Identify、创建
I/O 队列与基本 Read/Write/Flush，再接 MSI-X；多 namespace、多队列、ZNS、
SR-IOV、multipath 和热插拔不进入首个增量。

生产迁移在同一控制器增加第二个 namespace：NSID 1 承载 rootfs，NSID 2 承载
独立 swap 格式。两个 `NvmeNamespaceDevice` 只暴露 `BlockDevice`，上层不读取
NSID。单 namespace 仍保留驱动自检用途，不作为生产存储选择。

每个 I/O 槽拥有 16 个数据页和一页 PRP list。最大传输取 64 KiB、MDTS 和
namespace 容量三者的最小值，最大 outstanding 为 4；`BlockDevice` 缓冲由槽位
分页复制，不要求调用者物理连续。CQ completion 按 CID 找槽，不假定提交顺序。

MSI-X entry 0 使用 BSP LAPIC 和 IDT `0x50`。function/vector mask 一直保持到 CQ1
创建完成，避免 admin completion 在 I/O runtime 尚未就绪时触发。错误 completion
或绝对 deadline 超时都会冻结提交；reset 依次 mask MSI-X、清 CC.EN、等待 RDY=0、
清队列、重新建立 admin/I/O queue，成功后才重新 unmask。

规范依据：

- [NVM Express Base Specification](https://nvmexpress.org/specification/nvm-express-base-specification/)
- [QEMU NVMe emulation](https://www.qemu.org/docs/master/system/devices/nvme.html)

### 启动与迁移

ROM、Stage 1 和 early Kernel 继续从 ATA PIO 启动。NVMe 驱动在 Kernel 建立页表、
物理页分配和最小 PCI 支持后初始化。rootfs 与 swap 已在容量、Flush、故障、
持久化和资源回收证据达到 ATA 基线后切换到 Namespace 1/2。设备缺失或初始化
失败且资源安全回收时自动选择 ATA；资源泄漏不得回退并继续启动。

## 失败语义

- 非法几何：队列初始化失败，不发布部分状态；
- 非整块、跨 namespace 末端、超出 MDTS/队列深度或只读写入：提交失败；
- 重复完成或迟到完成：保留第一个终态并记录重复解析；
- 控制器 fatal status、超时或队列损坏：停止提交，冻结可诊断状态，执行有界复位；
- Flush 失败：不得向 journal、swap 或用户同步调用报告稳定成功。

## 测试

- 单元测试覆盖非法几何、只读/无 Flush、非整块、末 LBA、多块上限和容量复用；
- 集成测试覆盖深度内并发签发、乱序完成、IRQ/超时单赢家和补充签发；
- 固定种子随机模型执行十万步多深度提交、签发、完成、超时、取消和 Reap；
- ATA 三档 QEMU 回归必须保持原 marker、持久化和失败矩阵；
- PCI 纯模型覆盖 BDF、class、32/64 位 BAR aperture 与资源窗口；NVMe 纯模型覆盖
  CAP/CC/AQA、Identify command/data、CQE status/phase 与十万步 queue wrap；
- QEMU 必须挂独立 raw NVMe namespace，真实完成 BAR 分配、MMIO、enable、两条
  Identify、disable 和资源回收，不能用构造的 Identify 数据代替控制器 DMA。
- 数据路径必须在真实 QEMU namespace 完成高 LBA 多块 Write、Flush、Read 和逐字节
  回验；纯模型必须覆盖 Set Features、Create CQ/SQ 与 64 位 SLBA/零基 NLB。
- QEMU 正常路径必须观察 MSI-X 和四 outstanding；blkdebug EIO 与 doorbell timeout
  必须各触发一次 reset，禁止在失败后报告 I/O 成功。
- 双 namespace 必须在 64 MiB、256 MiB、4 GiB 完成同一 rootfs/swap 工作负载；
  可写副本必须两次重启恢复，并在第三次超级块损坏后拒绝挂载。

## 后果

上层从控制器类型中解耦，ATA 与 NVMe 可以共享请求身份、超时、统计和失败语义。
代价是通用队列需要维护多 Issued 状态，旧的文件系统设备名称在过渡期仍作为别名
存在；别名只能用于兼容，不能成为第二套接口。
