# vtkSHYXPointExtrudeFilter

菜单 **SHYX Point Extrude**。把输入 `vtkPolyData` 的**每一个点**沿 `vtkPolyDataNormals`（默认）或三分量点矢量数组位移。可选每点标量乘数。**没有**选区端口。默认 `ExtrusionDistance=1`，`UseNormalsForDirection=1`。

参数见 `ParaViewPlugin/SHYXPointExtrude.xml`。
