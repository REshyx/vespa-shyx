# vtkSHYXVectorFieldTopology

SHYX 对 VTK `vtkVectorFieldTopology` 的薄封装，使该滤镜作为 `vespa/shyx` 模块进入 **Filters → SHYX**，而不是仅在 XML 里直接引用上游类。

**输入**：带三维点矢量的 `vtkImageData` / `vtkUnstructuredGrid` / AMR。  
**输出端口**：临界点、分离线、分离面、边界切换点、边界切换分离线。  
Proxy：`SHYXVectorFieldTopology()`。

参数与 VTK 原版一致（积分步长单位、最大步数、是否计算分离面等），见同目录 `SHYXVectorFieldTopology.xml`。
