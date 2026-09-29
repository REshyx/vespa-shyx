# SHYX Extrude

菜单 **SHYX Extrude**（`vtkSHYXExtrudeFilter`）。接替原来的 **SHYX Point Extrude**。连通块选区挤出仍用旁边的 [Selection Extrude](../SelectionExtrude/README.md)。

## Type

- **Point**：沿**顶点法线**（或点矢量数组），顶点共享。只有 Output Front 时原地挪点（子集边界会拉邻面）。打开 Side 或 Back 后改成连通块：选区顶点复制一份做帽，外圈一圈侧壁，沿点法线。
- **Avoid Intersection**（仅 Point，默认关）：每个面沿面法线平移，顶点取这些偏移平面的交点。帽面翻过去、面积塌掉，或位移撞上不相邻的输入面时，把该顶点缩短，所以凹区和细缝里的实际厚度可以小于 Extrusion Distance。勾选后 Use Normals 和 Direction Array 不参与；选中面的每个顶点都会动，包括只开 Front 的原地挤出。
- **Poly**：每个选中多边形沿**面法线**独立挤出（每面一套顶点，鳞片/刺猬）。Side 把每条棱接到原网格顶点；相邻选中面之间是缝。

## Output Front / Side / Back

默认 Front 开、Side/Back 关。三样都开是封闭棱柱；Front 与 Back 同时开时原面反向绕序。全关则只保留未选中几何。

## 挤谁

**Use Selection** 和 **Use Mask Array** 默认都关。关 = 这一路不裁（全选 / 全部挤出），相关控件隐藏。两路独立，不是互斥：

- 都关 → 整网；
- 只开 Use Selection → 该选区（创建时仍会把当前选区拷进 widget，打开开关不必再 Copy Active Selection，除非后来改了选区）；
- 只开 Use Mask Array → Threshold（Between / Below Lower / Above Upper，同名优先 CellData，可 All Scalars）；
- **两个都开 → 交集**。

Invert 在组合之后取反。

Poly 支持三角/四边/多边形。挤出新点/新面时不搬运原 point/cell 数组；Point 原地挪点时保留数组。

参数见同目录 `SHYXExtrude.xml`。
