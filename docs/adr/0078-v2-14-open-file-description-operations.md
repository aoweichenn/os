# ADR 0078：V2.14 打开文件描述与定位 I/O

状态：已接受

日期：2026-08-24

## 问题

V2.13 已能用稳定目录 fd 完成路径事务，但打开普通文件之后仍只有顺序 read/write：用户态
不能显式调整共享 offset，不能在不改变 offset 的情况下执行定位 I/O，也不能直接针对已打开
vnode 查询或修改元数据。既有 append 还在单个 FileDescription 锁下执行“读取文件尾再写入”，
两个独立 open 之间没有共同串行点，无法保证并发追加不覆盖。

## 决策

- FileDescription 是顺序 offset 和 file status flags 的唯一所有者；duplicate/fork 继续共享同一
  对象和 offset。`Seek` 仅接受普通文件，支持 Beginning/Current/End，并用无符号分支处理正负
  位移，拒绝负结果、超过 `INT64_MAX` 的结果及不可定位对象；后者使用新错误 `NotSeekable`；
- `TryReadAt/TryWriteAt` 直接调用 VFS 的显式 offset 路径，不读取或修改共享 offset。为保持
  Linux 的既有兼容行为，带 append 状态的 positioned write 仍原子写到文件尾，但不推进共享
  offset；
- VFS 增加一个不跨 user-copy 的 append `RuntimeMutex`，把“读取逻辑文件大小、按 RLIMIT_FSIZE
  裁剪、WriteAt”放在同一临界区。它覆盖同一 VFS 中的独立 open description，页缓存与后端
  仍负责数据和 metadata 一致性；
- `Stat/Truncate/ChangeMode/ChangeOwner` 从已打开 `OpenFile` 的稳定 vnode 身份执行，不重新解析
  路径。truncate 在进入后端前由 ProcessRuntime 取消预读、写保护共享映射、释放 EOF 外映射，
  并通过同页 writeback wait 收束该文件全部写回；全局 VMA 扫描只接受 owner 已发布
  `address_space_stable` 且状态为 Alive/Stopped 的进程；只有准备成功才提交后端 truncate；
- file status flags 冻结为 Readable/Writable/Append。get 返回完整状态；set 必须原样保留 open 时
  的 access bits，只允许对 writable regular file 切换 Append。close-on-exec 仍是 FileTable 的
  descriptor flag，不能混入该接口；
- ABI v2.6.0 只在 96 后追加 97..105：seek、pread/pwrite、fstat/ftruncate、fchmod/fchown、
  get/set file status flags；1..96 与旧结构保持不变，错误区间只追加 -60 `NotSeekable`；
- 九个新用户包装单独编译为 function sections，LLD 只把实际引用的包装保留进静态 Ring 3
  ELF；基础 `OsUserInvokeSystemCall` 桩由 linker script 单独 KEEP，保持所有用户 ELF 的审计
  契约。ABI 扩展不能让无关程序共同多映射一页或放大全系统 rootfs 冷页工作集；
- 热路径只维护 seek、positioned I/O、metadata 与 status-update 聚合计数。Ring 3 探针成功时
  不逐操作打印，所有新操作完成后执行一次明确同步，让写脏页在进程退出前收束。

## 不变量

- duplicate/fork 观察同一个顺序 offset；positioned I/O 成功或失败都不得改变该 offset；
- 两个独立 append description 的每次写入必须各自连续且互不覆盖，RLIMIT 裁剪必须在取得
  append 串行点后基于实际文件尾决定；
- `SEEK_END` 使用包含页缓存逻辑大小的打开文件 stat，不得退回过期的后端 inode size；
- fstat/fchmod/fchown 在 rename/unlink 后仍以已打开 vnode 身份工作，不重新遍历原路径；
- truncate 不得在页仍为 Loading/Writeback 时先提交后端再由 cache 返回 `EntryBusy`；准备失败
  必须保持盘面 size 未变，并输出 cancel/protect/mapping/writeback 失败阶段；
- buffered write 的 writeback backing 必须在可能阻塞的 page Acquire 之后 retain，并在同一
  无阻塞窗口内 MarkDirty；retained size 预先覆盖本次完整 write end，后台 clean release 不能
  在“backing 已返回、Dirty 尚不可见”窗口提前关闭它；
- 地址空间 owner 必须在 Destroy 前先清除 stable 位；`active && CR3!=0` 或尚为 Alive 都不足以
  证明 VMA/FileBacking 可遍历，stable+Alive/Stopped 才是全局映射操作的稳定集合；
- access mode 在 FileDescription 生命周期内不可由 status flag set 提权，descriptor flag 与
  file status flag 必须继续分层；
- operation lock 不得跨 user-copy；VFS append/metadata 锁不得持有 FileTable 或对象管理器锁；
- rootfs v4、磁盘格式、4 GiB RAM、128 GiB 磁盘、VGA 协议与单 BSP 边界保持不变。

## 验证

- ABI unit 精确冻结 97..105、三种 seek origin、三项 status flag、v2.6.0 和 -60；
- FileDescription lifecycle integration 覆盖 duplicate 共享 seek offset、pread 不改 offset、
  负 seek、只读 append 拒绝、独立 open 并发 append 和 Linux append+pwrite 偏差；
- VFS/rootfs focused tests 继续覆盖 cached size、truncate、metadata invalidation、权限与资源守恒；
- 4 GiB primary 重复运行覆盖 background writeback 抢在 ftruncate 前后的两种交错；
- Ring 3 `fs_probe` 在真实 rootfs 上串联 seek/read/write-at、fstat/ftruncate、fchmod/fchown 与
  status flags，并在最终 sync 后重新读取核对持久数据；
- fresh CAW 的 4 GiB ATA primary、ATA/NVMe reclaim 与完整 verify 共同验收 VGA 可见画面、
  writeback 收束和全系统资源归零。

## 后果

V2.14 形成了现代 Unix 打开文件描述的基础语义：路径只负责取得对象，后续定位、定位 I/O、
元数据和 append 都围绕稳定 fd 工作。当前不实现稀疏洞打孔、`fallocate`、`copy_file_range`、
`io_uring`、异步文件 I/O、O_DIRECT、租约/锁或完整 `fcntl` 命令矩阵；这些需要独立缓存、
权限和取消协议。
