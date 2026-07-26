#!/usr/bin/env python3

"""检查系统图的卡片净距和实体电路图的逐引脚连通性。"""

import argparse
from pathlib import Path
import re
import sys
from xml.etree import ElementTree


OS_DIAGRAM_FILE_NAMES = (
    "x86_64_os_hardware_wiring.svg",
    "hardware_stage_roadmap.svg",
    "boot_and_memory_wiring.svg",
    "port_io_topology.svg",
    "interrupt_routing.svg",
    "keyboard_to_shell.svg",
    "storage_persistence.svg",
)
OS_ELECTRICAL_DIAGRAM_FILE_NAMES = (
    "physical_carrier/power_wiring.svg",
    "physical_carrier/high_speed_wiring.svg",
    "physical_carrier/control_wiring.svg",
)
OS_DIAGRAM_CONNECTOR_CLASSES = frozenset(
    {
        "memory",
        "memory-bi",
        "io",
        "io-bi",
        "irq",
        "host",
        "flow",
        "phase-arrow",
        "io-left",
        "io-right",
        "host-flow",
        "kernel-flow",
        "user-flow",
        "disk-flow",
        "verify-flow",
    }
)
OS_DIAGRAM_CLEARANCE_PIXELS = 8.0
OS_DIAGRAM_NUMBER_OR_COMMAND = re.compile(
    r"[MLHV]|-?(?:\d+(?:\.\d*)?|\.\d+)"
)
OS_DIAGRAM_MARKER_REFERENCE = re.compile(
    r"marker-(?:start|mid|end)\s*:\s*url\(#([^)]+)\)"
)
OS_ELECTRICAL_WIRE_CLASS = "schematic-wire"
OS_ELECTRICAL_PIN_CLASSES = frozenset({"pin", "no-connect"})
OS_ELECTRICAL_FORBIDDEN_PLACEHOLDERS = ("...", "…")


def ParsePathSegments(path_data: str):
    """把只含绝对 M/L/H/V 的正交路径展开成线段。"""

    tokens = OS_DIAGRAM_NUMBER_OR_COMMAND.findall(path_data.replace(",", " "))
    token_index = 0
    command = None
    current_point = None

    while token_index < len(tokens):
        token = tokens[token_index]
        if token in {"M", "L", "H", "V"}:
            command = token
            token_index += 1

        if command in {"M", "L"}:
            x_coordinate = float(tokens[token_index])
            y_coordinate = float(tokens[token_index + 1])
            token_index += 2
            next_point = (x_coordinate, y_coordinate)
            if command == "L" and current_point is not None:
                yield current_point, next_point
            current_point = next_point
            if command == "M":
                command = "L"
        elif command == "H":
            if current_point is None:
                raise ValueError("H command appears before M")
            x_coordinate = float(tokens[token_index])
            token_index += 1
            next_point = (x_coordinate, current_point[1])
            yield current_point, next_point
            current_point = next_point
        elif command == "V":
            if current_point is None:
                raise ValueError("V command appears before M")
            y_coordinate = float(tokens[token_index])
            token_index += 1
            next_point = (current_point[0], y_coordinate)
            yield current_point, next_point
            current_point = next_point
        else:
            raise ValueError(f"unsupported path command in {path_data!r}")


def IsCardLikeRectangle(
    rectangle_width: float,
    rectangle_height: float,
) -> bool:
    """排除整图背景和泳道，只保留卡片、总线与阶段块。"""

    normal_card = (
        70.0 <= rectangle_width <= 700.0
        and 40.0 <= rectangle_height <= 230.0
    )
    vertical_bus = (
        70.0 <= rectangle_width <= 150.0
        and 230.0 < rectangle_height <= 700.0
    )
    return normal_card or vertical_bus


def SegmentIntersectsExpandedRectangle(
    segment,
    rectangle,
    clearance_pixels: float,
) -> bool:
    """判断正交线段是否进入矩形外围的安全区。"""

    (first_x, first_y), (second_x, second_y) = segment
    rectangle_x, rectangle_y, rectangle_width, rectangle_height = rectangle
    left = rectangle_x - clearance_pixels
    right = rectangle_x + rectangle_width + clearance_pixels
    top = rectangle_y - clearance_pixels
    bottom = rectangle_y + rectangle_height + clearance_pixels
    epsilon = 1.0e-9

    if abs(first_y - second_y) < epsilon:
        if not top < first_y < bottom:
            return False
        segment_left, segment_right = sorted((first_x, second_x))
        return max(segment_left, left) < min(segment_right, right) - epsilon

    if abs(first_x - second_x) < epsilon:
        if not left < first_x < right:
            return False
        segment_top, segment_bottom = sorted((first_y, second_y))
        return max(segment_top, top) < min(segment_bottom, bottom) - epsilon

    return True


