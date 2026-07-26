#!/usr/bin/env python3

"""生成 LattePanda Mu 真实载板的三张器件级学习连线图。"""

from pathlib import Path
from xml.sax.saxutils import escape


OS_SVG_HEADER = """<svg xmlns="http://www.w3.org/2000/svg"
     width="{width}" height="{height}" viewBox="0 0 {width} {height}"
     role="img" aria-labelledby="title description">
  <title id="title">{title}</title>
  <desc id="description">{description}</desc>
  <defs>
    <filter id="shadow" x="-20%" y="-20%" width="140%" height="150%">
      <feDropShadow dx="0" dy="4" stdDeviation="6"
                    flood-color="#0f172a" flood-opacity="0.10"/>
    </filter>
    <style>
      text {{ font-family: Inter, "Noto Sans SC", "Microsoft YaHei", sans-serif;
              fill: #172033; }}
      .title {{ font-size: 34px; font-weight: 760; }}
      .subtitle {{ font-size: 17px; fill: #526079; }}
      .panel-title {{ font-size: 22px; font-weight: 740; }}
      .component-title {{ font-size: 17px; font-weight: 720; }}
      .pin-label {{ font-size: 13px; fill: #334155; }}
      .net-label {{ font-family: "JetBrains Mono", "Noto Sans Mono", monospace;
                    font-size: 12px; fill: #1e40af; }}
      .note {{ font-size: 13px; fill: #526079; }}
      .component {{ fill: #ffffff; stroke: #64748b; stroke-width: 1.6; }}
      .module {{ fill: #e8f1ff; stroke: #2563eb; stroke-width: 1.8; }}
      .connector {{ fill: #fff7d6; stroke: #b7791f; stroke-width: 1.8; }}
      .power-block {{ fill: #fff1f2; stroke: #be123c; stroke-width: 1.8; }}
      .protection {{ fill: #f5f3ff; stroke: #7c3aed; stroke-width: 1.6; }}
      .schematic-wire {{ fill: none; stroke: #15803d; stroke-width: 2.2; }}
      .schematic-wire[data-kind="differential"] {{
          stroke: #2563eb; stroke-width: 2.1;
      }}
      .schematic-wire[data-kind="power"] {{
          stroke: #dc2626; stroke-width: 3.2;
      }}
      .schematic-wire[data-kind="ground"] {{
          stroke: #334155; stroke-width: 2.8;
      }}
      .schematic-wire[data-kind="control"] {{
          stroke: #7c3aed; stroke-width: 2.1;
      }}
      .pin {{ fill: #ffffff; stroke: #0f766e; stroke-width: 1.8; }}
      .junction {{ fill: #15803d; stroke: none; }}
      .no-connect {{ fill: #ffffff; stroke: #dc2626; stroke-width: 1.8; }}
      .nc-mark {{ stroke: #dc2626; stroke-width: 2; }}
      .panel {{ fill: #ffffff; stroke: #c3cfdd; stroke-width: 1.4; }}
      .legend {{ fill: #f8fafc; stroke: #cbd5e1; stroke-width: 1.2; }}
    </style>
  </defs>
  <rect width="{width}" height="{height}" fill="#f4f7fb"/>
"""


class SvgDocument:
    """用显式引脚和正交导线构造可检查的 SVG。"""

    def __init__(
        self,
        width: int,
        height: int,
        title: str,
        description: str,
    ) -> None:
        self.width = width
        self.height = height
        self.lines = [
            OS_SVG_HEADER.format(
                width=width,
                height=height,
                title=escape(title),
                description=escape(description),
            )
        ]

    def Add(self, source: str) -> None:
        self.lines.append(source)

    def AddText(
        self,
        x_coordinate: float,
        y_coordinate: float,
        text: str,
        css_class: str,
        anchor: str = "start",
    ) -> None:
        self.Add(
            f'  <text x="{x_coordinate:g}" y="{y_coordinate:g}" '
            f'class="{css_class}" text-anchor="{anchor}">'
            f"{escape(text)}</text>\n"
        )

    def AddRectangle(
        self,
        x_coordinate: float,
        y_coordinate: float,
        width: float,
        height: float,
        css_class: str,
        radius: float = 12.0,
        shadow: bool = False,
    ) -> None:
        shadow_attribute = ' filter="url(#shadow)"' if shadow else ""
        self.Add(
            f'  <rect x="{x_coordinate:g}" y="{y_coordinate:g}" '
            f'width="{width:g}" height="{height:g}" rx="{radius:g}" '
            f'class="{css_class}"{shadow_attribute}/>\n'
        )

    def AddPin(
        self,
        x_coordinate: float,
        y_coordinate: float,
        pin_identifier: str,
        net_name: str,
        no_connect: bool = False,
    ) -> None:
        css_class = "no-connect" if no_connect else "pin"
        self.Add(
            f'  <circle cx="{x_coordinate:g}" cy="{y_coordinate:g}" r="4.5" '
            f'class="{css_class}" data-pin="{escape(pin_identifier)}" '
            f'data-net="{escape(net_name)}"/>\n'
        )
        if no_connect:
            self.Add(
                f'  <path d="M {x_coordinate - 6:g} {y_coordinate - 6:g} '
                f'L {x_coordinate + 6:g} {y_coordinate + 6:g} '
                f'M {x_coordinate - 6:g} {y_coordinate + 6:g} '
                f'L {x_coordinate + 6:g} {y_coordinate - 6:g}" '
                f'class="nc-mark"/>\n'
            )

    def AddWire(
        self,
        points: tuple[tuple[float, float], ...],
        net_name: str,
        kind: str = "signal",
    ) -> None:
        if len(points) < 2:
            raise ValueError("a wire needs at least two points")
        path_parts = [f"M {points[0][0]:g} {points[0][1]:g}"]
        for x_coordinate, y_coordinate in points[1:]:
            path_parts.append(f"L {x_coordinate:g} {y_coordinate:g}")
        self.Add(
            f'  <path d="{" ".join(path_parts)}" class="schematic-wire" '
            f'data-net="{escape(net_name)}" data-kind="{kind}"/>\n'
        )

    def AddJunction(
        self,
        x_coordinate: float,
        y_coordinate: float,
        net_name: str,
    ) -> None:
        self.Add(
            f'  <circle cx="{x_coordinate:g}" cy="{y_coordinate:g}" r="4" '
            f'class="junction" data-net="{escape(net_name)}"/>\n'
        )

    def Finish(self) -> str:
        return "".join(self.lines) + "</svg>\n"


