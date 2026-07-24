from dataclasses import dataclass
from pathlib import Path
import struct

from .errors import OsToolError
from .process import runCommand


OS_KERNEL_ELF_MAGIC = b"\x7fELF"
OS_KERNEL_ELF_CLASS_64 = 2
OS_KERNEL_ELF_DATA_LITTLE_ENDIAN = 1
OS_KERNEL_ELF_VERSION_CURRENT = 1
OS_KERNEL_ELF_TYPE_EXECUTABLE = 2
OS_KERNEL_ELF_MACHINE_X86_64 = 0x003E
OS_KERNEL_ELF_HEADER_SIZE_BYTES = 64
OS_KERNEL_ELF_PROGRAM_HEADER_SIZE_BYTES = 56
OS_KERNEL_ELF_EXPECTED_ENTRY_ADDRESS = 0x0010_0000
OS_KERNEL_ELF_PROGRAM_TYPE_LOAD = 1
OS_KERNEL_ELF_PROGRAM_FLAG_EXECUTE = 0x1
OS_KERNEL_ELF_PROGRAM_FLAG_WRITE = 0x2
OS_KERNEL_ELF_PROGRAM_FLAG_READ = 0x4
OS_KERNEL_ELF_PROGRAM_FLAG_KNOWN_MASK = (
    OS_KERNEL_ELF_PROGRAM_FLAG_EXECUTE
    | OS_KERNEL_ELF_PROGRAM_FLAG_WRITE
    | OS_KERNEL_ELF_PROGRAM_FLAG_READ
)
OS_KERNEL_ELF_EXPECTED_SEGMENT_ALIGNMENT_BYTES = 0x1000
OS_KERNEL_ELF_MAXIMUM_ADDRESS = 0xFFFF_FFFF_FFFF_FFFF
OS_KERNEL_ELF_HEADER_FORMAT = "<16sHHIQQQIHHHHHH"
OS_KERNEL_ELF_PROGRAM_HEADER_FORMAT = "<IIQQQQQQ"
OS_KERNEL_ELF_IDENT_MAGIC_END_OFFSET = 4
OS_KERNEL_ELF_IDENT_CLASS_OFFSET = 4
OS_KERNEL_ELF_IDENT_DATA_OFFSET = 5
OS_KERNEL_ELF_IDENT_VERSION_OFFSET = 6
OS_KERNEL_ELF_NO_ALIGNMENT = 0
OS_KERNEL_ELF_POWER_OF_TWO_DECREMENT = 1
OS_KERNEL_ELF_EMPTY_SEGMENT_SIZE_BYTES = 0


@dataclass(frozen=True)
class KernelLoadSegment:
    fileOffset: int
    virtualAddress: int
    physicalAddress: int
    fileSizeBytes: int
    memorySizeBytes: int
    flags: int
    alignmentBytes: int


def isPowerOfTwo(value: int) -> bool:
    return (
        value > OS_KERNEL_ELF_NO_ALIGNMENT
        and value & (value - OS_KERNEL_ELF_POWER_OF_TWO_DECREMENT) == 0
    )


def checkedAddressEnd(begin: int, sizeBytes: int, fieldName: str) -> int:
    if begin > OS_KERNEL_ELF_MAXIMUM_ADDRESS - sizeBytes:
        raise OsToolError(f"内核 ELF 的{fieldName}发生 64 位溢出。")
    return begin + sizeBytes


def rangesOverlap(
    firstBegin: int,
    firstEnd: int,
    secondBegin: int,
    secondEnd: int,
) -> bool:
    return firstBegin < secondEnd and secondBegin < firstEnd


