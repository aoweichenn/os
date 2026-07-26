# STUOS x86-64 实体硬件学习基线

这个目录保存的是一个完整、可在 EDA 中检查的 x86-64 实体载板工程，不是
QEMU 内部结构的概念图。

选用的现实平台是 LattePanda Mu（Intel Processor N100/N305）计算模组与
DFR1142 Lite Carrier V2 载板。CPU、内存、固件和高速电源管理位于计算模组
内部；本目录中的十页原理图和四层 PCB 是模组之外的完整载板电路。

## 直接查看

- [十页完整原理图 PDF](./upstream_dfr1142_v2/%5BDFR1142%5DLite%20Carrier%20for%20LattePanda%20Mu%28V2%29.pdf)
- [KiCad 9 工程](./upstream_dfr1142_v2/%5BDFR1142%5DLite%20Carrier%20for%20LattePanda%20Mu%28V2%29.kicad_pro)
- [PCB 源文件](./upstream_dfr1142_v2/%5BDFR1142%5DLite%20Carrier%20for%20LattePanda%20Mu%28V2%29.kicad_pcb)
- [260-pin 模组引脚表](./module_pinout/LattePanda_Mu_Edge_Connector_Pinout.xlsx)
- [模组引脚图](./module_pinout/pinout.jpg)
- [载板 V2.0.1 STEP 机械模型压缩包](./mechanical/%5BDFR1142%5DLite%20Carrier%20for%20LattePanda%20Mu%28V2.0.1%29-20260506.zip)

安装 KiCad 9.0 或更高版本后，可在本目录执行：

```bash
kicad "upstream_dfr1142_v2/[DFR1142]Lite Carrier for LattePanda Mu(V2).kicad_pro"
```

当前环境没有安装 `kicad-cli`，所以本次没有伪造 ERC/DRC 通过结论。

上游 V2 工程把符号和已放置 footprint 嵌在原理图/PCB 中，查看、测量和
运行检查不依赖外部库；但它没有随目录发布全部自定义 footprint 库。
如果要更换这些自定义器件或从原理图重新生成一块空 PCB，需要先补齐库映射。
上游也没有在此目录提供可直接采购的完整 BOM、CPL 和 Gerber。

## 十页电路分别是什么

| 页 | 内容 | PNG 预览 |
|---:|---|---|
| 1 | 层次化总图及各功能页互连 | [查看](./previews/schematic-01.png) |
| 2 | 260-pin LattePanda Mu 模组连接器、全部供电及复用信号 | [查看](./previews/schematic-02.png) |
| 3 | PCIe 3.0 x4 插槽、AC 耦合、辅助电源和控制信号 | [查看](./previews/schematic-03.png) |
| 4 | HDMI 2.0、DDC/HPD、电源开关与 ESD 保护 | [查看](./previews/schematic-04.png) |
| 5 | USB 2.0/USB 3.x 接口、电源开关与 ESD 保护 | [查看](./previews/schematic-05.png) |
| 6 | RTL8111H 千兆以太网、时钟、磁性 RJ45 与供电 | [查看](./previews/schematic-06.png) |
| 7 | UART、I2C、GPIO、电源/复位/状态接口 | [查看](./previews/schematic-07.png) |
| 8 | M.2 E Key 与 M.2 M Key、PCIe/USB 辅助信号 | [查看](./previews/schematic-08.png) |
| 9 | 12–20 V DC、15 V USB-PD、负载开关、5 V/3.3 V 降压及按键 | [查看](./previews/schematic-09.png) |
| 10 | CPU 风扇、转速反馈、PWM 和温度输入 | [查看](./previews/schematic-10.png) |

## 它为什么是“真实电路”

这不是只有符号和连线的图片。工程包含：

- 305 个已落到 PCB 上的封装、407 个网络、3961 段布线和 1307 个过孔；
- 146 mm × 102 mm 的完整板框和器件布局；
- 四层、约 1.6062 mm 板厚的叠层：外层铜 35 µm，内层铜 15.2 µm；
- 高速差分对、网络类别、线宽、间距、过孔、铜皮和测试点；
- 电源、USB、HDMI、PCIe、M.2、千兆网、RTC、风扇及控制接口；
- 原理图、PCB、设计规则、引脚表、机械模型和 MIT 许可证。

这仍不等于“现在就能直接下单”。正式投板前还必须完成
[制造前检查](./manufacturing_checklist.md)，尤其是用当前 KiCad 版本重跑
ERC/DRC、核对 BIOS 的 HSIO 复用、器件可采购性、阻抗和板厂叠层。

## 与当前 STUOS 的关系

当前仓库里的 STUOS 面向 QEMU 的虚拟 x86-64 机器。QEMU 没有可供绘制的
实体引脚、电压和 PCB；它模拟的是一组软件设备。

这块载板提供真实的 Intel x86-64 硬件，但它通过 UEFI/BIOS 启动，并不等同
于项目当前的自定义 QEMU 复位 ROM。要让 STUOS 在它上面运行，仍需新增：

1. UEFI 启动入口或兼容的引导器；
2. ACPI、APIC、HPET/定时器和 PCIe 枚举；
3. 至少一种可用的现实控制台，例如 UEFI GOP 或串口；
4. NVMe/eMMC/USB 启动介质支持；
5. 对实际机器内存映射与固件表的处理。

因此，这个目录解决的是“现实设备的完整电气载体是什么”，不是声称当前
内核已经可以裸机启动。

## 来源与边界

电气基线来自 LattePandaTeam 的公开参考工程，未冒充为本项目原创设计。
本项目新增的是本地归档、逐页预览、版本/哈希记录和学习/制造检查说明。
完整来源和可重复核验信息见 [provenance.md](./provenance.md)。