def AddTitle(
    document: SvgDocument,
    title: str,
    subtitle: str,
) -> None:
    document.AddText(45, 52, title, "title")
    document.AddText(45, 82, subtitle, "subtitle")


def AddConnectionPanel(
    document: SvgDocument,
    x_coordinate: float,
    y_coordinate: float,
    width: float,
    title: str,
    left_title: str,
    right_title: str,
    connections: tuple[tuple[str | None, str, str, str], ...],
) -> float:
    """画两列器件以及每一个明确的 pin-to-pin 网络。"""

    row_height = 30.0
    panel_height = 110.0 + row_height * len(connections)
    left_x = x_coordinate + 24.0
    block_width = 310.0
    right_x = x_coordinate + width - 24.0 - block_width
    block_y = y_coordinate + 74.0
    block_height = row_height * len(connections) + 18.0
    left_pin_x = left_x + block_width
    right_pin_x = right_x

    document.AddRectangle(
        x_coordinate,
        y_coordinate,
        width,
        panel_height,
        "panel",
        16,
    )
    document.AddText(
        x_coordinate + 24,
        y_coordinate + 35,
        title,
        "panel-title",
    )
    document.AddRectangle(
        left_x,
        block_y,
        block_width,
        block_height,
        "module",
        10,
    )
    document.AddRectangle(
        right_x,
        block_y,
        block_width,
        block_height,
        "connector",
        10,
    )
    document.AddText(
        left_x + block_width / 2,
        block_y + 24,
        left_title,
        "component-title",
        "middle",
    )
    document.AddText(
        right_x + block_width / 2,
        block_y + 24,
        right_title,
        "component-title",
        "middle",
    )

    first_row_y = block_y + 48.0
    for row_index, connection in enumerate(connections):
        net_name, left_pin, right_pin, kind = connection
        row_y = first_row_y + row_index * row_height
        if net_name is None:
            pin_identifier = f"{title}:{right_pin}"
            document.AddPin(
                right_pin_x,
                row_y,
                pin_identifier,
                "NC",
                True,
            )
            document.AddText(
                left_pin_x - 12,
                row_y + 5,
                left_pin,
                "pin-label",
                "end",
            )
            document.AddText(
                right_pin_x + 12,
                row_y + 5,
                right_pin,
                "pin-label",
            )
            document.AddText(
                (left_pin_x + right_pin_x) / 2,
                row_y + 5,
                "NC",
                "net-label",
                "middle",
            )
            continue

        left_identifier = f"{title}:{left_pin}"
        right_identifier = f"{title}:{right_pin}"
        document.AddPin(
            left_pin_x,
            row_y,
            left_identifier,
            net_name,
        )
        document.AddPin(
            right_pin_x,
            row_y,
            right_identifier,
            net_name,
        )
        document.AddWire(
            ((left_pin_x, row_y), (right_pin_x, row_y)),
            net_name,
            kind,
        )
        document.AddText(
            left_pin_x - 12,
            row_y + 5,
            left_pin,
            "pin-label",
            "end",
        )
        document.AddText(
            right_pin_x + 12,
            row_y + 5,
            right_pin,
            "pin-label",
        )
        document.AddText(
            (left_pin_x + right_pin_x) / 2,
            row_y - 6,
            net_name,
            "net-label",
            "middle",
        )

    return panel_height


