# vtkSHYXBooleanOperationFilter

菜单 **SHYX Boolean (CGAL, relaxed)**。CGAL 布尔的 relaxed 版本，**不因网格开放而直接 abort**；CGAL 仍可能失败。封闭且要严格自交策略时用上游 VESPA Boolean。

两个 `vtkPolyData` 输入：**Input** 与 **Source**（Tool mesh）。`OperationType`：0 Difference（默认）、1 Intersection、2 Union。`ThrowOnSelfIntersection` 默认关。

详见 `ParaViewPlugin/SHYXBooleanOperationFilter.xml`。
