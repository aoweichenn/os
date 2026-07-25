# ADR 0009：fw_cfg 物理内存图与内核自建页表

- 状态：接受；其中固定低 64 MiB 管理上限与缺少正式物理直映的部分已由
  [ADR 0017](0017-linux-style-physical-memory-and-direct-map.md) 取代
- 日期：2026-07-24
- 关联阶段：v0.6 内存管理

## 背景

内核不能把 QEMU 的 `-m 64` 参数等同于“物理地址 `0..64 MiB` 全部可用”。
PC 地址空间还可能包含 ROM、固件保留区、设备 MMIO 窗口和高地址空洞。此前
Stage 1 只为进入长模式建立固定的低 64 MiB 大页身份映射，它既不表达物理页
所有权，也不能提供代码只读、数据不可执行和栈 guard 等长期策略。

传统 BIOS 常通过实模式 `INT 15h, E820h` 提供内存图，但本项目禁止借用
SeaBIOS/OVMF，并且自研 ROM 尚未实现这一软件中断 ABI。QEMU PC 同时提供
`fw_cfg` 配置设备；QEMU 模拟该硬件接口，来宾仍需自行完成端口访问、目录解析、
格式检查和内核策略。

## 决策

### 内存图来源

Stage 1 使用 `fw_cfg` 的 selector 端口 `0x510` 和 data 端口 `0x511`：

1. 选择 key `0x0000` 并验证四字节签名为 `QEMU`。
2. 选择 key `0x0019`，按大端序读取文件目录数量。
3. 流式遍历最多 256 个 64 字节目录项，精确匹配 `etc/e820`。
4. 验证文件非空、长度为 20 的整数倍、条目数在 `1..128`。
5. 把 `{u64 base, u64 length, u32 type}` 转成项目
   `{u64 base, u64 length, u32 type, u32 attributes}`，attributes 当前补零。
6. 按 base 升序排列后，通过 BootInfo v2 交给内核。

`fw_cfg` 是当前 QEMU PC 平台的硬件契约，不成为通用内核接口。未来真实平台
可由自研 ACPI/UEFI 兼容层或其他固件后端产生同一 24 字节规范化内存图。

### 页帧状态

物理分配器当前只管理低 64 MiB 的 4 KiB 帧，每帧使用 2 bit：
unavailable、free、allocated、reserved。相较一位位图增加的元数据可控
（16384 帧共 4096 字节），却能把“固件不可用”“启动长期保留”和“动态所有权”
区分开，释放与重复释放因此具有明确失败语义。

保留低 1 MiB、链接器符号界定的完整内核映像和 64 KiB 初始栈。保留操作先
检查完整范围，遇到 allocated 帧时原子失败，不允许留下部分状态变化。

### 地址空间

内核从页帧分配器建立新的 PML4、PDPT、PD 和 PT，所有叶映射使用 4 KiB 页：

- 低 64 MiB继续身份映射，便于在本阶段访问已有内核对象和页表帧。
- 零页、初始栈底和三个 IST 栈底的 guard page 保持 not-present。
- `.text` 为 RX，`.rodata` 为 R/NX，`.data/.bss` 为 RW/NX。
- 64 KiB 早期堆位于 `0xFFFF800000000000`，权限为 RW/NX。
- 写保护测试页位于 `0xFFFF800000100000`，权限为 R/NX。

内核在切换 CR3 前确认 CPUID 报告 NX，设置 `IA32_EFER.NXE` 和 `CR0.WP`。
只查询页表位不构成最终验收；独立 QEMU 镜像让 Ring 0 写只读测试页，必须产生
#PF、错误码 `0x3` 和精确 CR2，证明处理器实际执行了 WP 权限。

### 早期堆

本阶段采用 64 KiB 单调分配器，支持二的幂对齐、溢出检查和显式容量失败，
不支持释放。它只用于启动期永久对象，避免在尚无锁、页回收和并发模型时假装
存在通用堆。

## 被否决的方案

### 把 `-m 64` 写成常量并全部标为空闲

该方案把宿主启动参数泄漏为内核事实，无法表示地址空洞和保留区，也不能验证
内存发现失败路径，因此否决。

### 使用 BIOS E820 软件中断

项目没有外部 BIOS，临时引入 SeaBIOS 会替代自研启动链；仅为 v0.6 在 ROM 中
实现完整 BIOS 中断兼容层也偏离当前最小增量，因此否决。

### 继续沿用 Stage 1 的 2 MiB 大页

大页无法单独保护 4 KiB guard、代码尾页和早期堆页。Stage 1 页表仍保留为
可靠模式切换工具，但内核必须接管 CR3，因此否决长期沿用。

### 一开始实现伙伴系统和通用可释放堆

伙伴系统、合并、并发和对象回收会同时扩大状态空间，掩盖地址翻译与权限学习
目标。本阶段先交付可审计的帧状态机和单调堆，后续在真实连续分配与回收需求
出现时演进。

## 后果

- BootInfo ABI 从版本 1 升级到版本 2，旧内核必须被明确拒绝。
- 内核启动依赖 QEMU PC `fw_cfg`；这是当前平台依赖，不是永久跨平台承诺。
- 页表映射失败时可能保留已经分配的中间表，启动会立即停机；通用事务回滚和
  空表回收仍待后续实现。
- 低 64 MiB 身份映射仍扩大了内核可访问范围；在用户地址空间阶段将改为更窄
  的内核直映或显式物理映射策略。
- 新的权限和 guard page 让原本潜伏的空指针、栈溢出和写只读数据错误转成
  可诊断页故障。

## 验证

- 单元测试覆盖内存图全部拒绝状态、2-bit 分配/释放/保留原子性、堆对齐与
  页表项往返。
- 集成测试覆盖启动内存布局和实际 QEMU 内存图的保留统计。
- 固定种子随机测试执行 8192 组页表项权限往返和 4096 步分配器参考模型对照。
- QEMU 成功路径必须在 `PAGING_READY`、`MEMORY_PERMISSIONS_VALID` 和
  `HEAP_SELF_TEST_PASSED` 后到达 `READY`。
- `MEMORY_MAP_INVALID` 故障镜像禁止越过 Stage 1 内存发现边界。
- 写保护镜像必须报告向量 14、错误码 `0x3`、
  CR2=`0xFFFF800000100000` 和 `PANIC`。

## 参考

- QEMU fw_cfg specification：
  <https://qemu.readthedocs.io/en/master/specs/fw_cfg.html>
- QEMU i386 fw_cfg implementation（`etc/e820` 注册位置）：
  <https://gitlab.com/qemu-project/qemu/-/blob/master/hw/i386/fw_cfg.c>
- Intel 64 and IA-32 Architectures Software Developer's Manual，卷 3A，
  分页、控制寄存器与页故障章节。
