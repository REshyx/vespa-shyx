# vtkSHYXResampleLines

对 `vtkPolyData` 线网按指定弧长间距重采样。可选前置 **Fuse** 把近点合并以保证连通；度不为 2 的顶点当作特征点保留；各分支独立 resample。间距不小于分支长度时只保留该分支两端，短分支不会被删掉。纯 VTK。

参数见同目录 `SHYXResampleLines.xml`。
