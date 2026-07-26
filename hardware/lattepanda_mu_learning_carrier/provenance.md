# 来源与完整性记录

## 上游

- 项目：LattePanda Mu
- 仓库：<https://github.com/LattePandaTeam/LattePanda-Mu>
- 本地归档提交：`f954bf0275fa0aec4c1e9eb168f09644563b28a4`
- 推荐基线：`[DFR1142]Lite Carrier for LattePanda Mu(V2)`
- 原理图/PCB 标题版本：V2.0.0，日期 2025-07-25
- 机械模型文件版本：V2.0.1，日期 2026-05-06
- 许可证：MIT，Copyright (c) 2024 LattePandaTeam

`upstream_dfr1142_v2/` 中除额外复制进去的 `LICENSE` 外，与上述提交中的
V2 电气目录逐文件一致。`previews/` 是用 Ghostscript 从上游 PDF 渲染出的
120 dpi 学习预览，不是另一份电路源文件。

## 关键文件 SHA-256

```text
bb1c8cf36b663a20b0bd29452596e839609730e9579aa8814e31443e1f1860ec  [DFR1142]Lite Carrier for LattePanda Mu(V2).kicad_sch
63674cd44e52e857c166faada4a69ffcc8deb8063bd3d4b0b513460fbd5f62ab  [DFR1142]Lite Carrier for LattePanda Mu(V2).kicad_pcb
47910dcaa6703de79d3715ec184455bdd234a52799f7700efad9731ef1a8acde  [DFR1142]Lite Carrier for LattePanda Mu(V2).pdf
517c91a0679005b36563b3f62a005fd3addea83944e8a1f5f23d8518435023e9  [DFR1142]Lite Carrier for LattePanda Mu(V2.0.1)-20260506.zip
```

## 已执行的本地检查

- 上游 V2 电气目录与本地副本做了逐文件比较：只有本地额外的 `LICENSE`；
- 十页 PDF 已全部成功渲染；
- STEP 压缩包已通过 ZIP CRC 完整性检查；
- XLSX 引脚表已通过 ZIP CRC 完整性检查；
- 从 PCB 源文件核对到四层叠层、305 个 footprint、407 个 net、3961 个
  segment 和 1307 个 via。

## 尚未执行

- KiCad ERC；
- KiCad DRC；
- 重新灌铜后的差异检查；
- Gerber/钻孔文件导出及 CAM 复核；
- BOM、替代料、生命周期和库存核对；
- SMT 坐标、钢网、装配图和 AOI 数据生成；
- 样板上电与接口信号完整性测试。

原因是当前开发环境没有 `kicad-cli`。任何人都不应把“上游是真实参考板”
误读成“本地修改后已经通过生产签核”。
