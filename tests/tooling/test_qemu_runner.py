from pathlib import Path
import tempfile
import unittest

from tools.os_tools.errors import OsToolError
from tools.os_tools.qemu_runner import (
    OS_QEMU_DEFAULT_CPU_MODEL,
    OS_QEMU_FIRMWARE_TIMEOUT_SECONDS,
    OS_QEMU_FUNCTIONAL_FIRMWARE_TIMEOUT_SECONDS,
    OS_QEMU_FUNCTIONAL_GUEST_MEMORY_MEBIBYTES,
    OS_QEMU_MINIMUM_GUEST_MEMORY_MEBIBYTES,
    OS_QEMU_PRIMARY_FIRMWARE_TIMEOUT_SECONDS,
    OS_QEMU_PRIMARY_GUEST_MEMORY_MEBIBYTES,
    OS_QEMU_VGA_TRACE_BYTES_OFFSET,
    OS_QEMU_VGA_TRACE_LENGTH_OFFSET,
    OS_QEMU_VGA_TRACE_MAGIC,
    OS_QEMU_VGA_TRACE_OVERFLOW_OFFSET,
    OS_QEMU_VGA_TRACE_REGION_SIZE_BYTES,
    OS_QEMU_VGA_TRACE_VERSION,
    OS_QEMU_VGA_TRACE_VERSION_OFFSET,
    OS_QEMU_VGA_DISPLAY_MINIMUM_LIT_PIXEL_COUNT,
    OS_QEMU_VGA_TEXT_COLUMN_COUNT,
    OS_QEMU_VGA_TEXT_ROW_COUNT,
    OS_QEMU_VNC_LOOPBACK_ADDRESS,
    createQemuFirmwareCommand,
    decodeVgaTraceSnapshot,
    decodeVgaTextSnapshot,
    qemuVncDisplayBackend,
    qemuFirmwareTimeoutSeconds,
    qemuKeyNameForCharacter,
    requiredVgaTraceSnapshotSize,
    roundedVgaTraceSnapshotSize,
    validateImageSize,
    validateVgaDisplaySnapshot,
    validateVgaTerminalSnapshot,
    validateVgaProtocol,
)


