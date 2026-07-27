from pathlib import Path
import re
import sys
import tempfile
import threading
import time
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
    createQemuFirmwareCommand,
    normalizeCapturedOutput,
    qemuFirmwareTimeoutSeconds,
    qemuKeyNameForCharacter,
    runQemuWithTimedSerial,
    validateImageSize,
    validateSerialProtocol,
)


class QemuRunnerToolTests(unittest.TestCase):
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

    def testCreatesPrimary64GibMemoryCommand(self) -> None:
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
            qmpSocketPath,
        )

        self.assertIn("-qmp", command)
        self.assertIn(
            f"unix:{qmpSocketPath},server=on,wait=off",
            command,
        )

    def testMapsShellCharactersToQemuKeys(self) -> None:
        self.assertEqual(qemuKeyNameForCharacter("a"), "a")
        self.assertEqual(qemuKeyNameForCharacter("7"), "7")
        self.assertEqual(qemuKeyNameForCharacter(" "), "spc")
        self.assertEqual(qemuKeyNameForCharacter("/"), "slash")
        self.assertEqual(qemuKeyNameForCharacter("<"), "shift-comma")
        self.assertEqual(qemuKeyNameForCharacter(">"), "shift-dot")
        self.assertEqual(qemuKeyNameForCharacter("|"), "shift-backslash")
        self.assertEqual(qemuKeyNameForCharacter("\n"), "ret")

    def testRejectsUnsupportedQemuKeyCharacter(self) -> None:
        with self.assertRaises(OsToolError):
            qemuKeyNameForCharacter("中")

    def testAcceptsRequiredAndAbsentForbiddenMarkers(self) -> None:
        validateSerialProtocol(
            "[OS][FIRMWARE] RESET\r\n",
            ("[OS][FIRMWARE] RESET",),
            ("[OS][FIRMWARE] SERIAL_READY",),
        )

    def testRejectsMissingRequiredMarker(self) -> None:
        with self.assertRaises(OsToolError):
            validateSerialProtocol(
                "",
                ("[OS][FIRMWARE] RESET",),
                (),
            )

    def testRejectsForbiddenMarker(self) -> None:
        with self.assertRaises(OsToolError):
            validateSerialProtocol(
                "[OS][FIRMWARE] SERIAL_READY",
                (),
                ("[OS][FIRMWARE] SERIAL_READY",),
            )

    def testReportsForbiddenMarkerBeforeMissingCompletion(self) -> None:
        with self.assertRaisesRegex(OsToolError, "禁止标记"):
            validateSerialProtocol(
                "[OS][KERNEL] MEMORY_INITIALIZATION_FAILED",
                ("[OS][KERNEL] READY",),
                ("[OS][KERNEL] MEMORY_INITIALIZATION_FAILED",),
            )

    def testRejectsRequiredMarkersInWrongOrder(self) -> None:
        with self.assertRaises(OsToolError):
            validateSerialProtocol(
                "[OS][STAGE1] ENTERED\r\n"
                "[OS][FIRMWARE] STAGE1_LOADED\r\n",
                (
                    "[OS][FIRMWARE] STAGE1_LOADED",
                    "[OS][STAGE1] ENTERED",
                ),
                (),
            )

    def testAcceptsExactMarkerCountsAndHexMinimumValues(self) -> None:
        validateSerialProtocol(
            "WORKER\r\nWORKER\r\nPREEMPTIONS=0x0000000000000004\r\n",
            (),
            (),
            (("WORKER", 2),),
            (("PREEMPTIONS=0x", 1),),
        )

    def testRejectsUnexpectedMarkerCount(self) -> None:
        with self.assertRaises(OsToolError):
            validateSerialProtocol(
                "WORKER\r\n",
                (),
                (),
                (("WORKER", 2),),
            )

    def testRejectsHexValueBelowMinimum(self) -> None:
        with self.assertRaises(OsToolError):
            validateSerialProtocol(
                "PREEMPTIONS=0x0000000000000000\r\n",
                (),
                (),
                (),
                (("PREEMPTIONS=0x", 1),),
            )

    def testNormalizesByteOutput(self) -> None:
        self.assertEqual(
            normalizeCapturedOutput(b"serial"),
            "serial",
        )

    def testCapturesIndependentElapsedTimeForEachSerialLine(self) -> None:
        command = [
            sys.executable,
            "-c",
            (
                "import time; "
                "print('FIRST', flush=True); "
                "time.sleep(0.05); "
                "print('SECOND', flush=True)"
            ),
        ]

        serialOutput, timedOutput, timedOut, completedByObserver, returnCode = (
            runQemuWithTimedSerial(
                command,
                Path.cwd(),
                1.0,
            )
        )

        self.assertEqual(serialOutput, "FIRST\nSECOND\n")
        self.assertFalse(timedOut)
        self.assertFalse(completedByObserver)
        self.assertEqual(returnCode, 0)
        timestamps = [
            int(timestamp)
            for timestamp in re.findall(r"T\+(\d{6})ms", timedOutput)
        ]
        self.assertEqual(len(timestamps), 2)
        self.assertLess(timestamps[0], timestamps[1])

    def testNotifiesObserverForEachSerialLine(self) -> None:
        observedLines: list[str] = []

        serialOutput, _timedOutput, timedOut, completedByObserver, returnCode = (
            runQemuWithTimedSerial(
                [sys.executable, "-c", "print('READY', flush=True)"],
                Path.cwd(),
                1.0,
                observedLines.append,
            )
        )

        self.assertEqual(serialOutput, "READY\n")
        self.assertEqual(observedLines, ["READY\n"])
        self.assertFalse(timedOut)
        self.assertFalse(completedByObserver)
        self.assertEqual(returnCode, 0)

    def testStopsCaptureAfterObservedCompletion(self) -> None:
        completionEvent = threading.Event()

        def observeLine(line: str) -> None:
            if "READY" in line:
                completionEvent.set()

        serialOutput, _timedOutput, timedOut, completedByObserver, _returnCode = (
            runQemuWithTimedSerial(
                [
                    sys.executable,
                    "-c",
                    (
                        "import time; "
                        "print('READY', flush=True); "
                        "time.sleep(10.0)"
                    ),
                ],
                Path.cwd(),
                1.0,
                observeLine,
                completionEvent,
            )
        )

        self.assertEqual(serialOutput, "READY\n")
        self.assertFalse(timedOut)
        self.assertTrue(completedByObserver)

    def testStopsProcessAfterBoundedTimeout(self) -> None:
        startTime = time.monotonic()

        _serialOutput, _timedOutput, timedOut, completedByObserver, _returnCode = (
            runQemuWithTimedSerial(
                [
                    sys.executable,
                    "-c",
                    "import time; time.sleep(10.0)",
                ],
                Path.cwd(),
                0.05,
            )
        )
        elapsedSeconds = time.monotonic() - startTime

        self.assertTrue(timedOut)
        self.assertFalse(completedByObserver)
        self.assertLess(elapsedSeconds, 1.0)


if __name__ == "__main__":
    unittest.main()
