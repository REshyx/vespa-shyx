# vtkSHYXSurfaceThickness

菜单 **SHYX Surface Thickness**。给表面每个顶点写局部壁厚，几何与拓扑不变。

**默认 Self-proximity**：在 `Max Distance` 内找欧氏最近邻，但丢掉 `Geodesic Rings` 跳以内的同壁顶点；可选只要内法向一侧的点。不打射线，交叉面仍能给出 0（贴死）或很小的厚度。

**Ray along normal / Shape diameter**：沿内法向（或锥内多条射线取中位数）打到对面。交叉面或法向反了会不准，可用 **Flip Normals**。

| 点数组 | 含义 |
| :--- | :--- |
| `Thickness` | 壁到对侧距离；没打到为 **-1** |
| `ThicknessValid` | 1 = 有效（含厚度 0 的 kissing/交叉）；0 = 未找到对侧 |
| `ThicknessOverEdgeLength` | `Thickness / LocalEdgeLength`，坏段判据用这个（\(\rho \lesssim 2\) 往往已扁） |
| `LocalEdgeLength` | 该点入射边平均长 |

`Max Distance` 默认 0 = 包围盒最长边的 5%（面板 Scale/Reset）。Field data `SHYXSurfaceThicknessSearchDistance` 是本次实际用的搜索距离。

纯 VTK（`vtkStaticPointLocator` / `vtkStaticCellLocator` + `vtkSMPTools`），不依赖 `VESPA_USE_SMP`。
