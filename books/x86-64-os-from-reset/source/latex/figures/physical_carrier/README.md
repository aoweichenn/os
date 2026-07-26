# 实体载板书稿图片来源

本目录只保存 LaTeX 构建所需的本地图片副本，不是新的电气事实来源。

- `reference_schematic.pdf` 复制自
  `hardware/lattepanda_mu_learning_carrier/upstream_dfr1142_v2/`
  中的 DFR1142 Lite Carrier V2 十页上游 PDF。该文件原样保留一个
  516×144 的上游标题标识图像；器件、文字、网络和导线本体仍是矢量对象。
- `power_wiring.pdf`、`high_speed_wiring.pdf`、`control_wiring.pdf`
  分别由 `docs/learning/assets/physical_carrier/` 中的同名 SVG 转换而成，
  转换只改变容器格式，保留矢量内容。

上游 `module_pinout/pinout.jpg` 是一张位图照片，直接嵌入 PDF 后无法无损
放大。因此书稿没有复制它，而是依据该照片、260-pin 表和模块资料，用 TikZ
重画正反面、辅助连接器、全部触点刻线和 pin 1/2/259/260 方向。矢量图只表达
方向与功能区域，不伪造照片中的器件细节。

上游参考工程固定到提交
`f954bf0275fa0aec4c1e9eb168f09644563b28a4`，使用 MIT 许可证。来源、
SHA-256 与边界记录见
`hardware/lattepanda_mu_learning_carrier/provenance.md`。

三张学习图应先通过：

```bash
python3 tools/generate_physical_carrier_diagrams.py
python3 tools/check_learning_diagrams.py --self-test
```

LaTeX 输入检查会验证本目录中被 `\includegraphics` 引用的文件存在且没有越过
书稿源码根目录。
