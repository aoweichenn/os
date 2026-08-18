# ADR 0049：受控 ShellEditor、环境展开与有界 glob

状态：已接受
日期：2026-08-18

## 背景

原 Terminal 在 Kernel canonical 行规程中编辑，Shell 只有 Enter 后才能读到
完整行，因此用户态无法实现左右插入、历史或 Tab 补全。直接永久切成 raw 又会
破坏 `cat`、Ctrl-C/Ctrl-Z 和前后台组语义。变量/glob 若在控制序列切分前做文本
替换，还可能把数据误解释成新的操作符。

## 决策

1. ABI 增加 `Canonical/ShellEditor` 两态，不提供通用 termios。只有 controlling
   session 的 foreground PGID 可在输入、edit、EOF 缓冲全空时切换。
2. ShellEditor 逐字节提交且 Kernel 不回显；Shell 维护 512 字节行、cursor、
   16 条相邻去重历史和有界命令共同前缀补全。外部管线与 fg 作业运行期间切回
   Canonical，所有返回路径恢复前台组后再恢复 ShellEditor。
3. 扩展键编码为 `ESC [ A/B/C/D`。VGA 只实现 CSI 上下左右、home、`2K`、`2J`
   子集；未知序列有界丢弃，不引入通用终端模拟器。
4. 环境表固定 32 项，每项最多 127 个 `NAME=value` 字节；赋值与 export 写同一
   导出表，unset 删除，exec 用现有 ProcessString 传递副本。
5. 顶层先切分控制 span，单词解析时再展开 `$NAME`、`${NAME}`、`$?`。展开值不
   重新解释为控制操作符，也不做隐式 field splitting。
6. 每个存储字节记录 glob flag。仅未引用、未转义的 `*`/`?` 生效；只展开最后
   路径组件、隐藏名要求显式点、匹配按字节排序，结果仍不得超过每 stage 8 参数。

## 后果

- Shell 可在手机 VGA 前台完成真实左右编辑、历史和命令补全，同时保持原作业
  控制与 canonical 程序行为。
- 所有表、路径、匹配和历史都有编译期上限；容量失败不会产生部分 exec。
- 当前不支持通用 raw/echo flags、路径补全、field splitting、递归 glob、locale
  排序或多个终端。