def validateLoadSegmentRelationships(
    loadSegments: list[KernelLoadSegment],
) -> None:
    for currentIndex, currentSegment in enumerate(loadSegments):
        currentVirtualEnd = checkedAddressEnd(
            currentSegment.virtualAddress,
            currentSegment.memorySizeBytes,
            "虚拟地址范围",
        )
        currentPhysicalEnd = checkedAddressEnd(
            currentSegment.physicalAddress,
            currentSegment.memorySizeBytes,
            "物理地址范围",
        )
        for previousSegment in loadSegments[:currentIndex]:
            previousVirtualEnd = checkedAddressEnd(
                previousSegment.virtualAddress,
                previousSegment.memorySizeBytes,
                "虚拟地址范围",
            )
            previousPhysicalEnd = checkedAddressEnd(
                previousSegment.physicalAddress,
                previousSegment.memorySizeBytes,
                "物理地址范围",
            )
            if rangesOverlap(
                previousSegment.virtualAddress,
                previousVirtualEnd,
                currentSegment.virtualAddress,
                currentVirtualEnd,
            ):
                raise OsToolError("内核可加载段的虚拟地址范围重叠。")
            if rangesOverlap(
                previousSegment.physicalAddress,
                previousPhysicalEnd,
                currentSegment.physicalAddress,
                currentPhysicalEnd,
            ):
                raise OsToolError("内核可加载段的物理地址范围重叠。")


def parseKernelLoadSegments(
    kernelElf: bytes,
) -> tuple[int, tuple[KernelLoadSegment, ...]]:
    if len(kernelElf) < OS_KERNEL_ELF_HEADER_SIZE_BYTES:
        raise OsToolError("内核 ELF 头被截断。")

    (
        identification,
        elfType,
        machine,
        version,
        entryAddress,
        programHeaderOffset,
        _sectionHeaderOffset,
        _flags,
        elfHeaderSizeBytes,
        programHeaderSizeBytes,
        programHeaderCount,
        _sectionHeaderSizeBytes,
        _sectionHeaderCount,
        _sectionNameIndex,
    ) = struct.unpack_from(OS_KERNEL_ELF_HEADER_FORMAT, kernelElf)

    if (
        identification[:OS_KERNEL_ELF_IDENT_MAGIC_END_OFFSET]
        != OS_KERNEL_ELF_MAGIC
    ):
        raise OsToolError("内核文件缺少 ELF magic。")
    if identification[OS_KERNEL_ELF_IDENT_CLASS_OFFSET] != OS_KERNEL_ELF_CLASS_64:
        raise OsToolError("内核 ELF 不是 64 位类别。")
    if (
        identification[OS_KERNEL_ELF_IDENT_DATA_OFFSET]
        != OS_KERNEL_ELF_DATA_LITTLE_ENDIAN
    ):
        raise OsToolError("内核 ELF 不是小端格式。")
    if (
        identification[OS_KERNEL_ELF_IDENT_VERSION_OFFSET]
        != OS_KERNEL_ELF_VERSION_CURRENT
    ):
        raise OsToolError("内核 ELF 标识版本不受支持。")
    if elfType != OS_KERNEL_ELF_TYPE_EXECUTABLE:
        raise OsToolError("内核 ELF 不是可执行文件。")
    if machine != OS_KERNEL_ELF_MACHINE_X86_64:
        raise OsToolError("内核 ELF 目标机器不是 x86-64。")
    if version != OS_KERNEL_ELF_VERSION_CURRENT:
        raise OsToolError("内核 ELF 头版本不受支持。")
    if elfHeaderSizeBytes != OS_KERNEL_ELF_HEADER_SIZE_BYTES:
        raise OsToolError("内核 ELF 头长度不正确。")
    if programHeaderSizeBytes != OS_KERNEL_ELF_PROGRAM_HEADER_SIZE_BYTES:
        raise OsToolError("内核 ELF 程序头长度不正确。")
    if programHeaderOffset < OS_KERNEL_ELF_HEADER_SIZE_BYTES:
        raise OsToolError("内核 ELF 程序头表与文件头重叠。")

    programHeaderTableSizeBytes = programHeaderSizeBytes * programHeaderCount
    programHeaderTableEnd = checkedAddressEnd(
        programHeaderOffset,
        programHeaderTableSizeBytes,
        "程序头表范围",
    )
    if programHeaderTableEnd > len(kernelElf):
        raise OsToolError("内核 ELF 程序头表越界。")

    loadSegments: list[KernelLoadSegment] = []
    for programHeaderIndex in range(programHeaderCount):
        currentHeaderOffset = (
            programHeaderOffset
            + programHeaderIndex * programHeaderSizeBytes
        )
        (
            programType,
            programFlags,
            fileOffset,
            virtualAddress,
            physicalAddress,
            fileSizeBytes,
            memorySizeBytes,
            alignmentBytes,
        ) = struct.unpack_from(
            OS_KERNEL_ELF_PROGRAM_HEADER_FORMAT,
            kernelElf,
            currentHeaderOffset,
        )
        if programType != OS_KERNEL_ELF_PROGRAM_TYPE_LOAD:
            continue
        if fileSizeBytes > memorySizeBytes:
            raise OsToolError("内核可加载段的文件长度大于内存长度。")
        if memorySizeBytes == OS_KERNEL_ELF_EMPTY_SEGMENT_SIZE_BYTES:
            raise OsToolError("内核可加载段的内存长度为零。")
        if fileOffset + fileSizeBytes > len(kernelElf):
            raise OsToolError("内核可加载段越过文件末尾。")
        if not isPowerOfTwo(alignmentBytes):
            raise OsToolError("内核可加载段对齐不是二的幂。")
        if alignmentBytes != OS_KERNEL_ELF_EXPECTED_SEGMENT_ALIGNMENT_BYTES:
            raise OsToolError("内核可加载段没有使用项目规定的页对齐。")
        if fileOffset % alignmentBytes != virtualAddress % alignmentBytes:
            raise OsToolError("内核可加载段的文件与虚拟地址对齐不一致。")
        if virtualAddress != physicalAddress:
            raise OsToolError("内核可加载段不符合初期恒等装载契约。")
        if programFlags & ~OS_KERNEL_ELF_PROGRAM_FLAG_KNOWN_MASK:
            raise OsToolError("内核可加载段包含未知权限位。")
        if not programFlags & OS_KERNEL_ELF_PROGRAM_FLAG_READ:
            raise OsToolError("内核可加载段缺少读取权限。")
        if (
            programFlags & OS_KERNEL_ELF_PROGRAM_FLAG_WRITE
            and programFlags & OS_KERNEL_ELF_PROGRAM_FLAG_EXECUTE
        ):
            raise OsToolError("内核可加载段同时可写与可执行。")
        loadSegments.append(
            KernelLoadSegment(
                fileOffset=fileOffset,
                virtualAddress=virtualAddress,
                physicalAddress=physicalAddress,
                fileSizeBytes=fileSizeBytes,
                memorySizeBytes=memorySizeBytes,
                flags=programFlags,
                alignmentBytes=alignmentBytes,
            )
        )

    if not loadSegments:
        raise OsToolError("内核 ELF 没有可加载段。")
    validateLoadSegmentRelationships(loadSegments)
    return entryAddress, tuple(loadSegments)


