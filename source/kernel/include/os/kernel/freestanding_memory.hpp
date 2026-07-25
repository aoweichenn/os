#pragma once

#include <stdint.h>

// 编译器可以把大型对象的清零和复制降级为 C 运行时符号。项目没有外部
// libc，因此在最小 ABI 边界自行提供实现；参数 value 的 int 类型由 C ABI 强制。
extern "C" void *memset(void *destination, int value, uint64_t length_bytes) noexcept;
extern "C" void *memcpy(void *destination, const void *source, uint64_t length_bytes) noexcept;
