# 整机学习图的书稿矢量副本

本目录中的七个 PDF 分别由 `docs/learning/assets/` 下同名 SVG 转换而成：

- `hardware_stage_roadmap.pdf`
- `x86_64_os_hardware_wiring.pdf`
- `boot_and_memory_wiring.pdf`
- `port_io_topology.pdf`
- `interrupt_routing.pdf`
- `keyboard_to_shell.pdf`
- `storage_persistence.pdf`

转换使用 librsvg 与 Cairo 的 PDF 后端，只改变书稿输入容器，不把 SVG
栅格化。线条、文字和几何图元在最终 PDF 中仍为矢量对象，可无损放大。
源 SVG 不使用 SVG 阴影滤镜：Cairo 会为滤镜建立整页 image XObject，造成
看似是 PDF、实际主图已经位图化。书稿输入检查会扫描这七个 PDF，发现任何
image XObject 就拒绝构建。
事实来源仍是对应 SVG；修改图后应重新生成 PDF，并运行：

```bash
python3 tools/check_learning_diagrams.py --self-test
make -C books/x86-64-os-from-reset pdf
```

七张图在书中使用横置整页环境，避免缩成普通段落宽度后文字过小。源 SVG 的
卡片净距与连接线由几何检查器验证；最终 PDF 还需抽页渲染，确认标题、图形、
图注和页边距没有重叠或裁切。
