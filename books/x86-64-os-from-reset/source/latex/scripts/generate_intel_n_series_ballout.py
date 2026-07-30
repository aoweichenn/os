#!/usr/bin/env python3
"""从 Intel N-Series 官方 Ballout 工作簿生成纯矢量 TikZ 球位数据。"""

from __future__ import annotations

import argparse
import hashlib
import re
import statistics
import xml.etree.ElementTree as ElementTree
import zipfile
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path


OS_SPREADSHEET_NAMESPACE = (
    "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
)
OS_EXPECTED_BALL_COUNT = 1264
OS_PACKAGE_SCALE_CM_PER_MICRON = 0.00034
OS_BALL_RADIUS_CM = 0.025
OS_BALL_PATTERN = re.compile(r"^([A-Z]+)([0-9]+)$")


@dataclass(frozen=True)
class Ball:
    name: str
    signal: str
    interface: str
    x_micron: float
    y_micron: float


def ParseArguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="生成 Intel N-Series BGA1264 的 TikZ 球位图。"
    )
    parser.add_argument("input", type=Path, help="759603_001_Ballout.xlsx")
    parser.add_argument("output", type=Path, help="生成的 TikZ .tex 文件")
    return parser.parse_args()


def LoadSharedStrings(archive: zipfile.ZipFile) -> list[str]:
    root = ElementTree.fromstring(archive.read("xl/sharedStrings.xml"))
    text_tag = f"{{{OS_SPREADSHEET_NAMESPACE}}}t"
    return [
        "".join(text.text or "" for text in item.iter(text_tag))
        for item in root
    ]


def ReadCell(
    cell: ElementTree.Element,
    shared_strings: list[str],
) -> tuple[str, str]:
    reference = cell.attrib["r"]
    column = "".join(character for character in reference if character.isalpha())
    value_node = cell.find(f"{{{OS_SPREADSHEET_NAMESPACE}}}v")
    value = "" if value_node is None else (value_node.text or "")
    if cell.attrib.get("t") == "s" and value:
        value = shared_strings[int(value)]
    return column, value


def LoadBalls(input_path: Path) -> list[Ball]:
    with zipfile.ZipFile(input_path) as archive:
        shared_strings = LoadSharedStrings(archive)
        root = ElementTree.fromstring(
            archive.read("xl/worksheets/sheet3.xml")
        )

    row_tag = (
        f".//{{{OS_SPREADSHEET_NAMESPACE}}}sheetData/"
        f"{{{OS_SPREADSHEET_NAMESPACE}}}row"
    )
    cell_tag = f"{{{OS_SPREADSHEET_NAMESPACE}}}c"
    balls: list[Ball] = []
    for row in root.findall(row_tag)[1:]:
        values = dict(
            ReadCell(cell, shared_strings) for cell in row.findall(cell_tag)
        )
        if not all(values.get(column) for column in ("A", "D", "E")):
            continue
        balls.append(
            Ball(
                name=values["A"],
                signal=values.get("B", ""),
                interface=values.get("F", "").strip(),
                x_micron=float(values["D"]),
                y_micron=float(values["E"]),
            )
        )

    if len(balls) != OS_EXPECTED_BALL_COUNT:
        raise ValueError(
            f"预期 {OS_EXPECTED_BALL_COUNT} 个球位，实际读取 {len(balls)} 个"
        )
    return balls


def CategoryForInterface(interface: str) -> tuple[str, str]:
    if interface in {"Power", "CPU Power", "PCH Power"}:
        return "power", "BookRed"
    if interface == "DDR Interface":
        return "ddr", "BookBlue"
    if interface in {
        "PCH Interfaces",
        "Display",
        "PCH Display",
        "Type C",
        "Camera",
    }:
        return "io", "BookTeal"
    if interface in {"PCH GPIOs", "Legacy/Sidebend", "Sideband"}:
        return "gpio", "BookAmber"
    if interface in {
        "Test Point",
        "CPU Debug",
        "PCH Debug",
    }:
        return "debug", "BookNavy"
    return "other", "BookMuted"


def FormatCoordinate(micron: float) -> str:
    return f"{micron * OS_PACKAGE_SCALE_CM_PER_MICRON:.4f}"


def BuildAxisPositions(
    balls: list[Ball],
) -> tuple[dict[str, float], dict[int, float]]:
    row_values: dict[str, list[float]] = defaultdict(list)
    column_values: dict[int, list[float]] = defaultdict(list)
    for ball in balls:
        match = OS_BALL_PATTERN.fullmatch(ball.name)
        if match is None:
            raise ValueError(f"无法解析球位编号：{ball.name}")
        row_name, column_text = match.groups()
        row_values[row_name].append(ball.y_micron)
        column_values[int(column_text)].append(ball.x_micron)

    rows = {
        name: statistics.median(values) for name, values in row_values.items()
    }
    columns = {
        number: statistics.median(values)
        for number, values in column_values.items()
    }
    return rows, columns


def RenderTikz(
    balls: list[Ball],
    source_hash: str,
) -> str:
    rows, columns = BuildAxisPositions(balls)
    category_counts = Counter(
        CategoryForInterface(ball.interface)[0] for ball in balls
    )
    expected_counts = {
        "power": 601,
        "ddr": 136,
        "io": 175,
        "gpio": 192,
        "debug": 81,
        "other": 79,
    }
    if dict(category_counts) != expected_counts:
        raise ValueError(
            f"接口分类计数发生变化：{dict(category_counts)}"
        )

    lines = [
        "% 由 scripts/generate_intel_n_series_ballout.py 生成；不要手工编辑。",
        "% 来源：Intel 文档 759603，附件 759603_001_Ballout.xlsx。",
        f"% 输入 SHA-256：{source_hash}",
        f"% 球位数量：{len(balls)}",
    ]
    for ball in sorted(balls, key=lambda item: (item.y_micron, item.x_micron)):
        _, color = CategoryForInterface(ball.interface)
        lines.append(
            f"\\fill[{color}] "
            f"({FormatCoordinate(ball.x_micron)},"
            f"{FormatCoordinate(ball.y_micron)}) "
            f"circle[radius={OS_BALL_RADIUS_CM:.3f}cm];"
        )

    axis_font = r"\fontsize{3.1}{3.3}\selectfont\sffamily"
    for row_name, y_micron in sorted(
        rows.items(),
        key=lambda item: item[1],
    ):
        y = FormatCoordinate(y_micron)
        lines.append(
            f"\\node[font={{{axis_font}}},anchor=east] "
            f"at (-4.20,{y}) {{{row_name}}};"
        )
        lines.append(
            f"\\node[font={{{axis_font}}},anchor=west] "
            f"at (4.20,{y}) {{{row_name}}};"
        )

    for column_number, x_micron in sorted(columns.items()):
        x = FormatCoordinate(x_micron)
        lines.append(
            f"\\node[font={{{axis_font}}},anchor=west,rotate=90] "
            f"at ({x},6.08) {{{column_number}}};"
        )
        lines.append(
            f"\\node[font={{{axis_font}}},anchor=east,rotate=90] "
            f"at ({x},-6.08) {{{column_number}}};"
        )
    return "\n".join(lines) + "\n"


def Main() -> int:
    arguments = ParseArguments()
    input_bytes = arguments.input.read_bytes()
    source_hash = hashlib.sha256(input_bytes).hexdigest()
    balls = LoadBalls(arguments.input)
    output = RenderTikz(balls, source_hash)
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(output, encoding="utf-8")
    print(
        f"已生成 {arguments.output}：{len(balls)} 个球位，"
        f"输入 SHA-256={source_hash}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(Main())
