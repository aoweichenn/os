from dataclasses import dataclass
from pathlib import Path
import struct

from .errors import OsToolError
from .process import runCommand


OS_USER_ELF_MAGIC = b"\x7fELF"
OS_USER_ELF_CLASS_64 = 2
OS_USER_ELF_DATA_LITTLE_ENDIAN = 1
OS_USER_ELF_VERSION_CURRENT = 1
OS_USER_ELF_TYPE_EXECUTABLE = 2
OS_USER_ELF_MACHINE_X86_64 = 0x003E
OS_USER_ELF_HEADER_SIZE_BYTES = 64
OS_USER_ELF_PROGRAM_HEADER_SIZE_BYTES = 56
OS_USER_ELF_MAXIMUM_PROGRAM_HEADER_COUNT = 8
OS_USER_ELF_MAXIMUM_MAPPED_PAGE_COUNT = 32
OS_USER_ELF_MINIMUM_VIRTUAL_ADDRESS = 0x0000_0000_4000_0000
OS_USER_ELF_MAXIMUM_VIRTUAL_ADDRESS_EXCLUSIVE = 0x0000_0000_8000_0000
OS_USER_ELF_PROGRAM_TYPE_LOAD = 1
OS_USER_ELF_PROGRAM_FLAG_EXECUTE = 0x1
OS_USER_ELF_PROGRAM_FLAG_WRITE = 0x2
OS_USER_ELF_PROGRAM_FLAG_READ = 0x4
OS_USER_ELF_PROGRAM_FLAG_KNOWN_MASK = (
    OS_USER_ELF_PROGRAM_FLAG_EXECUTE
    | OS_USER_ELF_PROGRAM_FLAG_WRITE
    | OS_USER_ELF_PROGRAM_FLAG_READ
)
OS_USER_ELF_PAGE_SIZE_BYTES = 0x1000
OS_USER_ELF_MAXIMUM_ADDRESS = 0xFFFF_FFFF_FFFF_FFFF
OS_USER_ELF_HEADER_FORMAT = "<16sHHIQQQIHHHHHH"
OS_USER_ELF_PROGRAM_HEADER_FORMAT = "<IIQQQQQQ"
OS_USER_ELF_IDENT_MAGIC_END_OFFSET = 4
OS_USER_ELF_IDENT_CLASS_OFFSET = 4
OS_USER_ELF_IDENT_DATA_OFFSET = 5
OS_USER_ELF_IDENT_VERSION_OFFSET = 6
OS_USER_ELF_REQUIRED_SYMBOLS = frozenset(
    (
        "OsUserEntry",
        "OsUserInvokeSystemCall",
    )
)


@dataclass(frozen=True)
class UserLoadSegment:
    fileOffset: int
    virtualAddress: int
    fileSizeBytes: int
    memorySizeBytes: int
    flags: int


def checkedUserAddressEnd(begin: int, sizeBytes: int, fieldName: str) -> int:
    if begin > OS_USER_ELF_MAXIMUM_ADDRESS - sizeBytes:
        raise OsToolError(f"用户 ELF 的{fieldName}发生 64 位溢出。")
    return begin + sizeBytes


def userRangesOverlap(
    firstBegin: int,
    firstEnd: int,
    secondBegin: int,
    secondEnd: int,
) -> bool:
    return firstBegin < secondEnd and secondBegin < firstEnd


