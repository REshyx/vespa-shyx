# vtkSHYXMeshSimplification

菜单 **SHYX Mesh Simplification**。三角网格一次 CGAL `edge_collapse`。`Algorithm` 选代价和落点，停止条件、边界和尖锐边对每种算法都生效。

贴着约束边的顶点不会被挪走；两端都贴着约束边时这次折叠被拒绝。约束边本身不折叠。

不是各向同性 remesh（`SHYXAdaptiveIsotropicRemesher`）。`SHYXEdgeCollapse` 和 `SHYXSubsetCoarsen` 仍是单独的滤镜。

| 参数 | 含义 |
| :--- | :--- |
| `Algorithm` | 0 Lindstrom–Turk（可挪点）；1 平面 QEM；2 三角形 QEM；3 概率三角形 QEM；4 边长 + 中点；5 端点子集（只留原顶点）。 |
| `StopCriterion` | 0 边数比例；1 剩余无向边数降到阈值以下；2 下一条边长于 `EdgeLength` 时停。 |
| `PreserveBoundary` | 默认开：边界边不折叠。 |
| `DetectFeatureEdges` / `ProtectAngle` | 按二面角把尖锐边标成约束边。默认关。 |
| `PreventNormalFlip` | 默认开：折叠若会翻面则拒绝。 |
| `VolumeWeight` / `BoundaryWeight` / `ShapeWeight` | 仅 Lindstrom–Turk。默认 0.5 / 0.5 / 0，与 CGAL `LindstromTurk_params` 相同。 |
| `DiscontinuityMultiplier` | 三种 QEM 的不连续权重。默认 100。 |
| `EndpointRanking` | 仅端点子集。0 平面 QEM；1 折叠后最小内角。 |

输入必须是纯三角 `vtkPolyData`（混合单元先 Triangulate）。约束或 link condition 可能使停止条件提前达到，此时会发 warning。
