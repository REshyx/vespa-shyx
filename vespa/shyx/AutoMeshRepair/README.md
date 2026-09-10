# vtkSHYXAutoMeshRepair

菜单 **SHYX Auto Mesh Repair**。前半段与 [Mesh Checker](../MeshChecker/README.md) 相同（汤边 / 边界环 / 自交诊断，以及 orient + `repair_polygon_soup`）。**默认不修交叉面**。

勾选 **Repair self-intersections** 后，**Repair stage** 默认是两步里的第一步，避免旧版「抽出 + Alpha Wrap + 立刻和整张网格 CGAL union」在 corefinement 上卡死：

1. **Extract and Alpha Wrap**（默认）：自交面分簇、扩圈、每簇补洞 + Alpha Wrap。**端口 0** 是挖掉这些簇并补洞后的剩余网格；**端口 2** 是包好的补丁（cell 数组 `SHYXAutoMeshRepairClusterId`）。这一步不做布尔并。
2. **Union remainder + wrapped**：再加一个 Auto Mesh Repair，Input 接上一步端口 0，**Wrapped patches** 接端口 2，做 CGAL union + 可选接缝 remesh / smooth（与 [Selection: Fill, Alpha Wrap, Union](../SelectionFillAlphaReunion/README.md) 相同）。
3. **One-shot**：旧行为（每 pass 修一簇再重检）。大网格上 union 仍可能卡住。

需要 CGAL ≥ 5.5（内部 `VESPA_ALPHA_WRAPPING`）。

**端口 0**：汤修复网格；extract 阶段为 remainder；union / one-shot 为并回结果。Field data：`SHYXAutoMeshRepairStage`、`SHYXAutoMeshRepairFirstPassClusters`、`SHYXAutoMeshRepairClustersRepaired`、`SHYXAutoMeshRepairRemainingClusters`。  
**端口 1**：与 Mesh Checker 相同的非法图元（`SHYX_CheckReason` 1/2/3）。  
**端口 2**：extract 阶段的 Alpha Wrap 补丁；其它阶段为空。

参数组见同目录 `SHYXAutoMeshRepair.xml`。
