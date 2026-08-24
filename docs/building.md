# 构建说明

## 支持环境

当前基线支持 Linux 宿主机。宿主机可以不是 x86-64；Clang 负责生成 x86-64
目标文件，QEMU TCG 负责模拟 x86-64 CPU 与 PC 硬件。

必需工具：

- Clang、Clang++、Clang-Tidy 与 `run-clang-tidy`
- LLD
- NASM
- QEMU `qemu-system-x86_64`
- GDB
- Python 3.11 或更高版本
- CMake 3.28 或更高版本
- Ninja
- LLVM `nm`、`objdump`、`objcopy` 与 `readelf`

Fedora：

```bash
sudo dnf install clang clang-tools-extra lld nasm qemu-system-x86-core gdb cmake ninja-build python3
```

Ubuntu：

```bash
sudo apt-get install clang clang-tidy lld nasm qemu-system-x86 gdb cmake ninja-build python3
```

QEMU 软件包可能连带安装 SeaBIOS 或 OVMF 文件，但项目运行命令始终通过
`-bios` 显式指定自研 ROM，不会使用这些固件。

## 一键验证

```bash
python3 tools/os.py verify
```

`test` 与 `verify` 默认使用 20 路 CTest 并行；全部 4 GiB QEMU 用例共享资源锁，
因此仍一次只运行一个来宾。无需在命令行额外传 `-j`。

Python 入口依次执行：

1. 检查全部必要工具。
2. 使用 `developer` CMake preset 配置工程。
3. 构建宿主测试库和 x86-64 freestanding 库。
4. 生成自研 ROM、Stage 1、v1.14 ELF64 内核、二十二个用户 ELF、一个截断
   ELF 夹具，把 init、Shell、multi-call 核心工具及功能探针安装进 rootfs；
   同时生成格式损坏、目标 ATA、
   内存图失败、非法指令、页故障和写保护注入镜像，并保留 v0.0 空镜像
   回归基线。
5. 运行全部 CTest 测试，包括基于编译数据库的 Clang AST 标识符门禁、
   命名空间单词门禁、64 MiB bootstrap 与 256 MiB functional 系统用例。
6. 发布前运行 4 GiB `-mem-prealloc` 手机主规格；它不进入日常 `verify`，避免
   每次局部验证都真实提交 4 GiB 宿主 RAM。

正常 QEMU 系统用例包含显式 `-m 64` 的 bootstrap、`-m 256` 的 functional
门禁和发布时显式 `-m 4096 -mem-prealloc` 的手机主规格；三者运行同一份 Shell、
IPC、文件系统、用户隔离和资源生命周期实现。故障注入和最小兼容路径保留
64 MiB。主规格会真实提交约 4 GiB RAM，宿主还需为 QEMU 和测试进程预留空间。

## 手动构建

```bash
python3 tools/os.py doctor
python3 tools/os.py configure
python3 tools/os.py build
python3 tools/os.py test
python3 tools/os.py source-metrics
python3 tools/os.py audit-release-identity
python3 tools/os.py phone-book-export
```

手机或桌面实际运行前，先把唯一 rootfs 与交换盘物化：

```bash
python3 tools/os.py materialize-image \
  build/developer/images/boot_disk.img \
  build/developer/images/boot_disk_allocated.img
python3 tools/os.py materialize-image \
  build/developer/images/swap_disk.img \
  build/developer/images/swap_disk_allocated.img
```

两个命令合计需要约 156.44 GiB 可用空间。`audit-allocated-image` 可独立复查，
`qemu-display` 默认也会拒绝 `st_blocks * 512 < st_size` 的路径。

v2.6 候选在两个已物化镜像上连续运行三次完整 4 GiB 工作负载：

```bash
python3 tools/os.py qemu-soak \
  build/developer/images/firmware.bin \
  build/developer/images/boot_disk_allocated.img \
  131072 137438953472 \
  --swap-disk-image build/developer/images/swap_disk_allocated.img
```

