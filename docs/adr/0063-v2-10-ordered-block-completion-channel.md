# ADR 0063：V2.10 有序块完成通道

状态：已接受（第一增量）

日期：2026-08-22

## 问题

`BlockRequestQueue` 已能按设备几何验证请求、限制 outstanding 深度、稳定签发、处理乱序
完成、超时和排队取消，但完成者必须已经知道 request identifier，再执行 `Read` 和
`Reap`。ATA 可以依赖单飞 active request，NVMe 和未来页缓存却需要一个设备无关的完成
出口；若每个驱动自行维护 completion list，上层会再次绑定控制器细节。

## 决策

在 `BlockRequestQueue` 内增加第二条侵入式 FIFO。Queued FIFO 仍按提交顺序签发；请求由
IRQ、超时或取消路径首次解析为 Completed 时，按解析发生顺序追加到 completion FIFO。
`TakeCompletion` 返回独立 `BlockCompletion`，包含 request id、操作、LBA、块数、owner
thread 和终态结果，并在同一提交中从完成队列移除、回收请求槽。空完成队列返回
Succeeded 和 `available=false`，调用方不得把它解释为错误或伪造事件。

保留按 identifier 的 `Read`/`Reap`，用于驱动恢复和失败清理；任意位置直接 Reap 必须
同时从 completion FIFO 摘除，不得改变其余完成的顺序。ATA IRQ 与 timeout 路径改为
消费 `TakeCompletion`，证明生产驱动不再依赖私有 Read+Reap 组合。本增量不改变同步
`BlockDevice`、rootfs、journal、swap 或 NVMe 轮询语义。

## 不变量

- 每个 Completed 请求在 completion FIFO 中恰好出现一次；
- IRQ、timeout、cancel 竞争只允许首个终态进入 FIFO，重复解析不得产生第二条完成；
- completion 顺序按解析发生顺序，不按 request id、提交顺序或设备槽排序；
- `TakeCompletion` 成功交付后 request id 立即失效，buffer 所有权归还原 owner；
- `submission = active + reap`，`resolution = completed + reap`；
- completion delivery 不得超过 reap，空队列不得修改累计统计；
- FIFO 和请求存储均由调用方提供，IRQ 路径不分配、不阻塞、不调用 VFS；
- 本增量不宣称 rootfs 已异步化，后续增量才能迁移生产读写。

## 后果

ATA 与未来 NVMe 可以把硬件完成统一交给 request owner，页缓存 waiter 不需要理解控制器
active slot。每个请求增加一个 completion link，队列回收任意完成项需要有界线性摘链；
容量当前最多 64，该成本可控。后续异步 BlockDevice adapter、BlockIo WaitQueue 和同页
Loading waiter 都建立在这条完成通道之上。
