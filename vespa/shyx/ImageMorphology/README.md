# vtkSHYXImageMorphology

`vtkImageData` 点标量上的形态学：膨胀 / 侵蚀 / 开 / 闭、形态学梯度（胀−蚀，不是 ParaView **Gradient Magnitude**）、内外梯度、白/黑顶帽、二值击中-击不中。纯 VTK，不依赖 CGAL。

**输入**：1 分量点标量。**输出**：同尺寸图像，替换所选数组，其余数组透传。

**结构元**：`KernelSize`（奇数体素，默认 `3 3 3`）；形状 Box / Cross / **Ellipsoid**（默认，对齐 VTK `vtkImageDilateErode3D`）。2D（单层 Z）自动 `KernelSize_Z = 1`。

**Value Mode**：Binary 只改前景/背景标签；Grayscale 为邻域 max/min。击中-击不中始终按二值匹配（前景核全为 FG，外围壳全为 BG）。

Python：`SHYXImageMorphology()`。面板字段见同目录 `SHYXImageMorphology.xml`。