def validateKernelEntry(
    entryAddress: int,
    loadSegments: tuple[KernelLoadSegment, ...],
) -> None:
    if entryAddress != OS_KERNEL_ELF_EXPECTED_ENTRY_ADDRESS:
        raise OsToolError("内核 ELF 入口地址不符合链接契约。")
    for loadSegment in loadSegments:
        segmentEnd = (
            loadSegment.virtualAddress + loadSegment.memorySizeBytes
        )
        if (
            loadSegment.flags & OS_KERNEL_ELF_PROGRAM_FLAG_EXECUTE
            and loadSegment.virtualAddress <= entryAddress < segmentEnd
        ):
            return
    raise OsToolError("内核 ELF 入口不在可执行加载段中。")


def auditKernelElf(projectRoot: Path, kernelElfPath: Path) -> None:
    entryAddress, loadSegments = parseKernelLoadSegments(
        kernelElfPath.read_bytes()
    )
    validateKernelEntry(entryAddress, loadSegments)

    undefinedSymbolResult = runCommand(
        ["llvm-nm", "--undefined-only", str(kernelElfPath)],
        projectRoot,
        captureOutput=True,
    )
    if undefinedSymbolResult.stdout.strip():
        raise OsToolError("内核 ELF 包含未解析运行时符号。")

    print(
        "内核 ELF64 审计通过："
        f"入口 0x{entryAddress:016X}，"
        f"{len(loadSegments)} 个可加载段。"
    )
