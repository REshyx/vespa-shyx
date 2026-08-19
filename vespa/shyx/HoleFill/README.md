# vtkSHYXHoleFillFilter

菜单 **SHYX Hole Fill (CGAL)**。用 CGAL polygon mesh processing 填充孔洞。与上游 VESPA Hole Filling 同类，面板独立；新管线优先用本滤镜。

输入 `vtkDataSet`（必要时先 GeometryFilter）。可选 **Selection**（port 1，`SelectionInput`）可先挖掉一块再补洞。`FairingContinuity` 默认 1（0–2）。**不**把属性插值到新补的三角面上（`SetUpdateAttributes` 是 no-op）。

详见同目录 `SHYXHoleFillFilter.xml`。
