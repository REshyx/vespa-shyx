# vtkSHYXSubsetCoarsen

菜单 **SHYX Subset Coarsen**。三角表面**子集粗化**：只把边折叠到某一个原端点，**不加新点**，留下来的点坐标也不改。

不是各向同性 remesh（`SHYXAdaptiveIsotropicRemesher` 会 split / 松弛 / 投影），也不是 `SHYXEdgeCollapse`（Lindstrom–Turk / QEM / 中点会把存活顶点挪走）。

| 参数 | 含义 |
| :--- | :--- |
| `EdgeCountRatio` | 当前无向边数 / 初始边数 **严格小于** 此值时停止（与 Edge Collapse 相同的 CGAL predicate）。特征/边界约束或 link condition 可能提前停在更高比例。 |
| `CostStrategy` | **0 Plane QEM**（默认）：两端点上的平面二次误差，外形保真。**1 Min angle**：折叠后剩余星形的最小内角，单元质量优先。两种都只留原端点。 |
| `PreserveBoundary` | 默认开：边界边不折叠，边界折线锁定。关：允许删边界点（仍是原顶点子集）。 |
| `DetectFeatureEdges` / `ProtectAngle` | 按二面角把尖锐边标成约束边，不折叠。 |
| `PreventNormalFlip` | 默认开：折叠若会翻面则拒绝。 |

输入必须是纯三角 `vtkPolyData`（混合单元先 Triangulate）。参数细节见同目录 `SHYXSubsetCoarsen.xml`。