`--iterations` 允许 1..16，发布默认值为 3。每轮使用 snapshot，任一轮缺 marker、
黑屏、panic、资源泄漏或超时都会立即失败。

生成供网站和发布记录使用的结构化清单：

```bash
python3 tools/os.py release-manifest \
  build/developer/project_release.json \
  <40位已推送主仓SHA> \
  build/developer/images/firmware.bin \
  build/developer/source/kernel/kernel.payload.elf \
  build/developer/images/boot_disk_allocated.img \
  build/developer/images/swap_disk_allocated.img
```

清单同时记录候选源码树 SHA-256；ROM/Kernel 使用完整 SHA-256；两块大盘记录
固定范围哈希、逻辑/已分配长度与 sparse 判定，避免顺序读取 156 GiB 空闲零区。

在有 GTK 图形会话的本机打开 VGA 窗口：

```bash
python3 tools/os.py qemu-display \
  build/developer/images/firmware.bin \
  build/developer/images/boot_disk_allocated.img \
  131072 137438953472 \
  --swap-disk-image build/developer/images/swap_disk_allocated.img
```

纯终端环境可增加 `--display-backend curses`。默认使用磁盘快照；只有明确需要
保留来宾写入时才增加 `--persistent-disk-writes`。该命令仍使用自研 ROM、
QEMU TCG 和显式 VGA 设备，不启用串口输出。详细来宾日志默认持续写到
`build/developer/qemu-display.log`；可用
`--guest-log-file <path>` 指定其他宿主路径，VGA 只保留用户终端和紧急错误。

宿主没有桌面、需要从手机查看时，先在宿主启动仅回环监听的 VNC：

```bash
python3 tools/os.py qemu-display \
  build/developer/images/firmware.bin \
  build/developer/images/boot_disk.img \
  131072 137438953472 \
  --display-backend vnc
```

另一个终端安装 noVNC，把本机 VNC 转成仍只监听回环地址的网页：

```bash
sudo dnf install novnc python3-websockify
novnc_proxy \
  --listen 127.0.0.1:6080 \
  --vnc 127.0.0.1:5901 \
  --web /usr/share/novnc \
  --file-only
```

再开一个终端，把 `6080` 的 HTTP/WebSocket 服务发布到自己的 Tailscale
网络：

```bash
tailscale serve --bg http://127.0.0.1:6080
tailscale serve status
```

首次使用时 Tailscale 可能输出一次性的 Serve 启用链接，需由 tailnet 所有者确认。
手机登录同一 Tailscale 账号后，打开
`https://<宿主的 Tailscale DNS 名称>/vnc.html?autoconnect=1&resize=scale`。
QEMU 与 noVNC 都不直接监听公网或局域网地址；停止后用 `tailscale serve reset`
移除转发。若 `5901` 已占用，可给 QEMU 增加 `--vnc-display-number 2`，并把
noVNC 的目标端口同步改为 `5902`。

若 QEMU 就运行在手机本机的 Termux/proot 中，Android 与 Termux 共享回环网络，
不需要 Tailscale。把 `tools/novnc_mobile.html` 链接到 noVNC Web 根后可直接由
Termux 打开：

```bash
ln -s "$PWD/tools/novnc_mobile.html" /usr/share/novnc/os_mobile.html
termux-open-url http://127.0.0.1:6080/os_mobile.html
```

手机页面底部固定提供命令输入、回车、退格、Ctrl-C、Ctrl-Z 和缩放切换。
页面默认自动适应当前横竖屏；“原始清晰度”切回 VGA 720×400 像素一一对应，
适合拖动查看。竖屏时控制区固定在底部，横屏时移到右侧，把剩余区域全部留给
等比 VGA 画面。目标 Shell 只接受当前 PS/2/ASCII 键盘路径支持的字符。

只运行 4 GiB 预分配正常整机验收：

