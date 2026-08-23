# ADR 0064：V2.10 异步块设备适配层

状态：已接受（第二增量）

日期：2026-08-22

## 问题

同步 `BlockDevice` 只表达调用返回前已经完成的 Read/Write/Flush。ATA 另有 request queue、
IRQ14 和 timeout API，NVMe 又以 command identifier、DMA slot、MSI-X 和控制器 reset 表达
同一生命周期。若 BlockIo 直接识别这两套接口，后续 WaitQueue、页缓存和回收会依赖控制器
细节，也无法统一取消和失败交付。

## 决策

在同步接口旁增加独立 `AsynchronousBlockDevice`，使用静态函数表和
`AsynchronousBlockDeviceAdapter<DriverType>` 做类型擦除，不使用 virtual、RTTI、异常或
动态分配。公共操作固定为 Geometry、Submit、best-effort Cancel、ResolveTimeouts 和
TakeCompletion。Submit 接收 request buffer、owner thread 与绝对 deadline；成功只表示
设备取得请求所有权，最终结果必须由 `BlockCompletion` 交付。

ATA 同时实现同步和异步 adapter。提交后尝试启动单个硬件请求；IRQ14、timer timeout 和
立即签发失败只把请求解析进 completion FIFO，`InterruptRuntime` 再通过公共
TakeCompletion 唤醒 owner 并启动下一项。

NVMe namespace 同样实现异步 adapter。64 位公共 request identifier 与 16 位硬件 command
identifier 分离：前者跨 command-id 回绕保持 owner 身份，后者只匹配 CQE。完成槽按实际
CQ 顺序链接；Read 使用驱动自有 DMA 页，IRQ 只标记 Completed，数据回拷和槽位释放在非
IRQ 的 TakeCompletion 阶段执行。一个命令超时会触发控制器 reset：最早到期请求得到
TimedOut，其余未完成异步请求得到 DeviceError，已经排队的完成必须跨 reset 保留。

取消采用 best-effort 语义。ATA 尚未签发的 queued 请求可以变为 Cancelled；ATA active
请求和已经写入 NVMe SQ 的请求返回 RequestInProgress，不伪造硬件已撤销。同步与异步
NVMe 请求在存在活动槽时不得混用；第三增量迁移生产 rootfs/swap 后再移除该过渡限制。

## 不变量

- 上层只依赖块几何、request id、owner、deadline 和 completion；
- request id 在 TakeCompletion 前唯一，不能等同于可回绕的 NVMe command id；
- IRQ 路径不分配、不执行 DMA 大块回拷、不进入 VFS；
- timeout/reset 后每个已接受异步请求仍恰有一个终态完成；
- 空完成队列返回 Succeeded 和 `available=false`；
- 已提交硬件的 cancel 拒绝不得改变请求状态或 buffer 所有权；
- 同步 `BlockDevice`、rootfs v4、journal、swap 与 ABI 2.4.0 在本增量保持不变。

## 后果

V2.10.3 可以只面向一个异步设备契约实现 BlockIo WaitQueue，不再复制 ATA/NVMe 分支。
代价是 NVMe slot 增加公共身份、owner、buffer 和 completion link；Read 槽在消费完成前
不能复用。真正的生产异步读还需要第三增量提供非 IRQ bottom-half/Worker，再由其执行
TakeCompletion 和唤醒线程。