def GenerateHighSpeedDiagram(output_path: Path) -> None:
    document = SvgDocument(
        2400,
        3060,
        "LattePanda Mu 载板高速接口完整连线学习图",
        "逐根展示 HDMI、USB、PCIe、M.2 和千兆网的差分线、时钟、复位、"
        "唤醒、电源和明确未接脚。",
    )
    AddTitle(
        document,
        "实体载板学习图 2/3：高速接口逐根连线",
        "每一对 P/N 都单独画出；成组电源脚只合并图形，不合并网络含义",
    )

    left_x = 35.0
    right_x = 1215.0
    panel_width = 1150.0
    left_y = 115.0
    right_y = 115.0

    hdmi_connections = (
        ("DDI_B_TX0_P", "Mu pin 217", "HDMI pin 1 TMDS D2+", "differential"),
        ("DDI_B_TX0_N", "Mu pin 215", "HDMI pin 3 TMDS D2-", "differential"),
        ("DDI_B_TX1_P", "Mu pin 211", "HDMI pin 4 TMDS D1+", "differential"),
        ("DDI_B_TX1_N", "Mu pin 209", "HDMI pin 6 TMDS D1-", "differential"),
        ("DDI_B_TX2_P", "Mu pin 205", "HDMI pin 7 TMDS D0+", "differential"),
        ("DDI_B_TX2_N", "Mu pin 203", "HDMI pin 9 TMDS D0-", "differential"),
        ("DDI_B_TX3_P", "Mu pin 199", "HDMI pin 10 TMDS CLK+", "differential"),
        ("DDI_B_TX3_N", "Mu pin 197", "HDMI pin 12 TMDS CLK-", "differential"),
        ("DDI_B_DDC_SCL", "Mu pin 171", "HDMI pin 15 SCL", "control"),
        ("DDI_B_DDC_SDA", "Mu pin 169", "HDMI pin 16 SDA", "control"),
        ("DDI_B_HPD", "Mu pin 183", "HDMI pin 19 HPD", "control"),
        ("HDMI_5V", "+5V through load switch", "HDMI pin 18 +5V", "power"),
        (
            "GND_HDMI",
            "GND plane",
            "HDMI pins 2,5,8,11,17,shell",
            "ground",
        ),
        (None, "explicitly not routed", "HDMI pin 13 CEC", "signal"),
        (None, "explicitly not routed", "HDMI pin 14 HEAC", "signal"),
    )
    left_y += AddConnectionPanel(
        document,
        left_x,
        left_y,
        panel_width,
        "HDMI 2.0",
        "LattePanda Mu / board rails",
        "J6 HDMI Type-A",
        hdmi_connections,
    ) + 28.0

    usb_connections = (
        ("USB3_SSTX_P", "Mu HSIO0 TX pin 13", "J4A SSTX1+ via C27", "differential"),
        ("USB3_SSTX_N", "Mu HSIO0 TX pin 15", "J4A SSTX1- via C26", "differential"),
        ("USB3_SSRX_P", "Mu HSIO0 RX pin 16", "J4A SSRX1+", "differential"),
        ("USB3_SSRX_N", "Mu HSIO0 RX pin 18", "J4A SSRX1-", "differential"),
        ("USB2_P2_P", "Mu pin 75", "J4A D1+", "differential"),
        ("USB2_P2_N", "Mu pin 73", "J4A D1-", "differential"),
        ("USB4_SSTX_P", "Mu HSIO1 TX pin 19", "J4B SSTX2+ via C29", "differential"),
        ("USB4_SSTX_N", "Mu HSIO1 TX pin 21", "J4B SSTX2- via C28", "differential"),
        ("USB4_SSRX_P", "Mu HSIO1 RX pin 22", "J4B SSRX2+", "differential"),
        ("USB4_SSRX_N", "Mu HSIO1 RX pin 24", "J4B SSRX2-", "differential"),
        ("USB2_P1_P", "Mu pin 69", "J4B D2+", "differential"),
        ("USB2_P1_N", "Mu pin 67", "J4B D2-", "differential"),
        ("USB2_P3_P", "Mu pin 81", "J5 upper D+", "differential"),
        ("USB2_P3_N", "Mu pin 79", "J5 upper D-", "differential"),
        ("USB2_P5_P", "Mu pin 111", "J5 lower D+", "differential"),
        ("USB2_P5_N", "Mu pin 109", "J5 lower D-", "differential"),
        ("USB_VBUS_1", "+5V through F4 6V/2A", "J4A VBUS1", "power"),
        ("USB_VBUS_2", "+5V through F5 6V/2A", "J4B VBUS2", "power"),
        ("USB_VBUS_3", "+5V through F2 6V/1A", "J5 VBUS1", "power"),
        ("USB_VBUS_4", "+5V through F3 6V/1A", "J5 VBUS2", "power"),
        (
            "GND_USB",
            "GND plane",
            "J4/J5 GND and shields",
            "ground",
        ),
    )
    left_y += AddConnectionPanel(
        document,
        left_x,
        left_y,
        panel_width,
        "四个外部 USB 端口",
        "LattePanda Mu / +5V rail",
        "J4 dual USB3 + J5 dual USB2",
        usb_connections,
    ) + 28.0

    ethernet_pcie_connections = (
        ("GBE_PCIE_TX_P", "Mu HSIO6 TX pin 61", "U6 HSIP pin 14", "differential"),
        ("GBE_PCIE_TX_N", "Mu HSIO6 TX pin 63", "U6 HSIN pin 13", "differential"),
        ("GBE_PCIE_RX_P", "Mu HSIO6 RX pin 64", "U6 HSOP pin 16", "differential"),
        ("GBE_PCIE_RX_N", "Mu HSIO6 RX pin 66", "U6 HSON pin 15", "differential"),
        ("GBE_REFCLK_P", "Mu REFCLK4 pin 94", "U6 REFCLK_P pin 18", "differential"),
        ("GBE_REFCLK_N", "Mu REFCLK4 pin 96", "U6 REFCLK_N pin 17", "differential"),
        ("PLT_RST_N_GBE", "Mu PLT_RST# pin 105", "U6 PERSTB pin 19", "control"),
        ("CLKREQ4_N", "Mu CLKREQ4# pin 102", "U6 CLKREQB pin 12", "control"),
        ("GBE_3V3", "+3V3 rail", "U6 AVDD33/DVDDREG", "power"),
        ("GND_GBE", "GND plane", "U6 GND/exposed pad", "ground"),
    )
    left_y += AddConnectionPanel(
        document,
        left_x,
        left_y,
        panel_width,
        "千兆网：PCIe 侧",
        "LattePanda Mu / rails",
        "U6 RTL8111H",
        ethernet_pcie_connections,
    ) + 28.0

    ethernet_phy_connections = (
        ("MDI0_P", "U6 MDI0+ pin 1", "J7 C1 through magnetics", "differential"),
        ("MDI0_N", "U6 MDI0- pin 2", "J7 C2 through magnetics", "differential"),
        ("MDI1_P", "U6 MDI1+ pin 4", "J7 C3 through magnetics", "differential"),
        ("MDI1_N", "U6 MDI1- pin 5", "J7 C6 through magnetics", "differential"),
        ("MDI2_P", "U6 MDI2+ pin 6", "J7 C4 through magnetics", "differential"),
        ("MDI2_N", "U6 MDI2- pin 7", "J7 C5 through magnetics", "differential"),
        ("MDI3_P", "U6 MDI3+ pin 9", "J7 C7 through magnetics", "differential"),
        ("MDI3_N", "U6 MDI3- pin 10", "J7 C8 through magnetics", "differential"),
        ("GBE_LED0", "U6 LED0 pin 27", "J7 LED1 cathode", "control"),
        ("GBE_LED1", "U6 LED1 pin 26", "J7 LED2 cathode", "control"),
        ("GND_RJ45", "GND plane", "J7 shield via chassis net", "ground"),
    )
    AddConnectionPanel(
        document,
        left_x,
        left_y,
        panel_width,
        "千兆网：PHY 与磁性 RJ45",
        "U6 RTL8111H",
        "J7 integrated-magnetics RJ45",
        ethernet_phy_connections,
    )

    pcie_connections = (
        ("PCIE_X4_TX0_P", "Mu HSIO8 TX pin 37", "J2 PETp0", "differential"),
        ("PCIE_X4_TX0_N", "Mu HSIO8 TX pin 39", "J2 PETn0", "differential"),
        ("PCIE_X4_RX0_P", "Mu HSIO8 RX pin 40", "J2 PERp0", "differential"),
        ("PCIE_X4_RX0_N", "Mu HSIO8 RX pin 42", "J2 PERn0", "differential"),
        ("PCIE_X4_TX1_P", "Mu HSIO9 TX pin 43", "J2 PETp1", "differential"),
        ("PCIE_X4_TX1_N", "Mu HSIO9 TX pin 45", "J2 PETn1", "differential"),
        ("PCIE_X4_RX1_P", "Mu HSIO9 RX pin 46", "J2 PERp1", "differential"),
        ("PCIE_X4_RX1_N", "Mu HSIO9 RX pin 48", "J2 PERn1", "differential"),
        ("PCIE_X4_TX2_P", "Mu HSIO10 TX pin 49", "J2 PETp2", "differential"),
        ("PCIE_X4_TX2_N", "Mu HSIO10 TX pin 51", "J2 PETn2", "differential"),
        ("PCIE_X4_RX2_P", "Mu HSIO10 RX pin 52", "J2 PERp2", "differential"),
        ("PCIE_X4_RX2_N", "Mu HSIO10 RX pin 54", "J2 PERn2", "differential"),
        ("PCIE_X4_TX3_P", "Mu HSIO11 TX pin 55", "J2 PETp3", "differential"),
        ("PCIE_X4_TX3_N", "Mu HSIO11 TX pin 57", "J2 PETn3", "differential"),
        ("PCIE_X4_RX3_P", "Mu HSIO11 RX pin 58", "J2 PERp3", "differential"),
        ("PCIE_X4_RX3_N", "Mu HSIO11 RX pin 60", "J2 PERn3", "differential"),
        ("PCIE_X4_REFCLK_P", "Mu REFCLK2 pin 97", "J2 REFCLK+ A13", "differential"),
        ("PCIE_X4_REFCLK_N", "Mu REFCLK2 pin 99", "J2 REFCLK- A14", "differential"),
        ("PLT_RST_N_X4", "Mu PLT_RST# pin 105", "J2 PERST# A11", "control"),
        ("PCIE_WAKE_N", "Mu PEWAKE# pin 103", "J2 WAKE# B11", "control"),
        ("PCIE_12VA", "+12VA after Q7", "J2 +12V B1/B2/B3", "power"),
        ("PCIE_3V3", "+3V3 rail", "J2 +3V3 and 3V3aux", "power"),
        ("GND_PCIE", "GND plane", "J2 all GND contacts", "ground"),
    )
    right_y += AddConnectionPanel(
        document,
        right_x,
        right_y,
        panel_width,
        "PCIe 3.0 x4 插槽",
        "LattePanda Mu / board rails",
        "J2 PCIe x4 edge connector",
        pcie_connections,
    ) + 28.0

    m_key_connections = (
        ("MKEY_TX_P", "Mu HSIO2 TX pin 25", "J15 PETp0 via C49", "differential"),
        ("MKEY_TX_N", "Mu HSIO2 TX pin 27", "J15 PETn0 via C50", "differential"),
        ("MKEY_RX_P", "Mu HSIO2 RX pin 28", "J15 PERp0", "differential"),
        ("MKEY_RX_N", "Mu HSIO2 RX pin 30", "J15 PERn0", "differential"),
        ("MKEY_REFCLK_P", "Mu REFCLK0- pin 87", "J15 REFCLKp", "differential"),
        ("MKEY_REFCLK_N", "Mu REFCLK0+ pin 85", "J15 REFCLKn", "differential"),
        ("PLT_RST_N_M", "Mu PLT_RST# pin 105", "J15 PERST#", "control"),
        ("MKEY_WAKE_N", "Mu PEWAKE# pin 103", "J15 PEWAKE#", "control"),
        ("SUSCLK_M", "Mu SUSCLK pin 131", "J15 SUSCLK pin 68", "control"),
        ("MKEY_3V3", "+3V3 rail", "J15 3.3V contacts", "power"),
        ("GND_MKEY", "GND plane", "J15 GND contacts", "ground"),
        (None, "explicitly not routed", "J15 CLKREQ#", "signal"),
        (None, "explicitly not routed", "J15 SATA pins", "signal"),
    )
    right_y += AddConnectionPanel(
        document,
        right_x,
        right_y,
        panel_width,
        "M.2 M Key 2230：PCIe x1",
        "LattePanda Mu / board rails",
        "J15 M.2 Socket M",
        m_key_connections,
    ) + 28.0

    e_key_connections = (
        ("EKEY_TX_P", "Mu HSIO3 TX pin 31", "J16 PETp0 via C55", "differential"),
        ("EKEY_TX_N", "Mu HSIO3 TX pin 33", "J16 PETn0 via C56", "differential"),
        ("EKEY_RX_P", "Mu HSIO3 RX pin 34", "J16 PERp0", "differential"),
        ("EKEY_RX_N", "Mu HSIO3 RX pin 36", "J16 PERn0", "differential"),
        ("EKEY_REFCLK_P", "Mu REFCLK3 pin 88", "J16 REFCLKp", "differential"),
        ("EKEY_REFCLK_N", "Mu REFCLK3 pin 90", "J16 REFCLKn", "differential"),
        ("USB2_P7_P", "Mu pin 76", "J16 USB_D+", "differential"),
        ("USB2_P7_N", "Mu pin 78", "J16 USB_D-", "differential"),
        ("PLT_RST_N_E", "Mu PLT_RST# pin 105", "J16 PERST#", "control"),
        ("CLKREQ3_N", "Mu CLKREQ3# pin 100", "J16 CLKREQ#", "control"),
        ("EKEY_WAKE_N", "Mu PEWAKE# pin 103", "J16 PEWAKE#", "control"),
        ("SUSCLK_E", "Mu SUSCLK pin 131", "J16 SUSCLK", "control"),
        ("EKEY_3V3", "+3V3 rail", "J16 3.3V contacts", "power"),
        ("GND_EKEY", "GND plane", "J16 GND contacts", "ground"),
        (None, "explicitly not routed", "J16 SDIO pins", "signal"),
        (None, "explicitly not routed", "J16 UART pins", "signal"),
    )
    AddConnectionPanel(
        document,
        right_x,
        right_y,
        panel_width,
        "M.2 E Key 2230：PCIe x1 + USB 2.0",
        "LattePanda Mu / board rails",
        "J16 M.2 Socket E",
        e_key_connections,
    )

    document.AddRectangle(35, 2990, 2330, 42, "legend", 10)
    document.AddText(
        55,
        3017,
        "蓝色：差分/高速；紫色：复位与控制；红色：电源；深灰：地。"
        "图中没有使用总线省略号，NC 以红叉明确标出。",
        "note",
    )
    output_path.write_text(document.Finish(), encoding="utf-8")