```bash
python3 tools/os.py qemu-firmware \
  build/developer/images/firmware.bin \
  build/developer/images/boot_disk.img \
  131072 137438953472 \
  --memory-mebibytes 4096 \
  --expected-outcome success
```

只运行当前唯一 4 GiB 系统规格：

```bash
ctest --test-dir build/developer \
  --output-on-failure \
  -R '^os_qemu_primary_smoke$'
```

该用例从磁盘启动 PID1，执行完整进程树、spawn/exec/wait、外部 Shell 命令、
文件系统、用户隔离和资源快照。自动门禁不再注册 64/256 MiB 重复档。

只验证原生系统调用能力失败边界：

```bash
python3 tools/os.py qemu-firmware \
  build/developer/images/firmware.bin \
  build/developer/images/boot_disk.img \
  131072 137438953472 \
  --memory-mebibytes 64 \
  --cpu-model 'qemu64,-syscall' \
  --expected-outcome processor-feature-unsupported
```

`--cpu-model` 只改变 QEMU 模拟 CPU 暴露的 CPUID 特性，不替换固件、Stage 1
或 Kernel。该命令必须到达 `PROCESSOR_FEATURES_UNSUPPORTED` 和非零
`PROCESSOR_MISSING_FEATURES`，并在扩展现场、GDT 与 Ring 3 之前有界结束。
默认 CPU 型号固定为 `qemu64`。

## 构建产物

所有产物位于 `build/developer/`：

```text
source/foundation/libos_foundation_host.a
source/foundation/libos_foundation_x86_64.a
source/firmware/generated/os_firmware.elf
source/firmware/generated/os_firmware_vga_failure.elf
source/firmware/generated/os_firmware_ide_busy_failure.elf
source/firmware/generated/os_firmware_ide_error_failure.elf
source/boot/stage1/generated/stage1.bin
source/boot/stage1/generated/stage1_memory_map_invalid.bin
source/kernel/kernel.elf
source/kernel/kernel.payload.elf
source/kernel/kernel_invalid_opcode.elf
source/kernel/kernel_invalid_opcode.payload.elf
source/kernel/kernel_page_fault.elf
source/kernel/kernel_page_fault.payload.elf
source/kernel/kernel_write_protection.elf
source/kernel/kernel_write_protection.payload.elf
source/user/user_init.elf
source/user/user_orphan_parent.elf
source/user/user_orphan_child.elf
source/user/user_argument_probe.elf
source/user/user_exec_probe.elf
source/user/user_exec_target.elf
source/user/user_file_system_probe.elf
source/user/user_truncated.elf
images/firmware.bin
images/firmware_vga_failure.bin
images/firmware_ide_busy_failure.bin
images/firmware_ide_error_failure.bin
images/boot_disk.img
images/stage1_invalid_header_disk.img
images/stage1_invalid_checksum_disk.img
images/kernel_invalid_header_disk.img
images/kernel_invalid_checksum_disk.img
images/kernel_invalid_elf_disk.img
images/stage1_kernel_ata_timeout/boot_disk.img
images/stage1_kernel_ata_error/boot_disk.img
images/stage1_memory_map_invalid/boot_disk.img
images/kernel_invalid_opcode/boot_disk.img
images/kernel_page_fault/boot_disk.img
images/kernel_write_protection/boot_disk.img
images/empty_firmware.bin
images/empty_disk.img
tests/os_foundation_unit_tests
tests/os_foundation_integration_tests
tests/os_foundation_randomized_tests
tests/os_kernel_boot_info_unit_tests
tests/os_kernel_descriptor_layout_unit_tests
tests/os_kernel_descriptor_layout_randomized_tests
tests/os_kernel_handoff_layout_integration_tests
tests/os_kernel_physical_memory_map_unit_tests
tests/os_kernel_physical_frame_allocator_unit_tests
tests/os_kernel_heap_and_page_layout_unit_tests
tests/os_kernel_memory_bootstrap_integration_tests
tests/os_kernel_memory_management_randomized_tests
tests/os_kernel_device_model_unit_tests
tests/os_kernel_device_bootstrap_integration_tests
tests/os_kernel_interrupt_device_randomized_tests
```

