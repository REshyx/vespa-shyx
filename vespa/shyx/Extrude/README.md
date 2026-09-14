# SHYX Extrude

菜单 **SHYX Extrude**（`vtkSHYXExtrudeFilter`）。接替原来的 **SHYX Point Extrude**。连通块选区挤出仍用旁边的 [Selection Extrude](../SelectionExtrude/README.md)。

## Type

- **Point**：沿**顶点法线**（或点矢量数组），顶点共享。只有 Output Front 时原地挪点（子集边界会拉邻面）。打开 Side 或 Back 后改成连通块：选区顶点复制一份做帽，外圈一圈侧壁，沿点法线。
- **Poly**：每个选中多边形沿**面法线**独立挤出（每面一套顶点，鳞片/刺猬）。Side 把每条棱接到原网格顶点；相邻选中面之间是缝。

## Output Front / Side / Back

默认 Front 开、Side/Back 关。三样都开是封闭棱柱；Front 与 Back 同时开时原面反向绕序。全关则只保留未选中几何。

## 挤谁

1. Selection 端口有 `vtkSelection`（创建时拷贝当前选区）；
2. 否则 Mask Array 非空：Threshold **Between / Below Lower / Above Upper**（与 `vtkThreshold` 相同的闭区间），同名数组优先 CellData，可 Invert、All Scalars；
3. 否则整网。

Poly 支持三角/四边/多边形。挤出新点/新面时不搬运原 point/cell 数组；Point 原地挪点时保留数组。

参数见同目录 `SHYXExtrude.xml`。
