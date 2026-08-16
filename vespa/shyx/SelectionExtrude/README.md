# SHYX Selection Extrude（选区沿平均法线挤出）

`vtkSHYXSelectionExtrudeFilter` 对三角表面网格上的一块选区挤出。默认 **AverageNormals=1**：按面积加权求**一条平均法线**再平移。**AverageNormals=0** 时帽面各顶点用各自选中三角形的点法线。场数据 **`SHYX_SelectionExtrude_AvgNormal`** 始终是该片的单位平均法线。

## ParaView 用法（与 VESPA Region Fairing 相同的选区方式）

1. 读入三角化表面（`vtkPolyData`），在视图中用鼠标**选中面或点**（与 Region Fairing 一样使用 ParaView 的选区机制）。
2. 对同一表面应用滤镜 **SHYX Selection Extrude**：
   - **Input**：该表面；
   - **Selection**：界面上的 **Selection** 输入会自动关联当前 `vtkSelection`（XML 中带 `SelectionInput` 提示，无需用户再手动接一条「Extract Selection」管线）。
3. 在属性面板调节 **Extrusion Distance**（可做成动画属性， scrub 或时间轴连续改距离，接近「拖拽」挤出量的交互）。

滤镜内部使用 `vtkExtractSelection`：优先根据提取结果单元上的 **`vtkOriginalCellIds`** 确定原网格上的三角形；若没有，则根据 **`vtkOriginalPointIds`** 取所有包含这些顶点的三角形。

若不使用视图选区，仍可在 **Selection Cell Array Name** 中指定输入网格上的单元数组名（浮点 > 0.5 或整型非零视为选中），作为仅单输入时的备选。

## 输出

- 挤出后的 `vtkPolyData`：未选中单元不变；**原位置的选中三角形已去掉**，只保留平移后的顶面与选区边界上的侧壁（开口壳体）。
- 场数据 **`SHYX_SelectionExtrude_AvgNormal`**（三元组）：本次使用的单位平均法线。

**Flip Direction** 在距离符号之外再反向平均法线。输入中非三角形的未选中单元会被忽略（仅支持三角表面流水线）。
