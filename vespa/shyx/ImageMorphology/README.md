# vtkSHYXImageMorphology

`vtkImageData` 点标量上的形态学：膨胀 / 侵蚀 / 开 / 闭、形态学梯度（胀−蚀，不是 ParaView **Gradient Magnitude**）、内外梯度、白/黑顶帽、二值击中-击不中。纯 VTK，不依赖 CGAL。

**输入**：1 分量点标量。**输出**：同尺寸图像，替换所选数组，其余数组透传。

**结构元**：`KernelSize`（奇数体素，默认 `3 3 3`）；形状 Box / Cross / **Ellipsoid**（默认，对齐 VTK `vtkImageDilateErode3D`）。2D（单层 Z）自动 `KernelSize_Z = 1`。

**Value Mode**：Binary 先判定前景/背景再做形态学；Grayscale 为邻域 max/min。击中-击不中始终按二值。

**Binary Match**（Binary 时，紧跟 Value Mode）：

- **Threshold**（默认）：一个数，`值 >= Threshold`（默认 `0.5`）为前景，否则背景；结果写成 1 / 0。
- **Equal**：两个数严格相等（`== Foreground` / `== Background`）；其它标签原样拷贝。

Python：`SHYXImageMorphology()`。面板字段见同目录 `SHYXImageMorphology.xml`。
