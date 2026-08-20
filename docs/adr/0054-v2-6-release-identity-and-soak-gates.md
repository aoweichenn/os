# ADR 0054：v2.6 发布身份、结构化清单与长稳门禁

状态：已接受，候选实现中
日期：2026-08-20

## 背景

v2.1 至 v2.5 已分别完成 VGA/手机交互、Shell、本地文件系统、权限和内存压力。
这些机制单独通过并不等于同一发布产物已经冻结：项目版本、ABI、rootfs、来宾
banner、QEMU 机器规格、两块磁盘和网站可能仍引用不同阶段。v2.6 不增加内核
机制，而是把这些身份和最终整机证据变成机器可拒绝的门禁。

128 GiB rootfs 和 28.44 GiB 交换盘不适合逐次计算完整逻辑 SHA-256；对全零空闲
区做 156 GiB 顺序读取只增加闪存写放大和发布时间。另一方面，只记录文件长度
也无法区分错误的 ROM、Kernel、rootfs superblock 或交换盘格式。

## 决策

### 冻结三类版本

项目版本提升为 2.6.0。ABI 保持 2.3.0、84 个系统调用和最后错误码 -59；rootfs
保持格式 4。`audit-release-identity` 同时读取 CMake、ABI 头、rootfs 头、Kernel、
PID1、参数探针、Shell、QEMU runner、README 和 v2.6 发布记录。任一消费者仍为
v2.5 或引用不同容量时立即失败。

### 结构化发布清单

`release-manifest` 接受精确 40 位主仓 SHA 和最终构建路径，输出排序 JSON。清单
记录：

- 项目、ABI、rootfs 和 4 GiB/128 GiB/28 GiB 机器身份；
- CMake/README/docs/source/tools/tests 的确定性源码树 SHA-256；
- 仅 `source/` 目标代码的文件数与 C++/NASM 行数；
- ROM 和 Kernel 载荷的完整 SHA-256；
- rootfs 盘的 4 MiB 启动前缀、rootfs superblock 和最后扇区 SHA-256；
- 交换盘 superblock 和最后扇区 SHA-256；
- 每个产物的逻辑字节数、宿主已分配字节数和 sparse 判定。

大盘使用结构化身份而不是完整逻辑哈希。启动前缀覆盖自研 Stage 1 与 Kernel
容器；rootfs/交换盘 superblock 冻结各自盘面；最后扇区冻结 LBA 边界。手机运行
副本仍由 `audit-allocated-image` 保证没有宿主空洞。

### 已物化整机长稳

`qemu-soak` 默认连续运行三次完整 4 GiB `-mem-prealloc` 工作负载，最多允许 16
次。命令在启动前拒绝稀疏 rootfs 或交换盘；每轮复用生产 ROM、同一自研启动链、
VGA/QMP 双证据和既有强 marker，使用 QEMU snapshot 避免前一轮改变下一轮基线。
任何一次失败立即停止，不能用后续成功稀释失败。

日常 `verify` 不物化 156.44 GiB，也不默认运行长稳。候选发布在 caw 的独立真实
磁盘目录生成唯一已物化副本，运行全量 CTest、三轮 soak 和结构化清单后删除
临时大盘；手机再生成长期副本完成 UI、旋转、温度和存储余量验收。

## 不变量

- `project=2.6.0`、`ABI=2.3.0/84/-59`、`rootfs=4` 在所有消费者中一致。
- 发布清单的主仓 SHA 是 40 位小写十六进制，并等于已推送主仓 `HEAD`。
- ROM/Kernel 使用完整哈希；大盘结构化哈希范围与字节数固定。
- soak 只使用已物化两盘和 4 GiB 预分配 RAM，迭代次数位于 1..16。
- 网站只能从清单绑定的已推送主仓 SHA 重新同步。

## 后果与边界

冻结门禁能在发布前发现只改 CMake、漏改来宾 banner、误用稀疏手机盘或网站绑定
旧 SHA 等问题。结构化大盘身份避免 156 GiB 全盘哈希，但它不是通用取证哈希；
未覆盖的空闲区依靠镜像生成器、`posix_fallocate` 零填充语义、fsck 和整机启动
共同约束。

教材有独立编辑与审校生命周期；v2.6 主工程不得覆盖或夹带未经审查的书稿改动。
教材、网站和 Sites 仍按 `docs/releasing.md` 的顺序完成，任何外部步骤未完成时
只能声明“主工程候选完成、公开发布未完成”。