`libos_foundation_x86_64.a` 必须是 x86-64 ELF，且不能包含未解析的外部运行时符号。
`kernel.elf` 必须是入口为 `0x00100000` 的 x86-64 `ET_EXEC`，入口位于可执行
`PT_LOAD` 段，且不能包含未解析符号。它保留 DWARF；同名
`*.payload.elf` 由 `llvm-objcopy --strip-debug` 生成，三个运行时
`PT_LOAD` 与入口不变，并作为实际写盘载荷。所有故障内核和用户隔离内核变体
都遵循同一规则，避免 Debug 非加载段增长后覆盖 LBA 32768 的 rootfs 区域。
启动暂存区和磁盘边界审计作用于实际写盘的 payload；带 DWARF 的
`kernel.elf` 仍做符号、段权限与 GDB 调试输入，但不拿非加载调试段长度冒充
Stage 1 载荷长度。
`firmware.bin` 和失败路径变体必须都是精确 131072 字节。
全部启动磁盘镜像必须具有精确 137438953472 字节逻辑长度。镜像使用稀疏文件
保存，不能把“逻辑容量”误当作“宿主实际占用”；派生镜像必须保留尾部 extent
与逻辑长度。宿主文件系统若不支持 `SEEK_DATA/SEEK_HOLE`，工具会拒绝逐字节
扫描 128 GiB 镜像并删除未完成副本；不得为了“兼容”退化为普通全盘复制。
`build/` 不进入 Git。

## 固件生成链

```text
reset_and_vga.asm
  └─ NASM elf32 → .o
       └─ LLD + rom.ld → .elf
            └─ llvm-objcopy --gap-fill=0xff → 128 KiB .bin
```

`elf32` 是保存 16 位代码节和符号的目标文件容器，不表示 CPU 已进入 32 位模式。
链接脚本把 VGA 字形固定到 `0xFFFFE000`、入口固定到 `0xFFFFF000`，并用
`ASSERT` 保证字形不侵入入口、入口不侵占最后 16 字节的复位向量区域。

## Stage 1 生成链

```text
source/boot/stage1/src/entry.asm
source/boot/stage1/src/kernel_loader.asm
source/boot/stage1/src/memory_map.asm
  └─ NASM bin → stage1.bin
       └─ Python Stage 1 格式编码与校验
```

使用 `python3 tools/os.py audit-stage1 build/developer/images/boot_disk.img`
可以独立检查描述符、磁盘范围、加载范围和负载校验。Python 只负责宿主镜像
编码与审计；目标机上的 Stage 1 描述符解析、ATA PIO 和跳转全部由自研固件
执行。进入长模式后，`kernel_loader.asm` 用另一条自研 ATA PIO 路径读取
Kernel。

rootfs v4 由构建图在 Kernel 启动前显式格式化，Kernel 没有自动格式化入口：

```bash
python3 tools/os.py inspect-rootfs build/developer/images/boot_disk.img
python3 tools/os.py fsck-rootfs build/developer/images/boot_disk.img
```

创建独立实验镜像时使用：

```bash
python3 tools/os.py mkfs-rootfs /tmp/rootfs-lab.img \
  --create --size-bytes 137438953472
python3 tools/os.py corrupt-rootfs /tmp/rootfs-lab.img superblock-checksum
```

`inspect-rootfs` 输出 JSON 摘要；`fsck-rootfs` 只读重建可达 inode/data
bitmap；`corrupt-rootfs` 只用于具名故障注入。

v2.16 的 v5 盘面使用独立命令，不能与生产 v4 混用：

```bash
python3 tools/os.py mkfs-rootfs-v5 /tmp/rootfs-v5-lab.img --create
python3 tools/os.py inspect-rootfs-v5 /tmp/rootfs-v5-lab.img
python3 tools/os.py fsck-rootfs-v5 /tmp/rootfs-v5-lab.img
python3 tools/os.py corrupt-rootfs-v5 /tmp/rootfs-v5-lab.img descriptor-checksum
```