def GenerateControlDiagram(output_path: Path) -> None:
    document = SvgDocument(
        2400,
        2200,
        "LattePanda Mu 载板低速控制完整连线学习图",
        "逐根展示电源按键、复位、状态、RTC、风扇、UART、I2C 和 UPS 控制线。",
    )
    AddTitle(
        document,
        "实体载板学习图 3/3：低速控制、调试与状态线",
        "所有外露信号均为 3.3V 逻辑；每个连接器的电源和地也单独列出",
    )

    panel_width = 1150.0
    left_x = 35.0
    right_x = 1215.0
    left_y = 115.0
    right_y = 115.0

    button_connections = (
        ("PWR_SW_N", "Mu pin 1, internal 10k PU", "SW1 → GND", "control"),
        ("RST_SW_N", "Mu pin 3, internal 10k PU", "SW2 → GND", "control"),
        ("PSON", "Mu pin 5", "JP1 / PWR_EN / status LED", "control"),
        ("SLP_S4", "Mu pin 7", "JP1 / sleep LED", "control"),
        ("POWER_SW_HDR", "+3V3 through 200R", "JP4 POWER_SW", "power"),
        ("RESET_SW_HDR", "GND reference", "JP4 RESET_SW", "ground"),
        ("PWR_LED_HDR", "PSON transistor output", "JP4 PWR_LED", "control"),
        ("GND_BUTTONS", "GND plane", "SW1/SW2/JP4 return", "ground"),
    )
    left_y += AddConnectionPanel(
        document,
        left_x,
        left_y,
        panel_width,
        "开机、复位和状态",
        "LattePanda Mu / PSU logic",
        "SW1, SW2 and JP4",
        button_connections,
    ) + 28.0

    fan_connections = (
        ("CPU_FAN_PWM", "Mu pin 2 FAN2_CTL", "M1 PWM through R69", "control"),
        ("CPU_FAN_TACH", "Mu pin 4 FAN2_TAC", "M1 TACH through R68", "control"),
        ("CPU_FAN_5V", "+5V_FAN rail", "M1 power", "power"),
        ("GND_CPU_FAN", "GND plane CPU branch", "M1 return", "ground"),
        ("SYS_FAN_PWM", "Mu pin 6 FAN3_CTL", "M2 option PWM", "control"),
        ("SYS_FAN_TACH", "Mu pin 8 FAN3_TAC", "M2 option TACH", "control"),
        ("SYS_FAN_12V", "+12V option rail", "M2 option power", "power"),
        ("GND_SYS_FAN", "GND plane SYS branch", "M2 option return", "ground"),
        ("TEMP_SENSE", "Mu pin 9 TSENSE", "JP2 / optional NTC", "control"),
    )
    left_y += AddConnectionPanel(
        document,
        left_x,
        left_y,
        panel_width,
        "风扇和温度输入",
        "LattePanda Mu / rails",
        "M1 CPU fan, M2 option, JP2",
        fan_connections,
    ) + 28.0

    rtc_connections = (
        ("VBAT_RTC", "BT1 CR1220 positive", "Mu pin 115 VBAT", "power"),
        ("GND_RTC", "BT1 CR1220 negative", "GND plane", "ground"),
    )
    left_y += AddConnectionPanel(
        document,
        left_x,
        left_y,
        panel_width,
        "RTC 后备电池",
        "BT1 battery holder",
        "LattePanda Mu",
        rtc_connections,
    ) + 28.0

    ups_connections = (
        ("UPS_USB_DP", "J19 pin 7 through U19 ESD", "Mu USB2 P8+ pin 82", "differential"),
        ("UPS_USB_DN", "J19 pin 6 through U19 ESD", "Mu USB2 P8- pin 84", "differential"),
        ("UPS_STATE_LP", "J19 pin 4", "status input/test TP14", "control"),
        ("UPS_SW_LP", "J19 pin 5", "power control through R92", "control"),
        ("UPS_VBAT", "J19 pins 1/2", "D30-D32 → VDC", "power"),
        ("GND_UPS", "J19 pins 3/8/9/10", "GND plane", "ground"),
    )
    AddConnectionPanel(
        document,
        left_x,
        left_y,
        panel_width,
        "Smart UPS 扩展控制与 USB",
        "J19 DFR1247 header",
        "Mu / PSU nets",
        ups_connections,
    )

    i2c_connections = (
        ("I2C2_SDA", "Mu pin 156 through U9", "J7 pin 1", "control"),
        ("I2C3_SDA", "Mu pin 152 through U9", "J7 pin 2", "control"),
        ("I2C4_SDA", "Mu pin 148 through U9", "J7 pin 3", "control"),
        ("I2C5_SDA", "Mu pin 144 through U9", "J7 pin 4", "control"),
        ("I2C2_SCL", "Mu pin 154 through U11", "J9 pin 1", "control"),
        ("I2C3_SCL", "Mu pin 150 through U11", "J9 pin 2", "control"),
        ("I2C4_SCL", "Mu pin 146 through U11", "J9 pin 3", "control"),
        ("I2C5_SCL", "Mu pin 142 through U11", "J9 pin 4", "control"),
        ("I2C_3V3", "+3V3 rail", "J13 pins 1-4", "power"),
        ("GND_I2C", "GND plane", "J12 pins 1-4", "ground"),
    )
    right_y += AddConnectionPanel(
        document,
        right_x,
        right_y,
        panel_width,
        "四组 I2C：数据、时钟、电源和地",
        "LattePanda Mu through ESD arrays",
        "J7, J9, J12 and J13",
        i2c_connections,
    ) + 28.0

    uart_connections = (
        ("UART0_RX", "Mu pin 137 through U10", "J8 pin 1", "control"),
        ("UART1_RX", "Mu pin 141 through U10", "J8 pin 2", "control"),
        ("UART2_RX", "Mu pin 140 through U10", "J8 pin 3", "control"),
        ("UART0_TX", "Mu pin 139 through U12", "J10 pin 1", "control"),
        ("UART1_TX", "Mu pin 143 through U12", "J10 pin 2", "control"),
        ("UART2_TX", "Mu pin 138 through U12", "J10 pin 3", "control"),
        ("UART_3V3", "+3V3 rail", "J14 pins 1-3", "power"),
        ("GND_UART", "GND plane", "J11 pins 1-3", "ground"),
    )
    right_y += AddConnectionPanel(
        document,
        right_x,
        right_y,
        panel_width,
        "三组 SoC UART：RX、TX、电源和地",
        "LattePanda Mu through ESD arrays",
        "J8, J10, J11 and J14",
        uart_connections,
    ) + 28.0

    spi_connections = (
        ("SPI_IO3", "Mu pin 153", "U18 carrier BIOS pad IO3", "control"),
        ("SPI_CLK", "Mu pin 155", "U18 carrier BIOS pad CLK", "control"),
        ("SPI_MOSI_IO0", "Mu pin 159", "U18 carrier BIOS pad IO0", "control"),
        ("SPI_IO2", "Mu pin 161", "U18 carrier BIOS pad IO2", "control"),
        ("SPI_MISO_IO1", "Mu pin 163", "U18 carrier BIOS pad IO1", "control"),
        ("SPI_CS_N", "Mu pin 165", "U18 carrier BIOS pad CS#", "control"),
        ("BIOS_SEL", "Mu pin 133", "JP BIOS source select", "control"),
        ("BIOS_3V3_ROM", "+3V3_ROM from U17", "U18 VCC", "power"),
        ("GND_BIOS", "GND plane", "U18 GND", "ground"),
    )
    AddConnectionPanel(
        document,
        right_x,
        right_y,
        panel_width,
        "可选载板 BIOS SPI 焊盘",
        "LattePanda Mu / +3V3_ROM",
        "U18 unpopulated footprint",
        spi_connections,
    )

    document.AddRectangle(35, 2130, 2330, 42, "legend", 10)
    document.AddText(
        55,
        2157,
        "紫色：3.3V 控制；蓝色：USB 差分；红色：电源；深灰：地。"
        "GPIO 页的 ESD 阵列 U9-U12 保留在每条路径中。",
        "note",
    )
    output_path.write_text(document.Finish(), encoding="utf-8")


