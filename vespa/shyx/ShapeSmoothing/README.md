# vtkSHYXShapeSmoothing

菜单 **SHYX Shape Smoothing**。CGAL 形状平滑，三种算法可切换：Mean Curvature Flow / Angle & Area / Fair。需要多算法或 Fair 时用本滤镜，不要用上游单一 MCF 的 VESPA Shape Smoothing。

顶层 `SmoothingMethod` 默认 0（Shape MCF）；`NumberOfIterations` 默认 1（Fair 忽略该参数）。端口 0：平滑后表面；端口 1：特征边诊断（线 + 约束顶点）。

详见 `ParaViewPlugin/SHYXShapeSmoothing.xml`。
