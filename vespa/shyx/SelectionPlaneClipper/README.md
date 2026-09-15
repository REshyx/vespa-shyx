# vtkSHYXSelectionPlaneClipper

用平面裁剪网格。默认切掉选区附近的小块；`InvertResult` 改为保留小块、切掉大块。初始平面来自当前选区：三角面/多边形用面积加权形心与法向；线、点在非共线时用 PCA 拟合，否则用邻面（或点）法向。封顶可选从平面原点做车轮状扇形三角化（`WheelCap`）。可显示交互切平面（`shyx_selection_plane_clipper`）。**Vascular** 工具条第 3 步。纯 VTK。

参数见同目录 `SHYXSelectionPlaneClipper.xml`。
