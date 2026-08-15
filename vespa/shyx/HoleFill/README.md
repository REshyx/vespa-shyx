# vtkSHYXHoleFillFilter

用 CGAL polygon mesh processing 填充 `vtkPolyData` 上的孔洞。与上游 VESPA Hole Filling 同类，面板独立；新管线优先用本滤镜。

主要参数：公平连续性（fairing continuity）、是否插值属性。详见 `ParaViewPlugin/SHYXHoleFillFilter.xml`。
