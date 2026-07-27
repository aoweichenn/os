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
OS_KERNEL_ELF_MAXIMUM_FILE_SIZE_BYTES = 0x0010_0000
OS_KERNEL_ELF_MAXIMUM_PROGRAM_HEADER_COUNT = 64
OS_KERNEL_ELF_MINIMUM_LOAD_ADDRESS = 0x0010_0000
OS_KERNEL_ELF_MAXIMUM_LOAD_END_ADDRESS = 0x03E0_0000
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
OS_KERNEL_ELF_ARCHITECTED_EXCEPTION_VECTOR_COUNT = 32
OS_KERNEL_ELF_EXCEPTION_VECTOR_SYMBOL_PREFIX = "os_kernel_exception_vector_"
OS_KERNEL_ELF_LEGACY_INTERRUPT_FIRST_VECTOR = 32
OS_KERNEL_ELF_LEGACY_INTERRUPT_VECTOR_COUNT = 16
OS_KERNEL_ELF_HARDWARE_INTERRUPT_VECTOR_SYMBOL_PREFIX = (
    "os_kernel_hardware_interrupt_vector_"
)
OS_KERNEL_ELF_FORBIDDEN_RUNTIME_INITIALIZATION_SECTIONS = (
    ".init_array",
    ".fini_array",
    ".ctors",
    ".dtors",
)
OS_KERNEL_ELF_IDLE_WAIT_SYMBOL = (
    "os::kernel::EnableInterruptsWaitAndDisable()"
)
OS_KERNEL_ELF_IDLE_WAIT_INSTRUCTION_SEQUENCE = ("sti", "hlt", "cli")
OS_KERNEL_ELF_REQUIRED_ARCHITECTURE_SYMBOLS = frozenset(
    (
        "OsKernelEntry",
        "OsKernelLoadGdtAndTss",
        "OsKernelLoadIdt",
        "OsKernelExceptionDispatch",
        "OsKernelDispatchException",
        "os_kernel_exception_stub_table",
        "OsKernelHardwareInterruptDispatch",
        "OsKernelDispatchHardwareInterrupt",
        "os_kernel_hardware_interrupt_stub_table",
        "OsKernelSystemCallEntry",
        "OsKernelSystemCallDispatch",
        "OsKernelNativeSystemCallEntry",
        "OsKernelDispatchSystemCall",
        "OsKernelPrepareUserReturn",
        "OsKernelSelectUserReturn",
        "OsKernelEnterScheduledProcess",
        "OsKernelReturnFromUserMode",
        "os_kernel_user_smoke_elf_start",
        "os_kernel_user_smoke_elf_end",
        "os_kernel_user_invalid_opcode_elf_start",
        "os_kernel_user_invalid_opcode_elf_end",
        "os_kernel_user_page_fault_elf_start",
        "os_kernel_user_page_fault_elf_end",
        "os_kernel_image_start",
        "os_kernel_image_end",
        "os_kernel_text_start",
        "os_kernel_text_end",
        "os_kernel_read_only_data_start",
        "os_kernel_read_only_data_end",
        "os_kernel_writable_data_start",
        "os_kernel_writable_data_end",
    )
)


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
    if len(kernelElf) > OS_KERNEL_ELF_MAXIMUM_FILE_SIZE_BYTES:
        raise OsToolError("内核 ELF 超出 Stage 1 暂存区容量。")

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
    if (
        programHeaderCount == 0
        or programHeaderCount
        > OS_KERNEL_ELF_MAXIMUM_PROGRAM_HEADER_COUNT
    ):
        raise OsToolError("内核 ELF 程序头数量超出 Stage 1 能力。")
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
        fileRangeEnd = checkedAddressEnd(
            fileOffset,
            fileSizeBytes,
            "文件加载范围",
        )
        if fileRangeEnd > len(kernelElf):
            raise OsToolError("内核可加载段越过文件末尾。")
        if not isPowerOfTwo(alignmentBytes):
            raise OsToolError("内核可加载段对齐不是二的幂。")
        if alignmentBytes != OS_KERNEL_ELF_EXPECTED_SEGMENT_ALIGNMENT_BYTES:
            raise OsToolError("内核可加载段没有使用项目规定的页对齐。")
        if fileOffset % alignmentBytes != virtualAddress % alignmentBytes:
            raise OsToolError("内核可加载段的文件与虚拟地址对齐不一致。")
        if virtualAddress != physicalAddress:
            raise OsToolError("内核可加载段不符合初期恒等装载契约。")
        physicalEndAddress = checkedAddressEnd(
            physicalAddress,
            memorySizeBytes,
            "物理装载范围",
        )
        if physicalAddress < OS_KERNEL_ELF_MINIMUM_LOAD_ADDRESS:
            raise OsToolError("内核可加载段低于目标装载区。")
        if physicalEndAddress > OS_KERNEL_ELF_MAXIMUM_LOAD_END_ADDRESS:
            raise OsToolError("内核可加载段越过目标装载区。")
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


