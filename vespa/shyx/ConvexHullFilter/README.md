# vtkSHYXConvexHullFilter

输入点集的三维凸包：可选 `vtkCleanPolyData` → `vtkDelaunay3D`（Alpha=0）→ 表面提取 → 三角化。纯 VTK，不依赖 CGAL。

参数：是否清洗重合点、清洗容差。详见同目录 `SHYXConvexHullFilter.xml`。
