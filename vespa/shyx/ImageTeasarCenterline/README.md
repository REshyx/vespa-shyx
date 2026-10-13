# vtkSHYXImageTeasarCenterline

从 `vtkImageData` 提中心线，不经过表面网格，也不链接 Kimimaro。算法是 TEASAR（Sato、Bitter、Kaufman）：在体素图上做惩罚最短路径。

**输入**：1 分量点标量。

- **Threshold**（默认）：`值 >= Threshold`（默认 0.5）为前景，再按 26 连通分成若干段。
- **Label**：正整数标签各算一个物体（非整数忽略）。`Label Value = 0` 处理全部标签，否则只处理那一个。

**输出**：`vtkPolyData` 折线（树，环会被拆开）。

| 数组 | 位置 | 含义 |
|------|------|------|
| `MaximumInscribedSphereRadius` | 点 | 到最近非前景体素的物理距离（图像 spacing） |
| `ObjectId` | 点 / 线 | Threshold 模式下恒为 1；Label 模式下为标签值 |
| `ComponentId` | 点 / 线 | 26 连通分量编号，从 1 起 |
| `PathId` | 线 | 折线序号 |

**步骤**（每个连通分量）：

1. 各向异性欧氏距离（到背景）。图像外不被当成背景，所以贴着体数据边界的开口半径不会被边界压成 0。
2. 从任意前景点做测地距离，最远点所在的一圈体素取质心，把根挪到开口中心，而不是帽缘。
3. 惩罚场：`penaltyScale * (1 - r / rMax) ^ exponent + 归一化测地距离`。边权是终点惩罚乘物理步长。
4. 下一个目标是剩余体素里测地距离最大的点，同样收到开口中心。沿最短路径回溯，直到撞上已有骨架，只把新的一段写成折线。
5. 沿路径把半径 `scale * r + const` 的轴对齐立方体标成已访问。`scale` / `const` 用图像物理单位，不是纳米。血管上 `const` 保持 0；`scale` 1.5 留分支，4–10 几乎只留主干。
6. **Fix Branching**（默认开）：已有骨架上的惩罚清零，下一条路径会贴着旧线走到分叉附近再离开。
7. **Reach Volume Boundary**（默认开）：前景贴着体数据六个面时，先连到接触面里半径最大的体素。

内部空洞会把距离场吸过去。要填洞先用 **SHYX Image Morphology** 的 Close。这不是 VMTK 表面 Voronoi 中心线；下游若认 `MaximumInscribedSphereRadius`，这条折线可以直接当半径用。

Python：`SHYXImageTeasarCenterline()`。
