from pathlib import Path

from tools.os_tools.errors import OsToolError


OS_KERNEL_LAYOUT_MODULE_NAMES = frozenset(
    {
        "arch",
        "boot",
        "core",
        "device",
        "fs",
        "io",
        "ipc",
        "memory",
        "object",
        "process",
        "security",
        "sync",
        "time",
        "user",
    }
)
OS_KERNEL_LAYOUT_EXTRA_IMPLEMENTATION_PATHS = frozenset(
    {
        Path("arch/architecture.asm"),
        Path("memory/page_table_layout.cpp"),
        Path("user/user_images.asm.in"),
    }
)
OS_KERNEL_LAYOUT_PUBLIC_HEADER_SUFFIX = ".hpp"
OS_KERNEL_LAYOUT_TEMPLATE_SUFFIX = ".tpp"
OS_KERNEL_LAYOUT_SOURCE_SUFFIX = ".cpp"


def validateKernelSourceLayout(projectRoot: Path) -> None:
    """验证 Kernel 功能目录、头源对称关系和已记录的额外实现。"""
    includeRoot = projectRoot / "source/kernel/include/os/kernel"
    sourceRoot = projectRoot / "source/kernel/src"
    if not includeRoot.is_dir() or not sourceRoot.is_dir():
        raise OsToolError("Kernel 头文件或源文件根目录不存在。")

    includeModuleNames = {
        path.name for path in includeRoot.iterdir() if path.is_dir()
    }
    sourceModuleNames = {
        path.name for path in sourceRoot.iterdir() if path.is_dir()
    }
    if includeModuleNames != OS_KERNEL_LAYOUT_MODULE_NAMES:
        raise OsToolError(
            "Kernel 头文件模块集合不匹配："
            f"{sorted(includeModuleNames)}"
        )
    if sourceModuleNames != OS_KERNEL_LAYOUT_MODULE_NAMES:
        raise OsToolError(
            "Kernel 源文件模块集合不匹配："
            f"{sorted(sourceModuleNames)}"
        )

    flatIncludeFiles = sorted(
        path.name
        for path in includeRoot.iterdir()
        if path.is_file()
        and path.suffix
        in {
            OS_KERNEL_LAYOUT_PUBLIC_HEADER_SUFFIX,
            OS_KERNEL_LAYOUT_TEMPLATE_SUFFIX,
        }
    )
    flatSourceFiles = sorted(
        path.name
        for path in sourceRoot.iterdir()
        if path.is_file()
        and (
            path.suffix
            in {
                OS_KERNEL_LAYOUT_SOURCE_SUFFIX,
                ".asm",
            }
            or path.name.endswith(".asm.in")
        )
    )
    if flatIncludeFiles or flatSourceFiles:
        raise OsToolError(
            "Kernel 根目录不允许保存实现文件："
            f"include={flatIncludeFiles}, src={flatSourceFiles}"
        )

    publicHeaders = sorted(includeRoot.glob("*/*.hpp"))
    for headerPath in publicHeaders:
        relativeHeaderPath = headerPath.relative_to(includeRoot)
        expectedSourcePath = (
            sourceRoot
            / relativeHeaderPath.with_suffix(
                OS_KERNEL_LAYOUT_SOURCE_SUFFIX
            )
        )
        if not expectedSourcePath.is_file():
            raise OsToolError(
                "Kernel 公开头文件缺少同模块实现："
                f"{relativeHeaderPath.as_posix()}"
            )

    for templatePath in sorted(includeRoot.glob("*/*.tpp")):
        expectedHeaderPath = templatePath.with_suffix(
            OS_KERNEL_LAYOUT_PUBLIC_HEADER_SUFFIX
        )
        if not expectedHeaderPath.is_file():
            raise OsToolError(
                "Kernel 模板实现缺少同名公开头文件："
                f"{templatePath.relative_to(includeRoot).as_posix()}"
            )

    publicHeaderSources = {
        headerPath.relative_to(includeRoot).with_suffix(
            OS_KERNEL_LAYOUT_SOURCE_SUFFIX
        )
        for headerPath in publicHeaders
    }
    actualExtraImplementationPaths = {
        sourcePath.relative_to(sourceRoot)
        for sourcePath in sourceRoot.glob("*/*")
        if sourcePath.is_file()
        and (
            sourcePath.suffix in {OS_KERNEL_LAYOUT_SOURCE_SUFFIX, ".asm"}
            or sourcePath.name.endswith(".asm.in")
        )
        and sourcePath.relative_to(sourceRoot) not in publicHeaderSources
    }
    if (
        actualExtraImplementationPaths
        != OS_KERNEL_LAYOUT_EXTRA_IMPLEMENTATION_PATHS
    ):
        raise OsToolError(
            "Kernel 非配对实现集合不匹配："
            f"{sorted(path.as_posix() for path in actualExtraImplementationPaths)}"
        )
