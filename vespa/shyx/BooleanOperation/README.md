# vtkSHYXBooleanOperationFilter

CGAL 布尔运算的 **relaxed** 版本，**不要求封闭网格**。开放网格用本滤镜；封闭且要严格自交策略时用上游 VESPA Boolean。

参数：运算类型（并/交/差等）。详见 `ParaViewPlugin/SHYXBooleanOperationFilter.xml`。
