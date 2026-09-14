# vtkSHYXHoleFillFilter

菜单 **SHYX Hole Fill (CGAL)**。用 CGAL polygon mesh processing 填充孔洞。与上游 VESPA Hole Filling 同类，面板独立；新管线优先用本滤镜。

输入 `vtkDataSet`（必要时先 GeometryFilter）。默认 **Repair polygon soup first** 与 Mesh Checker 相同：`orient_polygon_soup` + `repair_polygon_soup` 后再建 `Surface_mesh` 并补每个边界环。原网格若在洞口有重复顶点，直接 Euler `add_face` 会静默不补，焊点后才能补。关掉则走上游 `vtkCGALPatchFilling`。可选 **Selection**（port 1，创建时拷当前选择）先挖再补。`FairingContinuity` 默认 1（0–2）。**不**把属性插值到新补的三角面上。

详见同目录 `SHYXHoleFillFilter.xml`。
