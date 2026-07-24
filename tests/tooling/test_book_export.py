from pathlib import Path
import tempfile
import unittest

from tools.os_tools.book_export import (
    calculateFileSha256,
    exportBookPdf,
)
from tools.os_tools.errors import OsToolError


class BookExportToolTests(unittest.TestCase):
    def testExportsPdfIntoDedicatedDirectory(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            temporaryRoot = Path(temporaryDirectory)
            sourcePdfPath = temporaryRoot / "source.pdf"
            allowedRoot = temporaryRoot / "phone"
            destinationDirectory = allowedRoot / "原理卷" / "教材"
            sourcePdfPath.write_bytes(b"%PDF-1.7\nOS")

            destinationPdfPath = exportBookPdf(
                sourcePdfPath,
                destinationDirectory,
                "教材.pdf",
                allowedRoot,
            )

            self.assertEqual(
                destinationPdfPath,
                destinationDirectory / "教材.pdf",
            )
            self.assertEqual(
                calculateFileSha256(sourcePdfPath),
                calculateFileSha256(destinationPdfPath),
            )

    def testReplacesOnlyTheExpectedExportFile(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            temporaryRoot = Path(temporaryDirectory)
            sourcePdfPath = temporaryRoot / "source.pdf"
            allowedRoot = temporaryRoot / "phone"
            destinationDirectory = allowedRoot / "教材"
            destinationDirectory.mkdir(parents=True)
            destinationPdfPath = destinationDirectory / "教材.pdf"
            destinationPdfPath.write_bytes(b"old")
            sourcePdfPath.write_bytes(b"new")

            exportBookPdf(
                sourcePdfPath,
                destinationDirectory,
                "教材.pdf",
                allowedRoot,
            )

            self.assertEqual(destinationPdfPath.read_bytes(), b"new")

    def testRejectsDestinationOutsideAllowedRoot(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            temporaryRoot = Path(temporaryDirectory)
            sourcePdfPath = temporaryRoot / "source.pdf"
            sourcePdfPath.write_bytes(b"%PDF")

            with self.assertRaises(OsToolError):
                exportBookPdf(
                    sourcePdfPath,
                    temporaryRoot / "outside",
                    "教材.pdf",
                    temporaryRoot / "phone",
                )

    def testRejectsUnexpectedContentInDedicatedDirectory(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            temporaryRoot = Path(temporaryDirectory)
            sourcePdfPath = temporaryRoot / "source.pdf"
            allowedRoot = temporaryRoot / "phone"
            destinationDirectory = allowedRoot / "教材"
            destinationDirectory.mkdir(parents=True)
            sourcePdfPath.write_bytes(b"%PDF")
            (destinationDirectory / "其他资料.pdf").write_bytes(b"other")

            with self.assertRaises(OsToolError):
                exportBookPdf(
                    sourcePdfPath,
                    destinationDirectory,
                    "教材.pdf",
                    allowedRoot,
                )

    def testRejectsPhoneIncompatibleFileName(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            temporaryRoot = Path(temporaryDirectory)
            sourcePdfPath = temporaryRoot / "source.pdf"
            allowedRoot = temporaryRoot / "phone"
            sourcePdfPath.write_bytes(b"%PDF")

            with self.assertRaises(OsToolError):
                exportBookPdf(
                    sourcePdfPath,
                    allowedRoot / "教材",
                    "x86-64 操作系统.pdf",
                    allowedRoot,
                )


if __name__ == "__main__":
    unittest.main()
