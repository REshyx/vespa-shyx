# vtkSHYXMinimumOBBFilter

菜单 **SHYX Minimum OBB**。输入任意 `vtkDataSet` 的全部点，输出闭合三角盒子 `vtkPolyData`。Field data：`OBB.Center`、`OBB.HalfLengths`、`OBB.Axis0..2`、`OBB.Volume`、`OBB.IsAxisAlignedFallback`。

**Box Type**（`BoxType`，默认 **2**）：

| 值 | 界面 | 算法 |
|----|------|------|
| 2 | OBB (min-volume) | CGAL `oriented_bounding_box`（近似最小体积，不会比 AABB 更大） |
| 0 | OBB (PCA) | `vtkOBBTree::ComputeOBB`（协方差启发式，可能大于 AABB） |
| 1 | AABB | 世界轴对齐包围盒 |

**Copy Input Points** 默认 ON：先拷到连续 `vtkPoints`（任意数据集都安全）。OFF 时 `vtkPointSet` 直接用内部点，滤镜运行期间不要改坐标。

属性面板 **OBB interactive transform**（`shyx_obb_interactive_box`）：Translate / Rotate (Degrees) / Scale，约定与 ParaView Interactive Box 相同。切换 Box Type 会重新拟合并复位 widget。`UseReferenceBounds` / `ReferenceBounds` 为 widget 内部属性（`panel_visibility="never"`）。

参数见同目录 `SHYXMinimumOBB.xml`。