默认 profile 的逻辑镜像为 128 GiB，创建时可保持宿主稀疏；`--create` 不覆盖已有路径，
`--force` 才允许显式重建。它目前只用于离线实验，不是手机运行盘，也不能替换构建图中的
rootfs v4。小几何由 Python 测试直接传入 profile，避免日常故障矩阵扫描完整 inode table。

v2.17 journal v2 目前是 freestanding 库与 hosted 故障模型，没有独立 CLI，也不会修改
`mkfs-rootfs-v5` 创建的空镜像。定向验证使用 developer CTest preset：

```bash
ctest --preset developer -R root_journal_v2 --output-on-failure
```

不要把 hosted journal test device 生成的 4096-block 小介质当作 QEMU 磁盘；它只模拟
512B sector、Flush、volatile cache 和 Crash。journal/orphan inode 的实际 v5 镜像映射从
v2.18 allocator/extent 阶段开始。

## Kernel ELF64 生成链

```text
source/kernel/src/*.cpp ─ Clang x86_64-unknown-none-elf ─┐
source/kernel/src/arch/architecture.asm ─ NASM elf64 ─────────┤
                                                        └─ LLD elf_x86_64
                                                           + kernel.ld
                                                           → kernel.elf
            └─ Python ELF64 结构审计 + llvm-nm 符号审计
```

宿主为 ARM64 时不经过宿主 GCC 链接驱动，CMake 显式调用 LLD 的
`elf_x86_64` 模式。可独立执行：

```bash
python3 tools/os.py audit-kernel-elf build/developer/source/kernel/kernel.elf
```

所有 Ring 3 程序都是独立 ELF64 产物，可分别审计。v1.14 的正常磁盘启动程序
例如：

```bash
python3 tools/os.py audit-user-elf build/developer/source/user/user_init.elf
python3 tools/os.py audit-user-elf build/developer/source/user/user_shell.elf
python3 tools/os.py audit-user-elf build/developer/source/user/user_exec_target.elf
python3 tools/os.py audit-user-elf build/developer/source/user/user_fork_probe.elf
python3 tools/os.py audit-user-elf build/developer/source/user/user_thread_probe.elf
python3 tools/os.py audit-user-elf build/developer/source/user/user_time_probe.elf
python3 tools/os.py audit-user-elf build/developer/source/user/user_signal_probe.elf
python3 tools/os.py audit-user-elf build/developer/source/user/user_core_tool.elf
```

审计器要求 AMD64 `ET_EXEC`、入口位于可执行 `PT_LOAD`、段 4 KiB 对齐、
用户地址范围、W^X、无重叠和零未解析符号。它不替代内核解析器：宿主审计
证明“构建产生了预期文件”，QEMU 路径证明“目标内核自己拒绝或装入文件”。
截断夹具故意不是合法 ELF，不能使用成功审计命令；它由 exec 失败回滚路径
验证。历史调度 worker、IPC 和用户异常 ELF 继续作为回归或具名故障输入。

## Boot Disk 组合链

```text
stage1.bin ────────────────┐
                           ├─ boot_disk.img
kernel.elf ─ strip-debug ─ kernel.payload.elf ─ 审计 ─┘
                                  ├─ Stage 1 描述符与负载
                                  └─ Kernel 描述符、CRC32 与 ELF 文件
user_*.elf ─ audit ─ mkfs/install ─ rootfs v4 (/sbin + /bin) ─┘
```

`kernel.elf` 的链接命令直接依赖生成的 `architecture.o` 与最小
`user_images.o`，而不只依赖 phony 目标。正常 Shell、PID1 和功能探针不再
嵌入 Kernel：任一普通用户 ELF 改变会触发 ELF 审计、rootfs 重新安装和启动盘
重组；三个启动模式夹具改变才触发 Kernel 重新嵌入、链接与 strip。两条依赖链
都由真实输出文件连接，增量构建不会让 QEMU 误跑旧程序。