def validateKernelArchitectureSymbols(definedSymbols: set[str]) -> None:
    requiredSymbols = set(OS_KERNEL_ELF_REQUIRED_ARCHITECTURE_SYMBOLS)
    requiredSymbols.update(
        f"{OS_KERNEL_ELF_EXCEPTION_VECTOR_SYMBOL_PREFIX}{vector}"
        for vector in range(
            OS_KERNEL_ELF_ARCHITECTED_EXCEPTION_VECTOR_COUNT
        )
    )
    requiredSymbols.update(
        f"{OS_KERNEL_ELF_HARDWARE_INTERRUPT_VECTOR_SYMBOL_PREFIX}{vector}"
        for vector in range(
            OS_KERNEL_ELF_LEGACY_INTERRUPT_FIRST_VECTOR,
            OS_KERNEL_ELF_LEGACY_INTERRUPT_FIRST_VECTOR
            + OS_KERNEL_ELF_LEGACY_INTERRUPT_VECTOR_COUNT,
        )
    )
    missingSymbols = sorted(requiredSymbols - definedSymbols)
    if missingSymbols:
        raise OsToolError(
            "内核 ELF 缺少描述符、异常或硬件中断入口符号："
            + ", ".join(missingSymbols)
        )


def validateKernelRuntimeInitializationSections(sectionHeaders: str) -> None:
    forbiddenSections = [
        sectionName
        for sectionName in OS_KERNEL_ELF_FORBIDDEN_RUNTIME_INITIALIZATION_SECTIONS
        if sectionName in sectionHeaders
    ]
    if forbiddenSections:
        raise OsToolError(
            "内核 ELF 依赖自举代码未执行的 C++ 动态初始化区段："
            + ", ".join(forbiddenSections)
        )


def validateKernelIdleWaitInstructionSequence(disassembly: str) -> None:
    functionHeader = f"<{OS_KERNEL_ELF_IDLE_WAIT_SYMBOL}>:"
    functionInstructions: list[str] = []
    insideFunction = False
    for line in disassembly.splitlines():
        strippedLine = line.strip()
        if strippedLine.endswith(functionHeader):
            insideFunction = True
            continue
        if insideFunction and strippedLine.endswith(">:"):
            break
        if not insideFunction or "\t" not in line:
            continue
        instructionText = line.rsplit("\t", maxsplit=1)[-1].strip()
        if instructionText:
            functionInstructions.append(instructionText.split(maxsplit=1)[0])

    if not insideFunction:
        raise OsToolError("内核 ELF 缺少原子空闲等待函数。")
    sequenceLength = len(OS_KERNEL_ELF_IDLE_WAIT_INSTRUCTION_SEQUENCE)
    for instructionIndex in range(
        len(functionInstructions) - sequenceLength + 1
    ):
        if (
            tuple(
                functionInstructions[
                    instructionIndex : instructionIndex + sequenceLength
                ]
            )
            == OS_KERNEL_ELF_IDLE_WAIT_INSTRUCTION_SEQUENCE
        ):
            return
    raise OsToolError(
        "内核空闲等待没有生成相邻的 STI、HLT、CLI 指令。"
    )


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

    definedSymbolResult = runCommand(
        [
            "llvm-nm",
            "--defined-only",
            "--format=posix",
            str(kernelElfPath),
        ],
        projectRoot,
        captureOutput=True,
    )
    definedSymbols = {
        symbolLine.split(maxsplit=1)[0]
        for symbolLine in definedSymbolResult.stdout.splitlines()
        if symbolLine.strip()
    }
    validateKernelArchitectureSymbols(definedSymbols)

    sectionHeaderResult = runCommand(
        ["llvm-readelf", "--section-headers", "--wide", str(kernelElfPath)],
        projectRoot,
        captureOutput=True,
    )
    validateKernelRuntimeInitializationSections(sectionHeaderResult.stdout)

    disassemblyResult = runCommand(
        [
            "llvm-objdump",
            "--disassemble",
            "--demangle",
            str(kernelElfPath),
        ],
        projectRoot,
        captureOutput=True,
    )
    validateKernelIdleWaitInstructionSequence(disassemblyResult.stdout)

    print(
        "内核 ELF64 审计通过："
        f"入口 0x{entryAddress:016X}，"
        f"{len(loadSegments)} 个可加载段。"
    )
