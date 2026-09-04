# vtkSHYXAutoMeshRepair

菜单 **SHYX Auto Mesh Repair**。SHYX Mesh Checker 针对交叉面的下一步：把自交面按位置分簇，每簇当作选区，扩几圈后再做与 [Selection: Fill, Alpha Wrap, Union](../SelectionFillAlphaReunion/README.md) 相同的局部补洞 + Alpha Wrap + 布尔并 + 接缝 remesh/smooth。

需要 CGAL ≥ 5.5（内部 `VESPA_ALPHA_WRAPPING`）。

**端口 0**：修复后的 `vtkPolyData`。Field data：`SHYXAutoMeshRepairFirstPassClusters`、`SHYXAutoMeshRepairClustersRepaired`、`SHYXAutoMeshRepairRemainingClusters`。  
**端口 1**：第一轮检测到的自交三角形，cell 数组 `SHYX_ClusterId`（从 1 起）、`SHYX_CheckReason=3`。

每轮只修最大的一簇，然后重新检测，直到没有自交或达到 `MaxPasses`。参数组见同目录 `SHYXAutoMeshRepair.xml`。