离线安装可单独用于实验镜像：

```bash
python3 tools/os.py copy-file-prefix \
  build/developer/source/user/user_init.elf \
  /tmp/init.prefix 4096
```

生产构建不会只复制 ELF 前缀；`copy-file-prefix` 在这里用于制造截断失败夹具。
完整程序由 rootfs 工具按精确文件长度写入嵌套目录，并在工具测试中重新解析
inode/块树、逐字节回读。

可分别审计同一磁盘中的两个阶段：

```bash
python3 tools/os.py audit-stage1 build/developer/images/boot_disk.img
python3 tools/os.py audit-kernel-image build/developer/images/boot_disk.img
```

构建同时生成 Kernel 描述符损坏、Kernel ELF 内容损坏、CRC 正确但 ELF
语义非法、目标 ATA 永久忙/设备错误、`fw_cfg` 内存图失败，内核
`UD2`/not-present 页故障/写保护页故障，以及用户 `#UD`、用户 `#PF` 和
截断用户 ELF 的失败镜像。宿主审计拒绝磁盘和 ELF 不可信输入，QEMU 测试
进一步证明 Stage 1 与 Kernel 自己走到对应边界。

## 教材构建

教材是独立工程，不强制 CI 安装完整 TeX Live：

```bash
make -C books/x86-64-os-from-reset check
make -C books/x86-64-os-from-reset pdf
```

PDF 生成在 `books/x86-64-os-from-reset/source/latex/main.pdf`，属于构建产物，
不进入 Git。

`source-metrics` 只统计 `source/` 下核心 `.asm`、`.cpp` 和 `.hpp` 的非空、
非纯注释行。汇编 include、测试、宿主工具、书稿、网站、构建描述和链接脚本
不计入操作系统本体代码量。教材构建会自动刷新同一统计结果。

## 手机教材导出

手机书库沿用硬件教材的分类和命名规则：

```bash
make -C books/x86-64-os-from-reset phone-export
```

目标文件为
`/mnt/sdcard/STU/BOOKS/按卷类型/原理卷/从复位向量到自研x8664操作系统/从复位向量到自研x8664操作系统.pdf`。
目录与文件名不含短横线、空格或 `+`，避免微信读书导入链路误处理。导出工具
只允许替换这个目录中的同名 PDF；若目录含有其他文件则拒绝覆盖。复制完成后
会核对 SHA-256。

## 编译边界

x86-64 目标使用 freestanding C++20，并关闭：

- 编译器内建运行时假设
- 异常
- RTTI
- 位置无关代码
- 栈保护
- 线程安全局部静态初始化
- `__cxa_atexit`
- x86-64 红区、编译器生成的 MMX 和 SSE
- 宿主 C++ 标准库头文件

这些限制由 CMake 目标 `os_foundation_x86_64` 集中管理，后续固件与内核
目标必须复用同一策略。v1.2 的 FXSAVE/FXRSTOR 和用户态模式验收只存在于
显式 NASM Intel 汇编边界；普通 C++ 不会隐式占用被测试的 XMM/x87 状态。
v1.3 的 SYSCALL/SYSRET、SWAPGS 与 MSR 操作也只存在于最小架构边界；
UserContext 校验和 MSR 布局策略保持为可由宿主测试的普通 C++20。

## 构建职责

- CMake 描述模块、目标、源文件和依赖关系。
- Ninja 执行增量构建。
- Python 提供稳定命令入口并管理外部进程。
- CTest 保存测试注册、标签和完成判定。
- Clang-Tidy 按 `.clang-tidy` 检查变量、函数和命名空间；Python 词法门禁
  另外保证命名空间每层匹配 `[a-z]+`。二者都不参与依赖扫描或目标代码生成。

Python 工具只使用标准库，不自行扫描 C++ 依赖，也不替代 CMake 生成构建图。
