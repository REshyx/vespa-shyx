# vtkSHYXImageAntiAlias

菜单 **SHYX Image AntiAlias**。用 VMTK 安装树里的 ITK `AntiAliasBinaryImageFilter`（Whitaker 占用约束曲率流）去掉二值 `vtkImageData` 的体素台阶。需 `VESPA_USE_VMTK`，ITK 来自同一 `VMTK_DIR`，不是另一份独立 InsightToolkit。

**输入**：1 分量点标量。**输出**：**float level set**（内正外负）。**Resample Factor** 默认 1（原网格）；&gt;1 加密、&lt;1 减粗后再 AntiAlias，输出跟重采样后的网格走，抽表面仍用 **Contour 0**。Factor ≠ 1 时其它点/胞数组不透传。

默认 **Binarize Mode = Threshold**（≥ 0.5 为内）。重采样插值默认 **Nearest**（保占用）；Linear 在 Threshold 模式下会再二值化。已经是 0/1 或 0/255 时可用 Auto min/max。多标签体请先 Threshold。不是形态学开闭，也不是高斯模糊。

Python：`SHYXImageAntiAlias()`。面板字段见同目录 `SHYXImageAntiAlias.xml`。
