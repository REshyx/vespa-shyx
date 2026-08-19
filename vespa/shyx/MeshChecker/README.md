# vtkSHYXMeshChecker

SHYX 网格诊断与可选修复（CGAL PMP）。相对上游 VESPA Mesh Checker，输出更多非法图元几何。

**端口 0**：组合修复后的 `vtkPolyData`（定向 + `repair_polygon_soup`；可选自交 autorefine）。  
**端口 1**：诊断几何——汤边（线）、边界环（折线）、自交三角形。

主要开关：非流形汤边、边界环、自交、定向/`repair_polygon_soup`、可选 autorefine。详见同目录 `SHYXMeshChecker.xml`。
