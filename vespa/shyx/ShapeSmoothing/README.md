# vtkSHYXShapeSmoothing

CGAL 形状平滑，三种算法可切换：Mean Curvature Flow / Angle & Area / Fair。需要多算法或 Fair 时用本滤镜，不要用上游单一 MCF 的 VESPA Shape Smoothing。

顶层 `SmoothingMethod` 驱动子参数组。详见 `ParaViewPlugin/SHYXShapeSmoothing.xml`。
