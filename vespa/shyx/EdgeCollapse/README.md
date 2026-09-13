# vtkSHYXEdgeCollapse

CGAL 边塌缩简化。输入三角化 `vtkPolyData`。Lindstrom–Turk / QEM / 中点会**移动**存活顶点。

若需要「不加新点、留下的点坐标不变」，用 [Subset Coarsen](../SubsetCoarsen/README.md)。

参数见同目录 `SHYXEdgeCollapse.xml`。
