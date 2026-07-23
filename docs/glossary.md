# 项目词汇表

| 术语 | 含义 |
| --- | --- |
| freestanding | 不假定操作系统、C/C++ 标准运行时或完整标准库存在的编译环境 |
| host | 执行构建和快速测试的宿主机环境 |
| target | 项目生成代码所面向的 x86-64 环境 |
| TCG | QEMU 的动态二进制翻译执行引擎，可在非 x86-64 宿主机上模拟 x86-64 |
| ROM | 映射到处理器复位地址附近的只读固件镜像 |
| reset vector | x86 CPU 复位后开始取指的地址，本项目关注 `0xFFFFFFF0` |
| Stage 1 | 固件从磁盘载入的第一阶段引导代码 |
| Long Mode | x86-64 的 64 位执行模式 |
| half-open range | 包含起始地址、不包含结束地址的区间 `[begin, end)` |
| property test | 通过大量生成输入验证通用性质的随机测试 |
| ADR | Architecture Decision Record，记录重要架构决策及其原因 |