class QemuRunnerToolTests(unittest.TestCase):
    def createVgaTextSnapshot(self, lines: tuple[str, ...]) -> bytes:
        snapshot = bytearray(
            OS_QEMU_VGA_TEXT_COLUMN_COUNT * OS_QEMU_VGA_TEXT_ROW_COUNT * 2
        )
        for cellIndex in range(
            OS_QEMU_VGA_TEXT_COLUMN_COUNT * OS_QEMU_VGA_TEXT_ROW_COUNT
        ):
            snapshot[cellIndex * 2] = ord(" ")
            snapshot[cellIndex * 2 + 1] = 0x07
        for rowIndex, line in enumerate(lines):
            for columnIndex, character in enumerate(
                line[:OS_QEMU_VGA_TEXT_COLUMN_COUNT]
            ):
                cellIndex = rowIndex * OS_QEMU_VGA_TEXT_COLUMN_COUNT + columnIndex
                snapshot[cellIndex * 2] = ord(character)
        return bytes(snapshot)

    def testAcceptsCleanVgaUserTerminalSnapshot(self) -> None:
        snapshot = self.createVgaTextSnapshot(("OS v2.1", "[os:/]$ ",))

        validateVgaTerminalSnapshot(snapshot)
        self.assertIn("OS v2.1", decodeVgaTextSnapshot(snapshot))

    def testRejectsKernelDiagnosticsOnVgaUserTerminal(self) -> None:
        snapshot = self.createVgaTextSnapshot(("[OS][KERNEL] READY",))

        with self.assertRaisesRegex(OsToolError, "仍包含启动诊断"):
            validateVgaTerminalSnapshot(snapshot)

    def testAcceptsExpectedImageSize(self) -> None:
        expectedSizeBytes = 64

        with tempfile.TemporaryDirectory() as temporaryDirectory:
            imagePath = Path(temporaryDirectory) / "image.bin"
            imagePath.write_bytes(bytes(expectedSizeBytes))

            validateImageSize(
                imagePath,
                expectedSizeBytes,
                "测试镜像",
            )

    def testRejectsUnexpectedImageSize(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            imagePath = Path(temporaryDirectory) / "image.bin"
            imagePath.write_bytes(bytes(32))

            with self.assertRaises(OsToolError):
                validateImageSize(
                    imagePath,
                    64,
                    "测试镜像",
                )

    def testCreatesArchitectureIndependentTcgCommand(self) -> None:
        command = createQemuFirmwareCommand(
            Path("firmware.bin"),
            Path("disk.img"),
        )

        self.assertIn("pc,accel=tcg", command)
        self.assertIn(OS_QEMU_DEFAULT_CPU_MODEL, command)
        memoryOptionIndex = command.index("-m")
        self.assertEqual(
            command[memoryOptionIndex + 1],
            str(OS_QEMU_MINIMUM_GUEST_MEMORY_MEBIBYTES),
        )
        self.assertIn("firmware.bin", command)
        self.assertIn(
            "file=disk.img,format=raw,if=ide,snapshot=on",
            command,
        )
        self.assertIn("VGA", command)
        serialOptionIndex = command.index("-serial")
        self.assertEqual(command[serialOptionIndex + 1], "none")
        self.assertNotIn("-kernel", command)

    def testCreatesExplicitCpuFeatureFailureCommand(self) -> None:
        cpuModel = "qemu64,-sse2"
        command = createQemuFirmwareCommand(
            Path("firmware.bin"),
            Path("disk.img"),
            cpuModel=cpuModel,
        )

        cpuOptionIndex = command.index("-cpu")
        self.assertEqual(command[cpuOptionIndex + 1], cpuModel)

    def testCreatesPrimary32GibMemoryCommand(self) -> None:
        self.assertEqual(OS_QEMU_PRIMARY_GUEST_MEMORY_MEBIBYTES, 32 * 1024)
        command = createQemuFirmwareCommand(
            Path("firmware.bin"),
            Path("disk.img"),
            memoryMebibytes=OS_QEMU_PRIMARY_GUEST_MEMORY_MEBIBYTES,
        )

        memoryOptionIndex = command.index("-m")
        self.assertEqual(
            command[memoryOptionIndex + 1],
            str(OS_QEMU_PRIMARY_GUEST_MEMORY_MEBIBYTES),
        )

    def testCreatesFunctional256MibMemoryCommand(self) -> None:
        command = createQemuFirmwareCommand(
            Path("firmware.bin"),
            Path("disk.img"),
            memoryMebibytes=OS_QEMU_FUNCTIONAL_GUEST_MEMORY_MEBIBYTES,
        )

        memoryOptionIndex = command.index("-m")
        self.assertEqual(
            command[memoryOptionIndex + 1],
            str(OS_QEMU_FUNCTIONAL_GUEST_MEMORY_MEBIBYTES),
        )

    def testSelectsBoundedTimeoutByMemoryProfile(self) -> None:
        self.assertEqual(
            qemuFirmwareTimeoutSeconds(
                OS_QEMU_MINIMUM_GUEST_MEMORY_MEBIBYTES
            ),
            OS_QEMU_FIRMWARE_TIMEOUT_SECONDS,
        )
        self.assertEqual(
            qemuFirmwareTimeoutSeconds(
                OS_QEMU_FUNCTIONAL_GUEST_MEMORY_MEBIBYTES - 1
            ),
            OS_QEMU_FIRMWARE_TIMEOUT_SECONDS,
        )
        self.assertEqual(
            qemuFirmwareTimeoutSeconds(
                OS_QEMU_FUNCTIONAL_GUEST_MEMORY_MEBIBYTES
            ),
            OS_QEMU_FUNCTIONAL_FIRMWARE_TIMEOUT_SECONDS,
        )
        self.assertEqual(
            qemuFirmwareTimeoutSeconds(
                OS_QEMU_PRIMARY_GUEST_MEMORY_MEBIBYTES - 1
            ),
            OS_QEMU_FUNCTIONAL_FIRMWARE_TIMEOUT_SECONDS,
        )
        self.assertEqual(
            qemuFirmwareTimeoutSeconds(
                OS_QEMU_PRIMARY_GUEST_MEMORY_MEBIBYTES
            ),
            OS_QEMU_PRIMARY_FIRMWARE_TIMEOUT_SECONDS,
        )
        self.assertGreater(
            OS_QEMU_PRIMARY_FIRMWARE_TIMEOUT_SECONDS,
            OS_QEMU_FUNCTIONAL_FIRMWARE_TIMEOUT_SECONDS,
        )
        self.assertGreater(
            OS_QEMU_FUNCTIONAL_FIRMWARE_TIMEOUT_SECONDS,
            OS_QEMU_FIRMWARE_TIMEOUT_SECONDS,
        )

    def testRejectsMemoryBelowBootstrapMinimum(self) -> None:
        with self.assertRaises(OsToolError):
            createQemuFirmwareCommand(
                Path("firmware.bin"),
                Path("disk.img"),
                memoryMebibytes=(
                    OS_QEMU_MINIMUM_GUEST_MEMORY_MEBIBYTES - 1
                ),
            )

    def testCreatesQmpSocketForKeyboardInjection(self) -> None:
        qmpSocketPath = Path("/tmp/os-qemu-test.sock")
        command = createQemuFirmwareCommand(
            Path("firmware.bin"),
            Path("disk.img"),
            (qmpSocketPath,),
        )

        self.assertIn("-qmp", command)
        self.assertIn(
            f"unix:{qmpSocketPath},server=on,wait=off",
            command,
        )

    def testCreatesIndependentVgaTraceAndKeyboardQmpSockets(self) -> None:
        traceSocketPath = Path("/tmp/os-qemu-vga-trace.sock")
        keyboardSocketPath = Path("/tmp/os-qemu-keyboard.sock")
        command = createQemuFirmwareCommand(
            Path("firmware.bin"),
            Path("disk.img"),
            (traceSocketPath, keyboardSocketPath),
            displayBackend="curses",
        )

        self.assertEqual(command.count("-qmp"), 2)
        self.assertIn(
            f"unix:{traceSocketPath},server=on,wait=off",
            command,
        )
        self.assertIn(
            f"unix:{keyboardSocketPath},server=on,wait=off",
            command,
        )
        displayOptionIndex = command.index("-display")
        self.assertEqual(command[displayOptionIndex + 1], "curses")

    def testCreatesLoopbackOnlyVncDisplayBackend(self) -> None:
        self.assertEqual(
            qemuVncDisplayBackend(1),
            (
                f"vnc={OS_QEMU_VNC_LOOPBACK_ADDRESS}:1,"
                "share=force-shared,connections=8,lossy=off"
            ),
        )

    def testRejectsOutOfRangeVncDisplayNumber(self) -> None:
        with self.assertRaises(OsToolError):
            qemuVncDisplayBackend(-1)
        with self.assertRaises(OsToolError):
            qemuVncDisplayBackend(100)

    def testMapsShellCharactersToQemuKeys(self) -> None:
        self.assertEqual(qemuKeyNameForCharacter("a"), "a")
        self.assertEqual(qemuKeyNameForCharacter("7"), "7")
        self.assertEqual(qemuKeyNameForCharacter(" "), "spc")
        self.assertEqual(qemuKeyNameForCharacter("/"), "slash")
        self.assertEqual(qemuKeyNameForCharacter("&"), "shift-7")
        self.assertEqual(qemuKeyNameForCharacter("<"), "shift-comma")
        self.assertEqual(qemuKeyNameForCharacter(">"), "shift-dot")
        self.assertEqual(qemuKeyNameForCharacter("|"), "shift-backslash")
        self.assertEqual(qemuKeyNameForCharacter("\x03"), "ctrl-c")
        self.assertEqual(qemuKeyNameForCharacter("\x1a"), "ctrl-z")
        self.assertEqual(qemuKeyNameForCharacter("\n"), "ret")

    def testRejectsUnsupportedQemuKeyCharacter(self) -> None:
        with self.assertRaises(OsToolError):
            qemuKeyNameForCharacter("中")

    def testAcceptsRequiredAndAbsentForbiddenMarkers(self) -> None:
        validateVgaProtocol(
            "[OS][FIRMWARE] RESET\r\n",
            ("[OS][FIRMWARE] RESET",),
            ("[OS][FIRMWARE] VGA_READY",),
        )

    def testRejectsMissingRequiredMarker(self) -> None:
        with self.assertRaises(OsToolError):
            validateVgaProtocol(
                "",
                ("[OS][FIRMWARE] RESET",),
                (),
            )

    def testRejectsForbiddenMarker(self) -> None:
        with self.assertRaises(OsToolError):
            validateVgaProtocol(
                "[OS][FIRMWARE] VGA_READY",
                (),
                ("[OS][FIRMWARE] VGA_READY",),
            )

    def testReportsForbiddenMarkerBeforeMissingCompletion(self) -> None:
        with self.assertRaisesRegex(OsToolError, "禁止标记"):
            validateVgaProtocol(
                "[OS][KERNEL] MEMORY_INITIALIZATION_FAILED",
                ("[OS][KERNEL] READY",),
                ("[OS][KERNEL] MEMORY_INITIALIZATION_FAILED",),
            )

    def testRejectsRequiredMarkersInWrongOrder(self) -> None:
        with self.assertRaises(OsToolError):
            validateVgaProtocol(
                "[OS][STAGE1] ENTERED\r\n"
                "[OS][FIRMWARE] STAGE1_LOADED\r\n",
                (
                    "[OS][FIRMWARE] STAGE1_LOADED",
                    "[OS][STAGE1] ENTERED",
                ),
                (),
            )

    def testAcceptsExactMarkerCountsAndHexMinimumValues(self) -> None:
        validateVgaProtocol(
            "WORKER\r\nWORKER\r\nPREEMPTIONS=0x0000000000000004\r\n",
            (),
            (),
            (("WORKER", 2),),
            (("PREEMPTIONS=0x", 1),),
        )

    def testRejectsUnexpectedMarkerCount(self) -> None:
        with self.assertRaises(OsToolError):
            validateVgaProtocol(
                "WORKER\r\n",
                (),
                (),
                (("WORKER", 2),),
            )

    def testRejectsHexValueBelowMinimum(self) -> None:
        with self.assertRaises(OsToolError):
            validateVgaProtocol(
                "PREEMPTIONS=0x0000000000000000\r\n",
                (),
                (),
                (),
                (("PREEMPTIONS=0x", 1),),
            )

    def createVgaTraceSnapshot(
        self,
        output: bytes,
        *,
        version: int = OS_QEMU_VGA_TRACE_VERSION,
        overflow: int = 0,
    ) -> bytes:
        snapshot = bytearray(OS_QEMU_VGA_TRACE_REGION_SIZE_BYTES)
        snapshot[: len(OS_QEMU_VGA_TRACE_MAGIC)] = OS_QEMU_VGA_TRACE_MAGIC
        snapshot[
            OS_QEMU_VGA_TRACE_VERSION_OFFSET:
            OS_QEMU_VGA_TRACE_VERSION_OFFSET + 4
        ] = version.to_bytes(4, "little")
        snapshot[
            OS_QEMU_VGA_TRACE_LENGTH_OFFSET:
            OS_QEMU_VGA_TRACE_LENGTH_OFFSET + 4
        ] = len(output).to_bytes(4, "little")
        snapshot[
            OS_QEMU_VGA_TRACE_OVERFLOW_OFFSET:
            OS_QEMU_VGA_TRACE_OVERFLOW_OFFSET + 4
        ] = overflow.to_bytes(4, "little")
        snapshot[
            OS_QEMU_VGA_TRACE_BYTES_OFFSET:
            OS_QEMU_VGA_TRACE_BYTES_OFFSET + len(output)
        ] = output
        return bytes(snapshot)

    def testDecodesVgaTraceSnapshot(self) -> None:
        self.assertEqual(
            decodeVgaTraceSnapshot(
                self.createVgaTraceSnapshot(b"RESET\r\nVGA_READY\r\n")
            ),
            "RESET\r\nVGA_READY\r\n",
        )

    def testDecodesCoveredPartialVgaTraceSnapshot(self) -> None:
        output = b"RESET\r\n"
        snapshot = self.createVgaTraceSnapshot(output)
        requiredSizeBytes = OS_QEMU_VGA_TRACE_BYTES_OFFSET + len(output)
        self.assertEqual(
            decodeVgaTraceSnapshot(snapshot[:requiredSizeBytes]),
            output.decode("ascii"),
        )

    def testRequestsLargerVgaTraceSnapshotWithoutDecodingTruncation(self) -> None:
        output = b"A" * 20000
        snapshot = self.createVgaTraceSnapshot(output)
        partialSnapshot = snapshot[:16384]
        requiredSizeBytes = requiredVgaTraceSnapshotSize(partialSnapshot)
        self.assertEqual(
            requiredSizeBytes,
            OS_QEMU_VGA_TRACE_BYTES_OFFSET + len(output),
        )
        self.assertGreater(
            roundedVgaTraceSnapshotSize(requiredSizeBytes),
            len(partialSnapshot),
        )
        with self.assertRaisesRegex(OsToolError, "没有覆盖"):
            decodeVgaTraceSnapshot(partialSnapshot)

    def testTreatsUninitializedVgaTraceAsEmpty(self) -> None:
        self.assertEqual(
            decodeVgaTraceSnapshot(bytes(OS_QEMU_VGA_TRACE_REGION_SIZE_BYTES)),
            "",
        )

    def testRejectsUnsupportedVgaTraceVersion(self) -> None:
        with self.assertRaises(OsToolError):
            decodeVgaTraceSnapshot(
                self.createVgaTraceSnapshot(b"", version=OS_QEMU_VGA_TRACE_VERSION + 1)
            )

    def testRejectsVgaTraceOverflow(self) -> None:
        with self.assertRaisesRegex(OsToolError, "溢出"):
            decodeVgaTraceSnapshot(self.createVgaTraceSnapshot(b"READY\n", overflow=1))

    def testAcceptsVisibleVgaDisplaySnapshot(self) -> None:
        width = 32
        height = 32
        litPixels = bytes((0xA8, 0xA8, 0xA8)) * (
            OS_QEMU_VGA_DISPLAY_MINIMUM_LIT_PIXEL_COUNT
        )
        blackPixels = bytes(3) * (
            width * height - OS_QEMU_VGA_DISPLAY_MINIMUM_LIT_PIXEL_COUNT
        )
        snapshot = f"P6\n{width} {height}\n255\n".encode("ascii") + litPixels + blackPixels

        validateVgaDisplaySnapshot(snapshot)

    def testRejectsBlackVgaDisplaySnapshot(self) -> None:
        snapshot = b"P6\n32 32\n255\n" + bytes(32 * 32 * 3)

        with self.assertRaisesRegex(OsToolError, "全黑"):
            validateVgaDisplaySnapshot(snapshot)

    def testRejectsMalformedVgaDisplaySnapshot(self) -> None:
        with self.assertRaisesRegex(OsToolError, "PPM"):
            validateVgaDisplaySnapshot(b"not-a-ppm")


if __name__ == "__main__":
    unittest.main()