def CheckMarkerDefinitions(svg_path: Path, svg_root) -> list[str]:
    failures = []
    svg_source = svg_path.read_text(encoding="utf-8")

    if "marker-start" in svg_source:
        failures.append(
            f"{svg_path}: marker-start 会让箭头头部伸入源卡片"
        )

    marker_ids = {}
    for element in svg_root.iter():
        if element.tag.rsplit("}", 1)[-1] != "marker":
            continue
        marker_identifier = element.attrib.get("id")
        if marker_identifier is not None:
            marker_ids[marker_identifier] = element

    referenced_markers = set(OS_DIAGRAM_MARKER_REFERENCE.findall(svg_source))
    for marker_identifier in sorted(referenced_markers):
        marker = marker_ids.get(marker_identifier)
        if marker is None:
            failures.append(
                f"{svg_path}: missing marker definition {marker_identifier}"
            )
            continue
        if marker.attrib.get("markerUnits") != "userSpaceOnUse":
            failures.append(
                f"{svg_path}: marker {marker_identifier} must use "
                "userSpaceOnUse"
            )

    return failures


def CheckDiagram(svg_path: Path) -> tuple[list[str], int]:
    failures = []
    svg_root = ElementTree.parse(svg_path).getroot()
    failures.extend(CheckMarkerDefinitions(svg_path, svg_root))

    card_rectangles = []
    for element in svg_root.iter():
        if element.tag.rsplit("}", 1)[-1] != "rect":
            continue
        try:
            rectangle_x = float(element.attrib.get("x", 0.0))
            rectangle_y = float(element.attrib.get("y", 0.0))
            rectangle_width = float(element.attrib["width"])
            rectangle_height = float(element.attrib["height"])
        except (KeyError, ValueError):
            continue
        if IsCardLikeRectangle(rectangle_width, rectangle_height):
            card_rectangles.append(
                (
                    rectangle_x,
                    rectangle_y,
                    rectangle_width,
                    rectangle_height,
                )
            )

    checked_segment_count = 0
    for element in svg_root.iter():
        if element.tag.rsplit("}", 1)[-1] != "path":
            continue
        connector_class = element.attrib.get("class", "")
        if connector_class not in OS_DIAGRAM_CONNECTOR_CLASSES:
            continue
        try:
            path_segments = tuple(ParsePathSegments(element.attrib["d"]))
        except (KeyError, ValueError) as error:
            failures.append(f"{svg_path}: {error}")
            continue

        for path_segment in path_segments:
            checked_segment_count += 1
            for card_rectangle in card_rectangles:
                if SegmentIntersectsExpandedRectangle(
                    path_segment,
                    card_rectangle,
                    OS_DIAGRAM_CLEARANCE_PIXELS,
                ):
                    failures.append(
                        f"{svg_path}: connector {connector_class} segment "
                        f"{path_segment} enters the "
                        f"{OS_DIAGRAM_CLEARANCE_PIXELS:g}px safety zone of "
                        f"card {card_rectangle}"
                    )

    return failures, checked_segment_count


def PointOnSegment(point, segment) -> bool:
    """判断点是否位于水平或垂直线段上。"""

    point_x, point_y = point
    (first_x, first_y), (second_x, second_y) = segment
    epsilon = 1.0e-9

    if abs(first_y - second_y) < epsilon:
        return (
            abs(point_y - first_y) < epsilon
            and min(first_x, second_x) - epsilon
            <= point_x
            <= max(first_x, second_x) + epsilon
        )

    if abs(first_x - second_x) < epsilon:
        return (
            abs(point_x - first_x) < epsilon
            and min(first_y, second_y) - epsilon
            <= point_y
            <= max(first_y, second_y) + epsilon
        )

    return False


def SegmentsTouch(first_segment, second_segment) -> bool:
    """判断两条同网络正交线段是否相接或相交。"""

    (first_start_x, first_start_y), (
        first_end_x,
        first_end_y,
    ) = first_segment
    (second_start_x, second_start_y), (
        second_end_x,
        second_end_y,
    ) = second_segment
    epsilon = 1.0e-9
    first_horizontal = abs(first_start_y - first_end_y) < epsilon
    second_horizontal = abs(second_start_y - second_end_y) < epsilon

    if first_horizontal and second_horizontal:
        return (
            abs(first_start_y - second_start_y) < epsilon
            and max(
                min(first_start_x, first_end_x),
                min(second_start_x, second_end_x),
            )
            <= min(
                max(first_start_x, first_end_x),
                max(second_start_x, second_end_x),
            )
            + epsilon
        )

    if not first_horizontal and not second_horizontal:
        return (
            abs(first_start_x - second_start_x) < epsilon
            and max(
                min(first_start_y, first_end_y),
                min(second_start_y, second_end_y),
            )
            <= min(
                max(first_start_y, first_end_y),
                max(second_start_y, second_end_y),
            )
            + epsilon
        )

    horizontal_segment = (
        first_segment if first_horizontal else second_segment
    )
    vertical_segment = (
        second_segment if first_horizontal else first_segment
    )
    horizontal_y = horizontal_segment[0][1]
    vertical_x = vertical_segment[0][0]
    return (
        min(horizontal_segment[0][0], horizontal_segment[1][0])
        - epsilon
        <= vertical_x
        <= max(horizontal_segment[0][0], horizontal_segment[1][0])
        + epsilon
        and min(vertical_segment[0][1], vertical_segment[1][1])
        - epsilon
        <= horizontal_y
        <= max(vertical_segment[0][1], vertical_segment[1][1])
        + epsilon
    )