def AddPowerComponent(
    document: SvgDocument,
    x_coordinate: float,
    y_coordinate: float,
    width: float,
    height: float,
    title: str,
    detail: str,
    css_class: str,
) -> None:
    document.AddRectangle(
        x_coordinate,
        y_coordinate,
        width,
        height,
        css_class,
        10,
    )
    document.AddText(
        x_coordinate + width / 2,
        y_coordinate + 28,
        title,
        "component-title",
        "middle",
    )
    document.AddText(
        x_coordinate + width / 2,
        y_coordinate + 51,
        detail,
        "note",
        "middle",
    )


def GeneratePowerDiagram(output_path: Path) -> None:
    document = SvgDocument(
        2200,
        1320,
        "LattePanda Mu 载板电源完整连线学习图",
        "展示 DC、USB-PD 和 UPS 三路输入如何经保护和肖特基汇流到 VDC，"
        "再生成模组 VIN、PCIe 12V、5V、3.3V 和 3.3V ROM。",
    )
    AddTitle(
        document,
        "实体载板学习图 1/3：输入保护、汇流和电源树",
        "并联肖特基与电容阵列合并成器件组，但每个网络节点和控制线都保留",
    )

    ground_y = 1190.0
    document.AddWire(
        ((60, ground_y), (2140, ground_y)),
        "GND",
        "ground",
    )
    document.AddText(70, ground_y - 12, "GND PLANE", "net-label")

    vdc_x = 850.0
    document.AddWire(
        ((vdc_x, 180), (vdc_x, 900)),
        "VDC",
        "power",
    )
    document.AddText(vdc_x + 12, 202, "VDC common rail", "net-label")

    # 12 V DC input path.
    AddPowerComponent(document, 65, 190, 170, 90, "J17 DC Jack", "12V, 6A–10A", "connector")
    AddPowerComponent(
        document,
        300,
        202,
        130,
        66,
        "F6",
        "BSMD1206C-2100T",
        "protection",
    )
    AddPowerComponent(document, 500, 190, 210, 90, "D15,D16,D17", "SS54 in parallel", "protection")
    document.AddPin(235, 235, "J17.1", "DC_RAW")
    document.AddPin(300, 235, "F6.1", "DC_RAW")
    document.AddWire(((235, 235), (300, 235)), "DC_RAW", "power")
    document.AddPin(430, 235, "F6.2", "DC_FUSED")
    document.AddPin(500, 235, "D15-D17.A", "DC_FUSED")
    document.AddWire(((430, 235), (500, 235)), "DC_FUSED", "power")
    document.AddPin(710, 235, "D15-D17.K", "VDC")
    document.AddWire(((710, 235), (vdc_x, 235)), "VDC", "power")
    document.AddPin(150, 280, "J17.GND", "GND")
    document.AddWire(((150, 280), (150, ground_y)), "GND", "ground")
    AddPowerComponent(document, 320, 305, 150, 66, "D29", "SMAJ26CA TVS", "protection")
    document.AddPin(395, 305, "D29.K", "DC_FUSED")
    document.AddWire(
        ((395, 305), (465, 305), (465, 235)),
        "DC_FUSED",
        "power",
    )
    document.AddPin(395, 371, "D29.A", "GND")
    document.AddWire(((395, 371), (395, ground_y)), "GND", "ground")

    # 15 V USB-PD input path.
    AddPowerComponent(document, 65, 430, 170, 120, "J18 USB-C", "VBUS, CC1, CC2", "connector")
    AddPowerComponent(document, 300, 470, 210, 105, "U14 CH224K", "R43 selects 15V", "power-block")
    AddPowerComponent(document, 560, 430, 150, 90, "D18,D19,D20", "SS54 in parallel", "protection")
    document.AddPin(235, 460, "J18.VBUS", "USB_PD_15V")
    document.AddPin(560, 460, "D18-D20.A", "USB_PD_15V")
    document.AddWire(((235, 460), (560, 460)), "USB_PD_15V", "power")
    document.AddPin(710, 460, "D18-D20.K", "VDC")
    document.AddWire(((710, 460), (vdc_x, 460)), "VDC", "power")
    document.AddPin(235, 500, "J18.CC1", "USB_CC1")
    document.AddPin(300, 500, "U14.CC1", "USB_CC1")
    document.AddWire(((235, 500), (300, 500)), "USB_CC1", "control")
    document.AddPin(235, 530, "J18.CC2", "USB_CC2")
    document.AddPin(300, 530, "U14.CC2", "USB_CC2")
    document.AddWire(((235, 530), (300, 530)), "USB_CC2", "control")
    document.AddPin(405, 575, "U14.GND", "GND")
    document.AddWire(((405, 575), (405, ground_y)), "GND", "ground")
    document.AddPin(150, 550, "J18.GND", "GND")
    document.AddWire(((150, 550), (150, ground_y)), "GND", "ground")

    # Smart UPS input path and data connection.
    AddPowerComponent(document, 65, 690, 170, 130, "J19 Smart UPS", "+VBAT, D+, D-, CTRL", "connector")
    AddPowerComponent(document, 300, 700, 180, 90, "U19 ULC0524P", "USB ESD array", "protection")
    AddPowerComponent(document, 560, 680, 150, 90, "D30,D31,D32", "SS54 in parallel", "protection")
    document.AddPin(235, 720, "J19.1-2", "UPS_VBAT")
    document.AddPin(560, 720, "D30-D32.A", "UPS_VBAT")
    document.AddWire(((235, 720), (560, 720)), "UPS_VBAT", "power")
    document.AddPin(710, 720, "D30-D32.K", "VDC")
    document.AddWire(((710, 720), (vdc_x, 720)), "VDC", "power")
    document.AddPin(235, 755, "J19.7", "UPS_USB_DP")
    document.AddPin(300, 755, "U19.DP_IN", "UPS_USB_DP")
    document.AddWire(((235, 755), (300, 755)), "UPS_USB_DP", "differential")
    document.AddPin(235, 785, "J19.6", "UPS_USB_DN")
    document.AddPin(300, 785, "U19.DN_IN", "UPS_USB_DN")
    document.AddWire(((235, 785), (300, 785)), "UPS_USB_DN", "differential")
    document.AddPin(480, 740, "U19.DP_OUT", "MU_USB2_P8_P")
    document.AddPin(480, 770, "U19.DN_OUT", "MU_USB2_P8_N")
    AddPowerComponent(document, 520, 815, 220, 72, "Mu USB2 Port 8", "pins 82 P / 84 N", "module")
    document.AddPin(630, 815, "Mu.82", "MU_USB2_P8_P")
    document.AddWire(((480, 740), (520, 740), (520, 815), (630, 815)), "MU_USB2_P8_P", "differential")
    document.AddPin(670, 815, "Mu.84", "MU_USB2_P8_N")
    document.AddWire(((480, 770), (500, 770), (500, 835), (670, 835), (670, 815)), "MU_USB2_P8_N", "differential")
    document.AddPin(390, 790, "U19.GND", "GND")
    document.AddWire(((390, 790), (390, ground_y)), "GND", "ground")
    document.AddPin(150, 820, "J19.GND", "GND")
    document.AddWire(((150, 820), (150, ground_y)), "GND", "ground")

    # VDC consumers and generated rails.
    AddPowerComponent(document, 960, 145, 260, 100, "LattePanda Mu", "VIN pins 250–260", "module")
    document.AddPin(960, 195, "Mu.VIN.250-260", "VDC")
    document.AddWire(((vdc_x, 195), (960, 195)), "VDC", "power")
    document.AddPin(1090, 245, "Mu.GND", "GND")
    document.AddWire(((1090, 245), (1090, ground_y)), "GND", "ground")

    AddPowerComponent(document, 960, 300, 220, 90, "Q7 NCE30P28Q", "PCIe 12V load gate", "power-block")
    AddPowerComponent(document, 1280, 300, 220, 90, "J2 PCIe x4", "+12VA contacts", "connector")
    document.AddPin(960, 345, "Q7.S", "VDC")
    document.AddWire(((vdc_x, 345), (960, 345)), "VDC", "power")
    document.AddPin(1180, 345, "Q7.D", "PCIE_12VA")
    document.AddPin(1280, 345, "J2.12V", "PCIE_12VA")
    document.AddWire(((1180, 345), (1280, 345)), "PCIE_12VA", "power")
    document.AddPin(1070, 390, "Q7.GATE_CTRL", "PCIE_12V_EN")
    AddPowerComponent(document, 960, 425, 220, 80, "Q5,Q6,U13", "PSON + TL431 gate", "protection")
    document.AddPin(1070, 425, "Q5-Q6.OUT", "PCIE_12V_EN")
    document.AddWire(((1070, 425), (1070, 390)), "PCIE_12V_EN", "control")
    document.AddPin(1070, 505, "Q5-Q6.GND", "GND")
    document.AddWire(((1070, 505), (1070, ground_y)), "GND", "ground")
    document.AddPin(1390, 390, "J2.GND", "GND")
    document.AddWire(((1390, 390), (1390, ground_y)), "GND", "ground")

    AddPowerComponent(document, 960, 570, 220, 100, "U15 SY8253ADC", "VDC → +5V", "power-block")
    AddPowerComponent(document, 1280, 570, 250, 100, "+5V consumers", "USB, HDMI, fan", "connector")
    document.AddPin(960, 610, "U15.VIN", "VDC")
    document.AddWire(((vdc_x, 610), (960, 610)), "VDC", "power")
    document.AddPin(1180, 610, "U15.VOUT", "RAIL_5V")
    document.AddPin(1280, 610, "5V_LOADS", "RAIL_5V")
    document.AddWire(((1180, 610), (1280, 610)), "RAIL_5V", "power")
    document.AddPin(1070, 670, "U15.GND", "GND")
    document.AddWire(((1070, 670), (1070, ground_y)), "GND", "ground")
    document.AddPin(960, 630, "U15.EN", "PWR_EN")
    document.AddPin(1405, 670, "5V_LOADS.GND", "GND")
    document.AddWire(((1405, 670), (1405, ground_y)), "GND", "ground")

    AddPowerComponent(document, 960, 750, 220, 100, "U16 SY8253ADC", "VDC → +3V3", "power-block")
    AddPowerComponent(document, 1280, 750, 250, 100, "+3V3 consumers", "M.2, GbE, GPIO", "connector")
    document.AddPin(960, 790, "U16.VIN", "VDC")
    document.AddWire(((vdc_x, 790), (960, 790)), "VDC", "power")
    document.AddPin(1180, 790, "U16.VOUT", "RAIL_3V3")
    document.AddPin(1280, 790, "3V3_LOADS", "RAIL_3V3")
    document.AddWire(((1180, 790), (1280, 790)), "RAIL_3V3", "power")
    document.AddPin(1070, 850, "U16.GND", "GND")
    document.AddWire(((1070, 850), (1070, ground_y)), "GND", "ground")
    document.AddPin(960, 810, "U16.EN", "PWR_EN")
    document.AddPin(1405, 850, "3V3_LOADS.GND", "GND")
    document.AddWire(((1405, 850), (1405, ground_y)), "GND", "ground")

    AddPowerComponent(document, 1640, 570, 210, 100, "U17 HM7533HBPR", "VDC → +3V3_ROM", "power-block")
    AddPowerComponent(document, 1910, 570, 220, 100, "U18 BIOS pad", "optional population", "connector")
    document.AddPin(1640, 610, "U17.VIN", "VDC")
    document.AddWire(
        ((vdc_x, 880), (1580, 880), (1580, 610), (1640, 610)),
        "VDC",
        "power",
    )
    document.AddPin(1850, 610, "U17.VOUT", "RAIL_3V3_ROM")
    document.AddPin(1910, 610, "U18.VCC", "RAIL_3V3_ROM")
    document.AddWire(((1850, 610), (1910, 610)), "RAIL_3V3_ROM", "power")
    document.AddPin(1745, 670, "U17.GND", "GND")
    document.AddWire(((1745, 670), (1745, ground_y)), "GND", "ground")
    document.AddPin(2020, 670, "U18.GND", "GND")
    document.AddWire(((2020, 670), (2020, ground_y)), "GND", "ground")

    # Shared enable selection for the two buck converters.
    AddPowerComponent(document, 960, 970, 260, 92, "JP1 + R46", "PSON or SLP_S4 → PWR_EN", "protection")
    AddPowerComponent(document, 1280, 970, 280, 92, "Mu status outputs", "pin 5 PSON, pin 7 SLP_S4", "module")
    document.AddPin(1280, 1002, "Mu.5", "PSON")
    document.AddPin(1220, 1002, "JP1.PSON", "PSON")
    document.AddWire(((1220, 1002), (1280, 1002)), "PSON", "control")
    document.AddPin(1280, 1035, "Mu.7", "SLP_S4")
    document.AddPin(1220, 1035, "JP1.SLP_S4", "SLP_S4")
    document.AddWire(((1220, 1035), (1280, 1035)), "SLP_S4", "control")
    document.AddPin(960, 1018, "JP1.OUT", "PWR_EN")
    document.AddWire(((960, 1018), (910, 1018), (910, 630), (960, 630)), "PWR_EN", "control")
    document.AddWire(((910, 810), (960, 810)), "PWR_EN", "control")

    document.AddRectangle(60, 1240, 2080, 45, "legend", 10)
    document.AddText(
        80,
        1268,
        "红色：功率路径；紫色：控制；蓝色：USB 差分；深灰：公共地。"
        "D15–D20、D30–D32 是逐颗并联器件组，不表示一颗等效二极管。",
        "note",
    )
    output_path.write_text(document.Finish(), encoding="utf-8")


def Main() -> int:
    repository_root = Path(__file__).resolve().parent.parent
    output_directory = (
        repository_root
        / "docs"
        / "learning"
        / "assets"
        / "physical_carrier"
    )
    output_directory.mkdir(parents=True, exist_ok=True)
    GeneratePowerDiagram(output_directory / "power_wiring.svg")
    GenerateHighSpeedDiagram(output_directory / "high_speed_wiring.svg")
    GenerateControlDiagram(output_directory / "control_wiring.svg")
    print(f"generated physical carrier diagrams in {output_directory}")
    return 0


if __name__ == "__main__":
    raise SystemExit(Main())