def parseUserLoadSegments(
    userElf: bytes,
) -> tuple[int, tuple[UserLoadSegment, ...]]:
    if len(userElf) < OS_USER_ELF_HEADER_SIZE_BYTES:
        raise OsToolError("用户 ELF 头被截断。")

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
    ) = struct.unpack_from(OS_USER_ELF_HEADER_FORMAT, userElf)

    if (
        identification[:OS_USER_ELF_IDENT_MAGIC_END_OFFSET]
        != OS_USER_ELF_MAGIC
        or identification[OS_USER_ELF_IDENT_CLASS_OFFSET]
        != OS_USER_ELF_CLASS_64
        or identification[OS_USER_ELF_IDENT_DATA_OFFSET]
        != OS_USER_ELF_DATA_LITTLE_ENDIAN
        or identification[OS_USER_ELF_IDENT_VERSION_OFFSET]
        != OS_USER_ELF_VERSION_CURRENT
    ):
        raise OsToolError("用户 ELF 标识不是小端 ELF64 当前版本。")
    if elfType != OS_USER_ELF_TYPE_EXECUTABLE:
        raise OsToolError("用户 ELF 不是 ET_EXEC 可执行文件。")
    if machine != OS_USER_ELF_MACHINE_X86_64:
        raise OsToolError("用户 ELF 目标机器不是 x86-64。")
    if version != OS_USER_ELF_VERSION_CURRENT:
        raise OsToolError("用户 ELF 头版本不受支持。")
    if elfHeaderSizeBytes != OS_USER_ELF_HEADER_SIZE_BYTES:
        raise OsToolError("用户 ELF 头长度不正确。")
    if programHeaderSizeBytes != OS_USER_ELF_PROGRAM_HEADER_SIZE_BYTES:
        raise OsToolError("用户 ELF 程序头长度不正确。")
    if (
        programHeaderCount == 0
        or programHeaderCount > OS_USER_ELF_MAXIMUM_PROGRAM_HEADER_COUNT
    ):
        raise OsToolError("用户 ELF 程序头数量超出内核能力。")
    if programHeaderOffset < OS_USER_ELF_HEADER_SIZE_BYTES:
        raise OsToolError("用户 ELF 程序头表与文件头重叠。")
    programHeaderTableEnd = checkedUserAddressEnd(
        programHeaderOffset,
        programHeaderCount * programHeaderSizeBytes,
        "程序头表范围",
    )
    if programHeaderTableEnd > len(userElf):
        raise OsToolError("用户 ELF 程序头表越界。")

    loadSegments: list[UserLoadSegment] = []
    mappedPageCount = 0
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
            OS_USER_ELF_PROGRAM_HEADER_FORMAT,
            userElf,
            currentHeaderOffset,
        )
        if programType != OS_USER_ELF_PROGRAM_TYPE_LOAD:
            raise OsToolError("用户 ELF 包含当前阶段不支持的程序头。")
        if (
            programFlags & ~OS_USER_ELF_PROGRAM_FLAG_KNOWN_MASK
            or not programFlags & OS_USER_ELF_PROGRAM_FLAG_READ
            or (
                programFlags & OS_USER_ELF_PROGRAM_FLAG_WRITE
                and programFlags & OS_USER_ELF_PROGRAM_FLAG_EXECUTE
            )
        ):
            raise OsToolError("用户 ELF 加载段权限无效或违反 W^X。")
        if (
            alignmentBytes != OS_USER_ELF_PAGE_SIZE_BYTES
            or fileOffset % OS_USER_ELF_PAGE_SIZE_BYTES != 0
            or virtualAddress % OS_USER_ELF_PAGE_SIZE_BYTES != 0
            or physicalAddress != virtualAddress
        ):
            raise OsToolError("用户 ELF 加载段不满足整页恒等地址契约。")
        if memorySizeBytes == 0 or fileSizeBytes > memorySizeBytes:
            raise OsToolError("用户 ELF 加载段内存范围无效。")
        fileEnd = checkedUserAddressEnd(
            fileOffset,
            fileSizeBytes,
            "文件加载范围",
        )
        if fileEnd > len(userElf):
            raise OsToolError("用户 ELF 加载段越过文件末尾。")
        virtualEnd = checkedUserAddressEnd(
            virtualAddress,
            memorySizeBytes,
            "虚拟地址范围",
        )
        if (
            virtualAddress < OS_USER_ELF_MINIMUM_VIRTUAL_ADDRESS
            or virtualEnd > OS_USER_ELF_MAXIMUM_VIRTUAL_ADDRESS_EXCLUSIVE
        ):
            raise OsToolError("用户 ELF 加载段超出进程程序地址窗口。")
        for previousSegment in loadSegments:
            previousEnd = (
                previousSegment.virtualAddress
                + previousSegment.memorySizeBytes
            )
            if userRangesOverlap(
                previousSegment.virtualAddress,
                previousEnd,
                virtualAddress,
                virtualEnd,
            ):
                raise OsToolError("用户 ELF 加载段虚拟地址重叠。")
        segmentPageCount = (
            memorySizeBytes + OS_USER_ELF_PAGE_SIZE_BYTES - 1
        ) // OS_USER_ELF_PAGE_SIZE_BYTES
        if (
            segmentPageCount > OS_USER_ELF_MAXIMUM_MAPPED_PAGE_COUNT
            or mappedPageCount
            > OS_USER_ELF_MAXIMUM_MAPPED_PAGE_COUNT - segmentPageCount
        ):
            raise OsToolError("用户 ELF 加载页数超出内核能力。")
        mappedPageCount += segmentPageCount
        loadSegments.append(
            UserLoadSegment(
                fileOffset=fileOffset,
                virtualAddress=virtualAddress,
                fileSizeBytes=fileSizeBytes,
                memorySizeBytes=memorySizeBytes,
                flags=programFlags,
            )
        )

    for loadSegment in loadSegments:
        segmentEnd = (
            loadSegment.virtualAddress + loadSegment.memorySizeBytes
        )
        if (
            loadSegment.flags & OS_USER_ELF_PROGRAM_FLAG_EXECUTE
            and loadSegment.virtualAddress <= entryAddress < segmentEnd
        ):
            return entryAddress, tuple(loadSegments)
    raise OsToolError("用户 ELF 入口不在可执行加载段中。")


def auditUserElf(
    projectRoot: Path,
    userElfPath: Path,
) -> None:
    entryAddress, loadSegments = parseUserLoadSegments(
        userElfPath.read_bytes()
    )
    undefinedSymbolResult = runCommand(
        ["llvm-nm", "--undefined-only", str(userElfPath)],
        projectRoot,
        captureOutput=True,
    )
    if undefinedSymbolResult.stdout.strip():
        raise OsToolError("用户 ELF 包含未解析运行时符号。")
    definedSymbolResult = runCommand(
        [
            "llvm-nm",
            "--defined-only",
            "--format=posix",
            str(userElfPath),
        ],
        projectRoot,
        captureOutput=True,
    )
    definedSymbols = {
        symbolLine.split(maxsplit=1)[0]
        for symbolLine in definedSymbolResult.stdout.splitlines()
        if symbolLine.strip()
    }
    missingSymbols = sorted(OS_USER_ELF_REQUIRED_SYMBOLS - definedSymbols)
    if missingSymbols:
        raise OsToolError(
            "用户 ELF 缺少入口或系统调用桩："
            + ", ".join(missingSymbols)
        )
    print(
        "用户 ELF64 审计通过："
        f"入口 0x{entryAddress:016X}，"
        f"{len(loadSegments)} 个可加载段。"
    )