def CheckElectricalDiagram(svg_path: Path) -> tuple[list[str], int, int]:
    """验证学习电路图中的每个可见引脚都有可追踪导线。"""

    failures = []
    svg_source = svg_path.read_text(encoding="utf-8")
    for placeholder in OS_ELECTRICAL_FORBIDDEN_PLACEHOLDERS:
        if placeholder in svg_source:
            failures.append(
                f"{svg_path}: electrical diagram contains placeholder "
                f"{placeholder!r}"
            )

    svg_root = ElementTree.fromstring(svg_source)
    pins_by_net = {}
    pin_identifiers = set()
    segments_by_net = {}

    for element in svg_root.iter():
        element_name = element.tag.rsplit("}", 1)[-1]
        element_classes = frozenset(
            element.attrib.get("class", "").split()
        )

        if (
            element_name == "circle"
            and element_classes & OS_ELECTRICAL_PIN_CLASSES
        ):
            pin_identifier = element.attrib.get("data-pin", "")
            net_name = element.attrib.get("data-net", "")
            if not pin_identifier:
                failures.append(f"{svg_path}: visible pin has no data-pin")
                continue
            if pin_identifier in pin_identifiers:
                failures.append(
                    f"{svg_path}: duplicate pin identifier {pin_identifier}"
                )
            pin_identifiers.add(pin_identifier)
            if not net_name:
                failures.append(
                    f"{svg_path}: pin {pin_identifier} has no data-net"
                )
                continue
            try:
                pin_point = (
                    float(element.attrib["cx"]),
                    float(element.attrib["cy"]),
                )
            except (KeyError, ValueError):
                failures.append(
                    f"{svg_path}: pin {pin_identifier} has invalid coordinates"
                )
                continue

            no_connect = "no-connect" in element_classes
            if no_connect and net_name != "NC":
                failures.append(
                    f"{svg_path}: no-connect pin {pin_identifier} must use NC"
                )
            if not no_connect and net_name == "NC":
                failures.append(
                    f"{svg_path}: connected pin {pin_identifier} uses NC"
                )
            pins_by_net.setdefault(net_name, []).append(
                (pin_identifier, pin_point, no_connect)
            )

        if (
            element_name == "path"
            and OS_ELECTRICAL_WIRE_CLASS in element_classes
        ):
            net_name = element.attrib.get("data-net", "")
            if not net_name or net_name == "NC":
                failures.append(
                    f"{svg_path}: schematic wire has invalid net "
                    f"{net_name!r}"
                )
                continue
            try:
                path_segments = tuple(
                    ParsePathSegments(element.attrib["d"])
                )
            except (KeyError, ValueError) as error:
                failures.append(f"{svg_path}: {error}")
                continue
            if not path_segments:
                failures.append(
                    f"{svg_path}: schematic wire {net_name} has no segment"
                )
                continue
            segments_by_net.setdefault(net_name, []).extend(path_segments)

    for pin_identifier, _, _ in pins_by_net.get("NC", []):
        if "NC" in segments_by_net:
            failures.append(
                f"{svg_path}: NC pin {pin_identifier} is wired"
            )

    for net_name, net_pins in pins_by_net.items():
        if net_name == "NC":
            continue
        net_segments = segments_by_net.get(net_name, [])
        if not net_segments:
            for pin_identifier, _, _ in net_pins:
                failures.append(
                    f"{svg_path}: pin {pin_identifier} on {net_name} "
                    "has no wire"
                )
            continue

        for pin_identifier, pin_point, _ in net_pins:
            if not any(
                PointOnSegment(pin_point, segment)
                for segment in net_segments
            ):
                failures.append(
                    f"{svg_path}: wire for pin {pin_identifier} on "
                    f"{net_name} does not terminate at the pin coordinate"
                )

        parent = list(range(len(net_segments)))

        def Find(segment_index: int) -> int:
            while parent[segment_index] != segment_index:
                parent[segment_index] = parent[parent[segment_index]]
                segment_index = parent[segment_index]
            return segment_index

        def Union(first_index: int, second_index: int) -> None:
            first_root = Find(first_index)
            second_root = Find(second_index)
            if first_root != second_root:
                parent[second_root] = first_root

        for first_index, first_segment in enumerate(net_segments):
            for second_index in range(first_index + 1, len(net_segments)):
                if SegmentsTouch(
                    first_segment,
                    net_segments[second_index],
                ):
                    Union(first_index, second_index)

        component_pin_counts = {}
        for _, pin_point, _ in net_pins:
            touching_segment_index = next(
                (
                    segment_index
                    for segment_index, segment in enumerate(net_segments)
                    if PointOnSegment(pin_point, segment)
                ),
                None,
            )
            if touching_segment_index is None:
                continue
            component_root = Find(touching_segment_index)
            component_pin_counts[component_root] = (
                component_pin_counts.get(component_root, 0) + 1
            )

        for segment_index in range(len(net_segments)):
            component_root = Find(segment_index)
            if component_pin_counts.get(component_root, 0) < 2:
                failures.append(
                    f"{svg_path}: net {net_name} contains a disconnected "
                    "wire component with fewer than two visible pins"
                )
                break

    for net_name in sorted(set(segments_by_net) - set(pins_by_net)):
        failures.append(
            f"{svg_path}: net {net_name} has wires but no visible pins"
        )

    pin_count = sum(len(net_pins) for net_pins in pins_by_net.values())
    segment_count = sum(
        len(net_segments)
        for net_segments in segments_by_net.values()
    )
    return failures, pin_count, segment_count


