#pragma once

#include <stdint.h>

namespace os::abi {

inline constexpr uint64_t OS_ABI_ELF64_HEADER_SIZE_BYTES = 64ULL;
inline constexpr uint64_t OS_ABI_ELF64_PROGRAM_HEADER_SIZE_BYTES = 56ULL;
inline constexpr uint8_t OS_ABI_ELF_MAGIC_BYTE_0 = 0x7FU;
inline constexpr uint8_t OS_ABI_ELF_MAGIC_BYTE_1 = 0x45U;
inline constexpr uint8_t OS_ABI_ELF_MAGIC_BYTE_2 = 0x4CU;
inline constexpr uint8_t OS_ABI_ELF_MAGIC_BYTE_3 = 0x46U;
inline constexpr uint8_t OS_ABI_ELF_CLASS_64 = 0x02U;
inline constexpr uint8_t OS_ABI_ELF_LITTLE_ENDIAN = 0x01U;
inline constexpr uint8_t OS_ABI_ELF_IDENTIFICATION_VERSION = 0x01U;
inline constexpr uint16_t OS_ABI_ELF_EXECUTABLE_TYPE = 0x0002U;
inline constexpr uint16_t OS_ABI_ELF_X86_64_MACHINE = 0x003EU;
inline constexpr uint32_t OS_ABI_ELF_CURRENT_VERSION = 0x00000001U;
inline constexpr uint32_t OS_ABI_ELF_LOAD_PROGRAM_TYPE = 0x00000001U;
inline constexpr uint32_t OS_ABI_ELF_PROGRAM_EXECUTE_FLAG = 0x00000001U;
inline constexpr uint32_t OS_ABI_ELF_PROGRAM_WRITE_FLAG = 0x00000002U;
inline constexpr uint32_t OS_ABI_ELF_PROGRAM_READ_FLAG = 0x00000004U;
inline constexpr uint32_t OS_ABI_ELF_PROGRAM_KNOWN_FLAG_MASK =
    OS_ABI_ELF_PROGRAM_EXECUTE_FLAG | OS_ABI_ELF_PROGRAM_WRITE_FLAG |
    OS_ABI_ELF_PROGRAM_READ_FLAG;

}