def RunSelfTest() -> None:
    card_rectangle = (100.0, 100.0, 200.0, 100.0)
    assert SegmentIntersectsExpandedRectangle(
        ((50.0, 150.0), (350.0, 150.0)),
        card_rectangle,
        OS_DIAGRAM_CLEARANCE_PIXELS,
    )
    assert not SegmentIntersectsExpandedRectangle(
        ((50.0, 80.0), (350.0, 80.0)),
        card_rectangle,
        OS_DIAGRAM_CLEARANCE_PIXELS,
    )
    assert not SegmentIntersectsExpandedRectangle(
        ((50.0, 150.0), (90.0, 150.0)),
        card_rectangle,
        OS_DIAGRAM_CLEARANCE_PIXELS,
    )
    assert PointOnSegment(
        (150.0, 100.0),
        ((100.0, 100.0), (200.0, 100.0)),
    )
    assert SegmentsTouch(
        ((100.0, 100.0), (200.0, 100.0)),
        ((150.0, 50.0), (150.0, 150.0)),
    )
    print("diagram geometry self-test passed")


def Main() -> int:
    parser = argparse.ArgumentParser(
        description="检查学习图册连线与卡片之间的几何净距。"
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="先运行内置的正例与反例检查。",
    )
    arguments = parser.parse_args()

    if arguments.self_test:
        RunSelfTest()

    repository_root = Path(__file__).resolve().parent.parent
    diagram_directory = repository_root / "docs" / "learning" / "assets"
    all_failures = []
    total_segment_count = 0
    total_electrical_pin_count = 0
    total_electrical_segment_count = 0

    for diagram_file_name in OS_DIAGRAM_FILE_NAMES:
        diagram_path = diagram_directory / diagram_file_name
        if not diagram_path.is_file():
            all_failures.append(f"missing diagram: {diagram_path}")
            continue
        diagram_failures, checked_segment_count = CheckDiagram(diagram_path)
        all_failures.extend(diagram_failures)
        total_segment_count += checked_segment_count

    for diagram_file_name in OS_ELECTRICAL_DIAGRAM_FILE_NAMES:
        diagram_path = diagram_directory / diagram_file_name
        if not diagram_path.is_file():
            all_failures.append(f"missing diagram: {diagram_path}")
            continue
        (
            diagram_failures,
            checked_pin_count,
            checked_segment_count,
        ) = CheckElectricalDiagram(diagram_path)
        all_failures.extend(diagram_failures)
        total_electrical_pin_count += checked_pin_count
        total_electrical_segment_count += checked_segment_count

    if all_failures:
        for failure in all_failures:
            print(failure, file=sys.stderr)
        return 1

    print(
        f"diagram geometry passed: {len(OS_DIAGRAM_FILE_NAMES)} SVG files, "
        f"{total_segment_count} connector segments, "
        f"{OS_DIAGRAM_CLEARANCE_PIXELS:g}px card clearance; "
        f"{len(OS_ELECTRICAL_DIAGRAM_FILE_NAMES)} electrical SVG files, "
        f"{total_electrical_pin_count} explicit pins, "
        f"{total_electrical_segment_count} wire segments"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(Main())
